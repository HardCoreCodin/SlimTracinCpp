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

    Geometry *hit_geometry = nullptr;
    Material *hit_material = nullptr;

    Shader shader;
    RayHit _world_hit;
    Sampler sampler{_world_hit, scene.textures};
    mat3 inverted_camera_rotation;
    vec3 start, right, down, camera_position;
    vec2 screen_center_to_pixel_center;
    f32 squared_distance_to_projection_plane, z_depth;
    u32 max_depth = 4;
    bool use_gpu = false;
    bool use_ssb = true;

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
        vec3 direction = start;
        Pixel *pixel = canvas.pixels;

//        vec2 screen_center_coords{canvas.dimensions.h_width, canvas.dimensions.h_height};
//        vec2 screen_center_to_pixel_center = _world_ray.pixel_coords - screen_center_coords;
//        f32 distance_to_projection_plane = canvas.dimensions.h_height * camera_focal_length;
//        f32 t_to_pixel = sqrtf(screen_center_to_pixel_center.squaredLength() + distance_to_projection_plane * distance_to_projection_plane);
//
        screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;
        screen_center_to_pixel_center.y = canvas.dimensions.h_height - 0.5f;
        squared_distance_to_projection_plane = canvas.dimensions.h_height * camera.focal_length;
        squared_distance_to_projection_plane *= squared_distance_to_projection_plane;

        vec2i &coord{_world_ray.pixel_coords};

        if (render_mode == RenderMode_Beauty) {
            for (coord.y = 0; _world_ray.pixel_coords.y < height; coord.y++) {
                direction = start;
                screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;

                for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
                    castPrimaryRay(direction);
                    renderPixelBeauty(canvas);

                    direction += camera.right;
                    screen_center_to_pixel_center.x += 1.0f;
                }

                start -= camera.up;
                screen_center_to_pixel_center.y -= 1.0f;
            }
        } else
//            if (render_mode == RenderMode_Classic) {
//            for (coord.y = 0; _world_ray.pixel_coords.y < height; coord.y++) {
//                direction = start;
//                screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;
//
//                for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
//                    if (castPrimaryRay(direction))
//                        renderPixelClassic(canvas);
//
//                    direction += camera.right;
//                    screen_center_to_pixel_center.x += 1.0f;
//                }
//
//                start -= camera.up;
//                screen_center_to_pixel_center.y -= 1.0f;
//            }
//        } else
        {
            for (coord.y = 0; _world_ray.pixel_coords.y < height; coord.y++) {
                direction = start;
                screen_center_to_pixel_center.x = 0.5f - canvas.dimensions.h_width;

                for (coord.x = 0; coord.x < width; coord.x++, pixel++) {
                    if (castPrimaryRay(direction))
                        canvas.setPixel(_world_ray.pixel_coords.x, _world_ray.pixel_coords.y, shadeDebug(), 1, z_depth);

                    direction += camera.right;
                    screen_center_to_pixel_center.x += 1.0f;
                }

                start -= camera.up;
                screen_center_to_pixel_center.y -= 1.0f;
            }
        }
    }

    INLINE_XPU bool castPrimaryRay(vec3 direction) {
        f32 scaling_factor = 1.0f / direction.length();
        direction *= scaling_factor;
        z_depth = INFINITY;

        _world_ray.reset(camera_position, direction);
        hit_geometry = use_ssb ? findClosestGeoForPrimaryRay() : _trace(_world_ray, _world_hit);
        if (!hit_geometry) return false;

        _world_hit.cone_width_scaling_factor = scaling_factor / sqrtf(
                screen_center_to_pixel_center.squaredLength() +
                squared_distance_to_projection_plane
        );
        finalizeHit(hit_geometry, _world_hit);
        prepareForShading();
        z_depth = (inverted_camera_rotation * (_world_hit.position - camera_position)).z;

        return true;
    }

    INLINE_XPU Color shadeDebug() {
        switch (render_mode) {
            case RenderMode_UVs      : return getColorByUV(_world_hit.uv);
            case RenderMode_Depth    : return getColorByDistance(_world_hit.distance);
            case RenderMode_MipLevel : return hit_material->isTextured() ? MIP_LEVEL_COLORS[scene.textures[0].mipLevel(_world_hit.uv_coverage)] : Grey;
            case RenderMode_Normals  : return directionToColor(_world_hit.normal);
            case RenderMode_NormalMap: return directionToColor(sampler.sampleNormal());
            default: return Black;
        }
    }

    INLINE_XPU void renderPixelBeauty(const Canvas &canvas) {
        bool hit = hit_geometry;
        Color color;

        if (hit && !_world_hit.from_behind && hit_material->isEmissive()) color = hit_material->emission;
        if (scene.lights) {
            if (hit) {
                shadePBR(color);
            } else {
                hit = lights_shader.shadeLights(camera_position, _world_ray.direction, INFINITY, color);
                if (hit) z_depth = INFINITY;
            }
        }

        if (hit) {
            color.applyToneMapping();
            canvas.setPixel(_world_ray.pixel_coords.x, _world_ray.pixel_coords.y, color, 1, z_depth);
        }
    }

