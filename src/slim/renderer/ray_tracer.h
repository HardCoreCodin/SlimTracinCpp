#pragma once

#include "../viewport/viewport.h"
#include "./surface_shader.h"

//#define RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH 3
//#define RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE RenderMode_Beauty


struct RayTracerSettings {
    u8 max_depth;
    char skybox_color_texture_id;
    char skybox_radiance_texture_id;
    char skybox_irradiance_texture_id;
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
    SphereTracer sphereTracer;
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
                    ).color;

                shadeLights(scene.lights, scene.counts.lights, projection.camera_position, ray.direction, INFINITY, color);
            }
            if (color.r != 0.0f ||
                color.g != 0.0f ||
                color.b != 0.0f )
                color.applyToneMapping();
        }
    }

    INLINE_XPU bool shadeLights(const Light *lights, const u32 light_count, const vec3 &Ro, const vec3 &Rd, f32 max_distance, Color &color) {
        bool hit_light = false;
        f32 t1, t2, density;

        for (u32 i = 0; i < light_count; i++) {
            const Light &light = lights[i];

            if (sphereTracer.hit(light.position_or_direction, 64.0f / light.intensity, Ro, Rd, max_distance)) {
                // Integrate density along the ray's traced path through the spherical volume::
                t1 = Max(sphereTracer.t_near, 0);
                t2 = Min(sphereTracer.t_far, sphereTracer.t_max);
                density = (
                    sphereTracer.c*t1 - sphereTracer.b*t1*t1 + t1*t1*t1/3.0f - (
                    sphereTracer.c*t2 - sphereTracer.b*t2*t2 + t2*t2*t2/3.0f   )
                ) * (3.0f / 4.0f);
                color = light.color.scaleAdd(  pow(density, 8.0f) * 4, color);
                hit_light = true;
            }
        }

        return hit_light;
    }

    INLINE_XPU void shadePixel(const Scene &scene, const RayTracerSettings &settings, Color &color) {
        Color current_color;
        Color throughput = 1.0f;
        u32 depth_left = settings.max_depth;
        ray.depth = scene_tracer.aux_ray.depth = 1;

        while (depth_left) {
            current_color = Black;

            for (u32 i = 0; i < scene.counts.lights; i++)
                surface.shadeFromLight(scene.lights[i], scene, scene_tracer, current_color);

            if (scene.flags & SCENE_HAD_EMISSIVE_QUADS &&
                surface.geometry != (scene.geometries + 4))
                surface.shadeFromEmissiveQuads(scene, current_color);

            if (settings.skybox_irradiance_texture_id >= 0 &&
                settings.skybox_radiance_texture_id >= 0) {
                surface.L = surface.N;
                surface.NdotL = 1.0f;
                Color D{scene.textures[settings.skybox_irradiance_texture_id].sampleCube(surface.N.x,surface.N.y,surface.N.z).color};
                Color S{scene.textures[settings.skybox_radiance_texture_id  ].sampleCube(surface.R.x,surface.R.y,surface.R.z).color};
                surface.radianceFraction();
                current_color = D.mulAdd(surface.Fd, surface.Fs.mulAdd(S, current_color));
            }

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                shadeLights(scene.lights, scene.counts.lights, ray.origin, ray.direction, hit.distance, color);

            if ((surface.material->isReflective() ||
                 surface.material->isRefractive()) &&
                --depth_left) {
                ray.depth++;
                scene_tracer.aux_ray.depth++;

//                surface.H = (surface.R + surface.V).normalized();
//                surface.F = schlickFresnel(clampedValue(surface.H.dot(surface.R)), surface.material->reflectivity);
                surface.F = schlickFresnel(clampedValue(surface.N.dot(surface.R)), surface.material->reflectivity);
                throughput *= surface.refracted ? (1.0f - surface.F) : surface.F;

                ray.origin = hit.position;
                ray.direction = surface.RF;
                surface.geometry = scene_tracer.trace(ray, hit, scene);
                if (surface.geometry) {
                    surface.prepareForShading(ray, hit, scene.materials, scene.textures);
                    if (surface.geometry->type == GeometryType_Quad && surface.material->isEmissive()) {
                        color = hit.from_behind ? Black : surface.material->emission;
                        break;
                    }

                    continue;
                } else if (settings.skybox_color_texture_id >= 0)
                    color = scene.textures[settings.skybox_color_texture_id].sampleCube(ray.direction.x,ray.direction.y,ray.direction.z).color;
            }

            break;
        }
    }
};