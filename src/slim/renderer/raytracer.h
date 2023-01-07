#pragma once

#include "./closest_hit/lights.h"
#include "../viewport/viewport.h"
#include "./tracers/scene.h"
#include "./shaded.h"
#include "./SSB.h"


#ifdef __CUDACC__
#include "./raytracer_kernel.h"
#endif



struct RayTracer : public SceneTracer {
    LightsShader lights_shader;
    enum RenderMode render_mode = RenderMode_Beauty;

    Shaded shaded;
    mat3 inverted_camera_rotation;
    vec3 start, right, down, camera_position;
    f32 camera_focal_length;
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

        inverted_camera_rotation = camera.rotation.inverted();
        _world_ray.origin = camera.position;

        f32 left_of_center = 1.0f + (canvas.antialias == SSAA ? -(canvas.dimensions.f_width) : -canvas.dimensions.h_width);
        f32 up_of_center = -1.0f + (canvas.antialias == SSAA ? (canvas.dimensions.f_height) : canvas.dimensions.h_height);

        camera_focal_length = camera.focal_length;
        camera_position = camera.position;
        down = -camera.up;
        right = camera.right;
        start = camera.forward.scaleAdd(camera.focal_length * canvas.dimensions.h_height, right.scaleAdd( left_of_center, camera.up * up_of_center));

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
        Color color{Black};
        vec3 direction = start;
        Pixel *pixel = canvas.pixels;

//        vec2 screen_center_coords{canvas.dimensions.h_width, canvas.dimensions.h_height};
//        vec2 screen_center_to_pixel_center = _world_ray.pixel_coords - screen_center_coords;
//        f32 distance_to_projection_plane = canvas.dimensions.h_height * camera_focal_length;
//        f32 t_to_pixel = sqrtf(screen_center_to_pixel_center.squaredLength() + distance_to_projection_plane * distance_to_projection_plane);
//
        vec2i &coord{_world_ray.pixel_coords};
        vec2 c2p{0.5f - canvas.dimensions.h_width, canvas.dimensions.h_height - 0.5f}; // Screen center to_pixel_center
        f32 d2 = canvas.dimensions.h_height * camera_focal_length; // Distance to projection plane
        d2 *= d2;
        for (coord.y = 0; _world_ray.pixel_coords.y < height; coord.y++) {
            direction = start;
            c2p.x = 0.5f - canvas.dimensions.h_width;

            for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
                renderPixel(canvas, direction, sqrtf(c2p.squaredLength() + d2), color);

                direction += camera.right;
                c2p.x += 1.0f;
            }

