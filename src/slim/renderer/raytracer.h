#pragma once

#include "./closest_hit/lights.h"
#include "../viewport/viewport.h"
#include "./tracers/scene.h"
#include "./shaded.h"
#include "./SSB.h"


#ifdef __CUDACC__
#include "./raytracer_kernel.H"
#endif



struct RayTracer : public SceneTracer {
    LightsShader lights_shader;
    enum RenderMode render_mode = RenderMode_Beauty;

    Surface surface;
    Ray ray;

    mat3 inverted_camera_rotation;
    vec3 start, right, down, camera_position;
    vec2 screen_center_to_pixel_center;
    f32 squared_distance_to_projection_plane, z_depth;
    u32 max_depth = 4;
    bool use_gpu = false;
    bool use_ssb = false;

    INLINE_XPU RayTracer(Scene &scene, u32 *stack, u32 stack_size, u32 *mesh_stack, u32 mesh_stack_size, RenderMode render_mode = RenderMode_Beauty) :
            SceneTracer{scene, stack, stack_size, mesh_stack, mesh_stack_size}, lights_shader{scene.lights, scene.counts.lights}, render_mode{render_mode} {}

    RayTracer(Scene &scene, u32 stack_size, u32 mesh_stack_size = 0, RenderMode render_mode = RenderMode_Beauty, memory::MonotonicAllocator *memory_allocator = nullptr) :
            SceneTracer{scene, stack_size, mesh_stack_size, memory_allocator}, lights_shader{scene.lights, scene.counts.lights}, render_mode{render_mode}  {}

    void render(const Viewport &viewport, bool update_scene = true) {
        Canvas &canvas = viewport.canvas;
        Camera &camera = *viewport.camera;

        inverted_camera_rotation = camera.orientation.inverted();
        ray.origin = camera.position;

        f32 left_of_center = 1.0f + (canvas.antialias == SSAA ? -(canvas.dimensions.f_width) : -canvas.dimensions.h_width);
        f32 up_of_center = -1.0f + (canvas.antialias == SSAA ? (canvas.dimensions.f_height) : canvas.dimensions.h_height);

        camera_position = camera.position;
        down = -camera.orientation.up;
        right = camera.orientation.right;
        start = camera.orientation.forward.scaleAdd(camera.focal_length * canvas.dimensions.h_height, right.scaleAdd(left_of_center, camera.orientation.up * up_of_center));

        if (update_scene) {
            scene.updateAABBs();
            scene.updateBVH();
            if (use_ssb) updateScreenBounds(scene, viewport);
        }
        if (use_gpu) renderOnGPU(canvas, camera);
        else         renderOnCPU(canvas, camera);
    }

    void renderOnCPU(const Canvas &canvas, const Camera &camera) {
        u16 width = canvas.dimensions.width * (canvas.antialias == SSAA ? 2 : 1);
        u16 height = canvas.dimensions.height * (canvas.antialias == SSAA ? 2 : 1);
        Pixel *pixel = canvas.pixels;

        screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;
        screen_center_to_pixel_center.y = canvas.dimensions.h_height - 0.5f;
        squared_distance_to_projection_plane = canvas.dimensions.h_height * camera.focal_length;
        squared_distance_to_projection_plane *= squared_distance_to_projection_plane;

        vec2i &coord{ray.pixel_coords};
        vec3 direction;

        if (render_mode == RenderMode_Beauty) {
            for (coord.y = 0; ray.pixel_coords.y < height; coord.y++) {
                direction = start;
                screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;

                for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
                    castPrimaryRay(direction);
                    renderPixelBeauty(canvas);

                    direction += camera.orientation.right;
                    screen_center_to_pixel_center.x += 1.0f;
                }

                start -= camera.orientation.up;
                screen_center_to_pixel_center.y -= 1.0f;
            }
        } else
            for (coord.y = 0; ray.pixel_coords.y < height; coord.y++) {
                direction = start;
                screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;

                for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
                    if (castPrimaryRay(direction))
                        canvas.setPixel(ray.pixel_coords.x, ray.pixel_coords.y, shadeDebug(), 1, z_depth);

                    direction += camera.orientation.right;
                    screen_center_to_pixel_center.x += 1.0f;
                }

