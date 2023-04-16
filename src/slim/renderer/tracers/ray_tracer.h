#pragma once

#include "../../viewport/viewport.h"
#include "../shaders/surface_shader.h"
//
////#include "./SSB.h"
//#define RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH 3
//#define RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE RenderMode_Beauty


struct RayTracerSettings {
    u8 max_depth;
    char skybox_color_texture_id;
    char skybox_radiance_texture_id;
    char skybox_irradiance_texture_id;
    f32 skybox_color_intensity;
    f32 skybox_radiance_intensity;
    f32 skybox_irradiance_intensity;
    RenderMode render_mode;
    ColorID mip_level_colors[9];
};

struct RayTracerProjection {
    mat3 inverted_camera_rotation;
    vec3 start, right, down, camera_position;

    f32 squared_distance_to_projection_plane;
    f32 sample_size;
    f32 Cx_start;
    f32 Cy_start;

    INLINE_XPU f32 getDepthAt(vec3 &position) const {
        return (inverted_camera_rotation * (position - camera_position)).z;
    }

    void reset(const Camera &camera, const Canvas &canvas) {
        const Dimensions &dim = canvas.dimensions;

        sample_size = canvas.antialias == SSAA ? 0.5f : 1.0f;
        squared_distance_to_projection_plane = dim.h_height * camera.focal_length;
        Cx_start = (sample_size * 0.5f) - dim.h_width;
        Cy_start = dim.h_height - (sample_size * 0.5f);

        inverted_camera_rotation = camera.orientation.inverted();
        camera_position = camera.position;
        down = -camera.orientation.up      * sample_size;
        right = camera.orientation.right   * sample_size;
        start = camera.orientation.right   * Cx_start +
                camera.orientation.up      * Cy_start +
                camera.orientation.forward * squared_distance_to_projection_plane;
        squared_distance_to_projection_plane *= squared_distance_to_projection_plane;
    }
};

struct RayTracer {
    SceneTracer scene_tracer;
    LightsShader lights_shader;
    SurfaceShader surface;
    Ray ray;
    RayHit hit;
    vec2 screen_center_to_pixel_center;
    f32 z_depth;

    INLINE_XPU RayTracer(u32 *stack, u32 *mesh_stack) : scene_tracer{stack, mesh_stack} {}

    explicit RayTracer(u32 stack_size, u32 mesh_stack_size, memory::MonotonicAllocator *memory_allocator = nullptr) :
        scene_tracer{stack_size, mesh_stack_size, memory_allocator}  {}

    INLINE_XPU void renderPixel(const Scene &scene, const RayTracerSettings &settings, const RayTracerProjection &projection, vec3 &direction, Color &color) {
        color = Black;

        z_depth = INFINITY;

        ray.reset(projection.camera_position, direction.normalized());
        surface.geometry = scene_tracer.trace(ray, hit, scene);

        bool render_beauty = settings.render_mode == RenderMode_Beauty;
        bool hit_found = surface.geometry != nullptr;
        if (hit_found) {
            hit.scaling_factor = 1.0f / sqrtf(
                screen_center_to_pixel_center.squaredLength() +
                    projection.squared_distance_to_projection_plane
            );
            scene_tracer.finalizeHit(surface.geometry, scene.materials, ray, hit);
            surface.prepareForShading(ray, hit, scene.materials, scene.textures);
            z_depth = projection.getDepthAt(hit.position);
            if (render_beauty) {
                if (!hit.from_behind && surface.material->isEmissive()) color = surface.material->emission;
                else shadePixel(scene, settings, color);
            } else
                switch (settings.render_mode) {
                    case RenderMode_UVs      : color = getColorByUV(hit.uv); break;
                    case RenderMode_Depth    : color = getColorByDistance(hit.distance); break;
                    case RenderMode_Normals  : color = directionToColor(hit.normal);  break;
                    case RenderMode_NormalMap: color = directionToColor(sampleNormal(*surface.material, hit, scene.textures));  break;
                    default                  : color = scene.counts.textures ?
                        settings.mip_level_colors[scene.textures[0].mipLevel(hit.uv_coverage)] : Grey;
                }
        }
        if (render_beauty) {
            if (!hit_found) {
                if (settings.skybox_color_texture_id >= 0)
                    color = scene.textures[settings.skybox_color_texture_id].sampleCube(
                        ray.direction.x,
                        ray.direction.y,
                        ray.direction.z
                    ).color * settings.skybox_color_intensity;

                lights_shader.shadeLights(scene.lights, scene.counts.lights,
                                          projection.camera_position, ray.direction,
                                          INFINITY, color);
            }
            if (color.r != 0.0f ||
                color.g != 0.0f ||
                color.b != 0.0f )
                color.applyToneMapping();
        }
    }

    INLINE_XPU Color cubemapColor(const vec3 &direction, const Texture &texture) {
        return texture.sampleCube(direction.x,direction.y,direction.z).color;
    }

    INLINE_XPU void shadePixel(const Scene &scene, const RayTracerSettings &settings, Color &color) {
        Color current_color;
        Color throughput = 1.0f;
        u32 depth_left = settings.max_depth;
        ray.depth = scene_tracer.shadow_ray.depth = scene_tracer.aux_ray.depth = 1;

        while (depth_left) {
            current_color = Black;

            for (u32 i = 0; i < scene.counts.lights; i++)
                surface.shadeFromLight(scene.lights[i], scene, scene_tracer, current_color);

            if (scene.flags & SCENE_HAD_EMISSIVE_QUADS &&
                surface.geometry != (scene.geometries + 4))
                surface.shadeFromEmissiveQuads(scene, scene_tracer.aux_ray, scene_tracer.aux_hit, current_color);

            if (settings.skybox_irradiance_texture_id >= 0 &&
                settings.skybox_radiance_texture_id >= 0) {
                surface.L = surface.N;
                surface.NdotL = 1.0f;
                Color D{cubemapColor(surface.N, scene.textures[settings.skybox_irradiance_texture_id])};
                Color S{cubemapColor(surface.R, scene.textures[settings.skybox_radiance_texture_id])};
                surface.radianceFraction();
                current_color = D.mulAdd(surface.Fd, surface.Fs.mulAdd(S, current_color));
            }

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                lights_shader.shadeLights(scene.lights, scene.counts.lights, ray.origin, ray.direction, hit.distance, color);

            if ((surface.material->isReflective() ||
                 surface.material->isRefractive()) &&
                --depth_left) {
                ray.depth++;
                scene_tracer.aux_ray.depth++;
                scene_tracer.shadow_ray.depth++;
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

                ray.origin = hit.position;
                ray.direction = next_ray_direction;
                surface.geometry = scene_tracer.findClosestGeometry(ray, hit, scene);
                if (surface.geometry) {
                    surface.prepareForShading(ray, hit, scene.materials, scene.textures);
                    if (surface.geometry->type == GeometryType_Quad && surface.material->isEmissive()) {
                        color = hit.from_behind ? Black : surface.material->emission;
                        break;
                    }

                    continue;
                } else if (settings.skybox_color_texture_id >= 0)
                    color = cubemapColor(ray.direction, scene.textures[settings.skybox_color_texture_id]).mulAdd(throughput, color);
            }

            break;
        }
    }
};