            start -= camera.up;
            c2p.y -= 1.0f;
        }
    }

    INLINE_XPU void renderPixel(const Canvas &canvas, vec3 direction, f32 t_to_pixel, Color &color) {
        f32 hit_depth = INFINITY;
        f32 scaling_factor = 1.0f / direction.length();
        direction *= scaling_factor;

        color = Black;

        _world_ray.reset(camera_position, direction);
        shaded.geometry = use_ssb ? findClosestGeoForPrimaryRay() : _trace(_world_ray, shaded);

        bool hit_light_only = false;
        bool hit = shaded.geometry;
        if (hit) {
            shaded.cone_width_scaling_factor = scaling_factor / t_to_pixel;
            finalizeHit(shaded.geometry, shaded);
            hit_depth = (inverted_camera_rotation * (shaded.position - camera_position)).z;
            color = shadePixel();
        } else if (render_mode == RenderMode_Beauty && scene.lights) {
            hit_light_only = lights_shader.shadeLights(camera_position, direction, INFINITY, color);
            if (hit_light_only)
                hit_depth = INFINITY;
        }

        if (render_mode == RenderMode_Beauty)
            color.applyToneMapping();

        if (hit || hit_light_only)
            canvas.setPixel(_world_ray.pixel_coords.x, _world_ray.pixel_coords.y, color, 1, hit_depth);
    }

    INLINE_XPU Color shadePixel() {
        shaded.prepareForShading(_world_ray, scene.materials, scene.textures);
        switch (render_mode) {
            case RenderMode_UVs      : return shaded.getColorByUV();
            case RenderMode_Depth    : return shaded.getColorByDistance();
            case RenderMode_MipLevel : return shaded.material->isTextured() ? shaded.getColorByMipLevel(scene.textures[0]) : Grey;
            case RenderMode_Normals  : return directionToColor(shaded.normal);
            case RenderMode_NormalMap: return directionToColor(shaded.sampleNormal(scene.textures));
            default: return shaded.material->isEmissive() ? (shaded.from_behind ? Black : shaded.material->emission) : shadeSurface();
        }
    }

    Color shadeSurface() {
        f32 max_distance = shaded.distance;

        Color current_color, color;
        Color throughput = 1.0f;
        u32 depth_left = max_depth;
        _world_ray.depth = _temp_ray.depth = _shadow_ray.depth = 1;
        while (depth_left) {
            bool is_ref = shaded.material->isReflective() ||
                          shaded.material->isRefractive();
            current_color = is_ref ? Black : scene.ambient_light.color;
            if (scene.lights) {
                for (u32 i = 0; i < scene.counts.lights; i++) {
                    const Light &light = scene.lights[i];
                    shaded.setLight(light);
                    if (shaded.NdotL > 0 && !inShadow(shaded.position, shaded.light_direction, shaded.light_distance))
                        current_color = ((light.intensity / shaded.light_distance_squared) * light.color).mulAdd(shaded.radianceFraction(), current_color);
                }
            }

//            if (scene_has_emissive_quads &&
//                shadeFromEmissiveQuads(ray, current_color))
//                max_distance = shaded.distance;

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                lights_shader.shadeLights(_world_ray.origin, _world_ray.direction, max_distance, color);

            if ((shaded.material->isReflective() ||
                 shaded.material->isRefractive()) &&
                --depth_left) {
                _world_ray.depth++;
                _temp_ray.depth++;
                _shadow_ray.depth++;
                vec3 &next_ray_direction{
                        shaded.material->isRefractive() ?
                        shaded.refracted_direction :
                        shaded.reflected_direction
                };

                shaded.geometry = findClosestGeometry(shaded.position, next_ray_direction, shaded);
                if (shaded.geometry) {
                    shaded.prepareForShading(_world_ray, scene.materials, scene.textures);
                    if (shaded.geometry->type == GeometryType_Quad && shaded.material->isEmissive()) {
                        color = shaded.from_behind ? Black : shaded.material->emission;
                        break;
                    }

                    if (shaded.material->brdf != BRDF_CookTorrance)
                        throughput *= shaded.material->reflectivity;

                    continue;
                }
            }

            break;
        }

        return color;
    }

    Geometry* findClosestGeoForPrimaryRay() {
        _temp_hit.distance = shaded.distance = INFINITY;
        _temp_hit.cone_width_scaling_factor = shaded.cone_width_scaling_factor;
        vec2i &coord{_world_ray.pixel_coords};
        RectI *bounds = scene.screen_bounds;
        Geometry *geo = scene.geometries;
        Geometry *hit_geo = nullptr;

        for (u32 i = 0; i < scene.counts.geometries; i++, geo++, bounds++) {
            if (!(geo->flags & GEOMETRY_IS_VISIBLE) ||
                (i32)coord.x <  bounds->left ||
                (i32)coord.x >  bounds->right ||
                (i32)coord.y >  bounds->top ||
                (i32)coord.y >  bounds->bottom)
                continue;

            if (_temp_hit.distance < shaded.distance) {
                hit_geo = geo;
                shaded = _temp_hit;
                _closest_hit_ray_direction = _temp_ray.direction;
            }
        }

        return hit_geo;
    }