                start -= camera.orientation.up;
                screen_center_to_pixel_center.y -= 1.0f;
            }
    }

    INLINE_XPU bool castPrimaryRay(vec3 direction) {
        f32 scaling_factor = 1.0f / direction.length();
        direction *= scaling_factor;
        z_depth = INFINITY;

        ray.reset(camera_position, direction);
        surface.geometry = _trace(ray, surface.hit);
        if (!surface.geometry) return false;

        surface.hit.cone_width_scaling_factor = scaling_factor / sqrtf(
                screen_center_to_pixel_center.squaredLength() +
                squared_distance_to_projection_plane
        );
        finalizeHit(surface.geometry, ray, surface.hit);
        surface.prepareForShading(*this, ray);
        z_depth = (inverted_camera_rotation * (surface.hit.position - camera_position)).z;

        return true;
    }

    INLINE_XPU Color shadeDebug() {
        switch (render_mode) {
            case RenderMode_UVs      : return getColorByUV(surface.hit.uv);
            case RenderMode_Depth    : return getColorByDistance(surface.hit.distance);
            case RenderMode_MipLevel : return surface.material->isTextured() ? MIP_LEVEL_COLORS[scene.textures[0].mipLevel(surface.hit.uv_coverage)] : Grey;
            case RenderMode_Normals  : return directionToColor(surface.hit.normal);
            case RenderMode_NormalMap: return directionToColor(sampleNormal(*surface.material, surface.hit, scene.textures));
            default: return Black;
        }
    }

    INLINE_XPU void renderPixelBeauty(const Canvas &canvas) {
        bool hit = surface.geometry != nullptr;
        Color color;

        if (hit && !surface.hit.from_behind && surface.material->isEmissive()) color = surface.material->emission;
        if (scene.lights) {
            if (hit) {
                shade(color);
            } else {
                hit = lights_shader.shadeLights(camera_position, ray.direction, INFINITY, color);
                if (hit) z_depth = INFINITY;
            }
        }

        if (hit) {
            color.applyToneMapping();
            canvas.setPixel(ray.pixel_coords.x, ray.pixel_coords.y, color, 1, z_depth);
        }
    }

    void shade(Color &color) {
        f32 max_distance = surface.hit.distance;

        Color current_color;
        Color throughput = 1.0f;
        u32 depth_left = max_depth;
        ray.depth = shadow_ray.depth = _temp_ray.depth = 1;
        while (depth_left) {
            current_color = Black;

            for (u32 i = 0; i < scene.counts.lights; i++)
                surface.shadeFromLight(scene.lights[i], *this, current_color);

            if (scene_has_emissive_quads &&
                surface.geometry != (scene.geometries + 4) &&
                surface.shadeFromEmissiveQuads(*this, current_color))
                max_distance = surface.hit.distance;

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                lights_shader.shadeLights(ray.origin, ray.direction, max_distance, color);

            if ((surface.material->isReflective() ||
                 surface.material->isRefractive()) &&
                --depth_left) {
                ray.depth++;
                _temp_ray.depth++;
                shadow_ray.depth++;
                vec3 &next_ray_direction{surface.material->isRefractive() ? surface.RF : surface.R};

                Color next_ray_throughput{White};
                surface.refracted = surface.refracted && surface.material->isRefractive();
                if (surface.material->brdf == BRDF_CookTorrance) {
                    surface.F = schlickFresnel(clampedValue(surface.N.dot(surface.R)), surface.material->reflectivity);

                    if (surface.refracted)
                        next_ray_throughput -= surface.F;
                    else
                        next_ray_throughput = surface.F;
                } else {
                    if (surface.refracted)
                        next_ray_throughput = surface.material->reflectivity;
                    else
                        next_ray_throughput -= surface.material->reflectivity;
                }
                throughput *= next_ray_throughput;

                ray.origin = surface.hit.position;
                ray.direction = next_ray_direction;
                surface.geometry = findClosestGeometry(ray, surface.hit);
                if (surface.geometry) {
                    surface.prepareForShading(*this, ray);
                    if (surface.geometry->type == GeometryType_Quad && surface.material->isEmissive()) {
                        color = surface.hit.from_behind ? Black : surface.material->emission;
                        break;
                    }

                    continue;
                }
            }

            break;
        }
    }

#ifdef __CUDACC__
    void renderOnGPU(canvas, camera) {
        Dimensions &dim = canvas.dimensions;
        u32 pixel_count = dim.width_times_height;
        u32 threads = 256;
        u32 blocks  = pixel_count / threads;
        if (pixel_count < threads) {
            threads = pixel_count;
            blocks = 1;
        } else if (pixel_count % threads)
            blocks++;

        d_render<<<blocks, threads>>>(
                viewport.projection_plane,
                viewport.settings.render_mode,
                viewport.camera.transform.position,
                viewport.camera.transform.rotation_inverted,
                viewport.trace,

                dim.width,
                pixel_count,

                *scene,
                d_scene_bvh_leaf_ids,
                d_scene_bvh_nodes,
                d_mesh_bvh_nodes,
                d_meshes,
                d_triangles,
                d_lights,
                d_area_lights,
                d_materials,
                d_textures,
                d_texture_mips,
                d_texel_quads,
                d_geometries,

                d_mesh_bvh_leaf_ids,
                d_mesh_bvh_node_counts,
                d_mesh_triangle_counts);

        checkErrors()
        downloadN(d_pixels, viewport.frame_buffer.float_pixels, dim.width_times_height)
    }

#else
    void renderOnGPU(const Canvas &canvas, const Camera &camera) { renderOnCPU(canvas, camera); }
#endif
};