//    INLINE_XPU void renderPixelClassic(const Canvas &canvas) {
//        bool hit = _world_hit.geometry;
//        Color color{hit && !_world_hit.from_behind && _world_hit.material->isEmissive() ? Black : _world_hit.material->emission};
//        if (scene.lights) {
//            if (hit) {
//                shadeClassic(color);
//            } else {
//                hit = lights_shader.shadeLights(camera_position, _world_ray.direction, INFINITY, color);
//                if (hit) z_depth = INFINITY;
//            }
//        }
//
//        if (hit) {
//            color.applyToneMapping();
//            canvas.setPixel(_world_ray.pixel_coords.x, _world_ray.pixel_coords.y, color, 1, z_depth);
//        }
//    }

//    INLINE_XPU void shadeClassic(Color &color) {
//        color += scene.ambient_light.color;
//
//        Light *light = scene.lights;
//        for (u32 i = 0; i < scene.counts.lights; i++, light++) {
//            if (_world_hit.isFacingLight(*light) &&
//                !inShadow(_world_hit.position,
//                          _world_hit.light_direction,
//                          _world_hit.light_distance))
//                _world_hit.shadeFromLightClassic(*light, color);
//        }
//    }

    void shadePBR(Color &color) {
        f32 max_distance = _world_hit.distance;

        Light *light;
        Color current_color;
        Color throughput = 1.0f;
        u32 depth_left = max_depth;
        _world_ray.depth = _temp_ray.depth = _shadow_ray.depth = 1;
        while (depth_left) {
            current_color = Black;

            light = scene.lights;
            for (u32 i = 0; i < scene.counts.lights; i++, light++)
                if (shader.isFacingLight(*light, _world_hit.position) && !inShadow(_world_hit.position, shader.L, shader.Ld))
                    shader.shadeFromLight(*light,current_color);

//            if (scene_has_emissive_quads &&
//                shadeFromEmissiveQuads(ray, current_color))
//                max_distance = _world_hit.distance;

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                lights_shader.shadeLights(_world_ray.origin, _world_ray.direction, max_distance, color);

            if ((hit_material->isReflective() ||
                 hit_material->isRefractive()) &&
                --depth_left) {
                _world_ray.depth++;
                _temp_ray.depth++;
                _shadow_ray.depth++;
                vec3 &next_ray_direction{hit_material->isRefractive() ? shader.RF : shader.R};

//                Color next_ray_throughput{White};
//                shader.refracted = shader.refracted && hit_material->isRefractive();
//                if (shader.brdf == BRDF_CookTorrance) {
//                    shader.cos_wi_h = clampedValue(shader.R.dot(shader.N));
//                    shader.F = schlickFresnel(shader.cos_wi_h, shader.F0);
//                    shader.D = ggxTrowbridgeReitz_D(shader.roughness, 1.0f);
//                    shader.G = ggxSchlickSmith_G(shader.roughness, shader.cos_wi_h, shader.cos_wi_h);
//                    shader.Ks = shader.F * (shader.D * shader.G
//                                            /
//                                            (4.0f * shader.cos_wi_h * shader.cos_wi_h)
//                    );
//
//                    if (shader.refracted)
//                        next_ray_throughput = shader.Ks;
//                    else
//                        next_ray_throughput -= shader.Ks;
//                } else {
//                    if (shader.refracted)
//                        next_ray_throughput = hit_material->reflectivity;
//                    else
//                        next_ray_throughput -= hit_material->reflectivity;
//                }
//                throughput *= next_ray_throughput;

                hit_geometry = findClosestGeometry(_world_hit.position, next_ray_direction, _world_hit);
                if (hit_geometry) {
                    prepareForShading();
                    if (hit_geometry->type == GeometryType_Quad && hit_material->isEmissive()) {
                        color = _world_hit.from_behind ? Black : hit_material->emission;
                        break;
                    }

                    if (hit_material->brdf != BRDF_CookTorrance)
                        throughput *= hit_material->reflectivity;

                    continue;
                }
            }

            break;
        }
    }

    Geometry* findClosestGeoForPrimaryRay() {
        _temp_hit.distance = _world_hit.distance = INFINITY;
        _temp_hit.cone_width_scaling_factor = _world_hit.cone_width_scaling_factor;
        vec2i &coord{_world_ray.pixel_coords};
        RectI *bounds = scene.screen_bounds;
        Geometry *geo = scene.geometries;
        Geometry *hit_geo = nullptr;

        for (u32 i = 0; i < scene.counts.geometries; i++, geo++, bounds++) {
            if (!(geo->flags & GEOMETRY_IS_VISIBLE) ||
                (i32)coord.x <  bounds->left ||
                (i32)coord.x >  bounds->right ||
                (i32)coord.y <  bounds->top ||
                (i32)coord.y >  bounds->bottom)
                continue;

            if (hitGeometryInLocalSpace(*geo, _world_ray, _temp_hit, false)) {
                if (_temp_hit.distance < _world_hit.distance) {
                    hit_geo = geo;
                    _world_hit = _temp_hit;
                    _closest_hit_ray_direction = _temp_ray.direction;
                }
            }
        }

        return hit_geo;
    }

    INLINE_XPU void prepareForShading() {
        hit_material = sampler.material = scene.materials + hit_geometry->material_id;
        if (hit_material->hasNormalMap()) _world_hit.normal = sampler.sampleNormal(_world_hit.normal);
        shader.reset(*hit_material,
                     _world_hit.normal,
                     _world_ray.direction,
                     _world_hit.from_behind,
                     hit_material->hasAlbedoMap() ? sampler.sampleAlbedo() : White);
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
//            quad.transform.internPosAndDir(_world_hit.viewing_origin, _world_hit.viewing_direction, Ro, Rd);
//            if (local_ray.hitsDefaultQuad(_world_hit, quad.flags & GEOMETRY_IS_TRANSPARENT)) {
//                _world_hit.position = quad.transform.externPos(_world_hit.position);
//                f32 t2 = (_world_hit.position - _world_hit.viewing_origin).squaredLength();
//                if (t2 < _world_hit.distance*_world_hit.distance) {
//                    local_hit.distance = sqrtf(t2);
//                    local_hit.id = i;
////                local_hit.geo_type = GeometryType_Quad;
////                local_hit.material_id = quad.material_id;
//                    *((RayHit*)&_world_hit) = local_hit;
//                    found = true;
//                }
//            }
//
//            emissive_quad_normal.x = emissive_quad_normal.z = 0;
//            emissive_quad_normal.y = 1;
//            emissive_quad_normal = quad.transform.rotation * emissive_quad_normal;
//            _world_hit.light_direction = ray.direction = quad.transform.position - _world_hit.position;
//            if (emissive_quad_normal.dot(_world_hit.light_direction) >= 0)
//                continue;
//
//            f32 emission_intensity = _world_hit.normal.dot(getAreaLightVector(quad.transform, _world_hit.position, _world_hit.emissive_quad_vertices));
//            if (emission_intensity > 0) {
//                bool skip = true;
//                for (u8 j = 0; j < 4; j++) {
//                    if (_world_hit.normal.dot(_world_hit.emissive_quad_vertices[j] - _world_hit.position) >= 0) {
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
//                    if (&quad == _world_hit.geometry ||
//                        &quad == &shadowing_primitive ||
//                        emissive_quad_normal.dot(shadowing_primitive.transform.position - quad.transform.position) <= 0)
//                        continue;
//
//                    shadowing_primitive.transform.internPosAndDir(_world_hit.position, _world_hit.light_direction, Ro, Rd);
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
//                    _world_hit.NdotL = clampedValue(_world_hit.normal.dot(_world_hit.light_direction));
//                    if (_world_hit.NdotL > 0.0f) {
//                        Color radiance_fraction = _world_hit.radianceFraction();
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