//    INLINE bool shadeFromEmissiveQuads(Ray &ray, Color &color) {
//        vec3 &Rd = local_ray.direction;
//        vec3 &Ro = local_ray.origin;
//        vec3 emissive_quad_normal;
//        bool found = false;
//
//        for (u32 i = 0; i < scene.counts.geometries; i++) {
//            Geometry &quad = scene.geometries[i];
//            Material &emissive_material = scene.materials[quad.material_id];
//            if (quad.type != GeometryType_Quad || !(emissive_material.isEmissive()))
//                continue;
//
//            quad.transform.internPosAndDir(shaded.viewing_origin, shaded.viewing_direction, Ro, Rd);
//            if (local_ray.hitsDefaultQuad(shaded, quad.flags & GEOMETRY_IS_TRANSPARENT)) {
//                shaded.position = quad.transform.externPos(shaded.position);
//                f32 t2 = (shaded.position - shaded.viewing_origin).squaredLength();
//                if (t2 < shaded.distance*shaded.distance) {
//                    local_hit.distance = sqrtf(t2);
//                    local_hit.id = i;
////                local_hit.geo_type = GeometryType_Quad;
////                local_hit.material_id = quad.material_id;
//                    *((RayHit*)&shaded) = local_hit;
//                    found = true;
//                }
//            }
//
//            emissive_quad_normal.x = emissive_quad_normal.z = 0;
//            emissive_quad_normal.y = 1;
//            emissive_quad_normal = quad.transform.rotation * emissive_quad_normal;
//            shaded.light_direction = ray.direction = quad.transform.position - shaded.position;
//            if (emissive_quad_normal.dot(shaded.light_direction) >= 0)
//                continue;
//
//            f32 emission_intensity = shaded.normal.dot(getAreaLightVector(quad.transform, shaded.position, shaded.emissive_quad_vertices));
//            if (emission_intensity > 0) {
//                bool skip = true;
//                for (u8 j = 0; j < 4; j++) {
//                    if (shaded.normal.dot(shaded.emissive_quad_vertices[j] - shaded.position) >= 0) {
//                        skip = false;
//                        break;
//                    }
//                }
//                if (skip)
//                    continue;
//
//                f32 shaded_light = 1;
//                for (u32 s = 0; s < scene.counts.geometries; s++) {
//                    Geometry &shadowing_primitive = scene.geometries[s];
//                    if (&quad == shaded.geometry ||
//                        &quad == &shadowing_primitive ||
//                        emissive_quad_normal.dot(shadowing_primitive.transform.position - quad.transform.position) <= 0)
//                        continue;
//
//                    shadowing_primitive.transform.internPosAndDir(shaded.position, shaded.light_direction, Ro, Rd);
//                    Ro = Rd.scaleAdd(TRACE_OFFSET, Ro);
//
//                    f32 d = 1;
//                    if (shadowing_primitive.type == GeometryType_Sphere) {
//                        if (local_ray.hitsDefaultSphere(local_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT))
//                            d -= (1.0f - local_hit.distance) / (local_hit.distance * emission_intensity * 3);
//                    } else if (shadowing_primitive.type == GeometryType_Quad) {
//                        if (local_ray.hitsDefaultQuad(local_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT)) {
//                            local_hit.position.y = 0;
//                            local_hit.position.x = local_hit.position.x < 0 ? -local_hit.position.x : local_hit.position.x;
//                            local_hit.position.z = local_hit.position.z < 0 ? -local_hit.position.z : local_hit.position.z;
//                            if (local_hit.position.x > local_hit.position.z) {
//                                local_hit.position.y = local_hit.position.z;
//                                local_hit.position.z = local_hit.position.x;
//                                local_hit.position.x = local_hit.position.y;
//                                local_hit.position.y = 0;
//                            }
//                            d -= (1.0f - local_hit.position.z) / (local_hit.distance * emission_intensity);
//                        }
//                    }
//                    if (d < shaded_light)
//                        shaded_light = d;
//                }
//                if (shaded_light > 0) {
//                    shaded.NdotL = clampedValue(shaded.normal.dot(shaded.light_direction));
//                    if (shaded.NdotL > 0.0f) {
//                        Color radiance_fraction = shaded.radianceFraction();
//                        color = radiance_fraction.mulAdd(emissive_material.emission * (emission_intensity * shaded_light), color);
//                    }
//                }
//            }
//        }
//
//        return found;
//    }

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