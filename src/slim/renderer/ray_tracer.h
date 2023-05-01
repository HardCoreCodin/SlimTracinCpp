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

INLINE_XPU void renderPixelBeauty(
    const RayTracerSettings &settings,
    const CameraRayProjection &projection,
    Scene &scene,
    SceneTracer &scene_tracer,
    SurfaceShader &surface,
    Ray &ray,
    RayHit &hit,

    const vec3 &direction,

    Color &color,
    f32 &depth
) {
    color = Black;
    depth = INFINITY;

    ray.reset(projection.camera_position, direction.normalized());

    Color current_color, next_throughput, throughput = 1.0f;
    u32 depth_left = settings.max_depth;
    ray.depth = scene_tracer.aux_ray.depth = 1;

    while (depth_left) {
        current_color = Black;

        surface.geometry = scene_tracer.trace(ray, hit, scene);
        if (surface.geometry) { // Hit:
            surface.material = scene.materials + surface.geometry->material_id;
            if (surface.material->isEmissive() && surface.geometry->type == GeometryType_Quad && !hit.from_behind) {
                depth_left = 0;
                current_color = surface.material->emission;
            } else {
                surface.prepareForShading(ray, hit, scene.materials, scene.textures);
                if (depth_left == settings.max_depth) depth = projection.getDepthAt(hit.position);

                // Point / Directional lights:
                if (scene.lights)
                    for (u32 i = 0; i < scene.counts.lights; i++)
                        surface.shadeFromLight(scene.lights[i], scene, scene_tracer, current_color);

                // Area Lights:
                if (scene.flags & SCENE_HAD_EMISSIVE_QUADS)
                    surface.shadeFromEmissiveQuads(scene, current_color);

                // Image Based Lighting:
                if (settings.skybox_irradiance_texture_id >= 0 &&
                    settings.skybox_radiance_texture_id >= 0) {
                    surface.L = surface.N;
                    surface.NdotL = 1.0f;
                    Color D{scene.textures[settings.skybox_irradiance_texture_id].sampleCube(surface.N.x,surface.N.y,surface.N.z).color};
                    Color S{scene.textures[settings.skybox_radiance_texture_id  ].sampleCube(surface.R.x,surface.R.y,surface.R.z).color};
                    surface.radianceFraction();
                    current_color = D.mulAdd(surface.Fd, surface.Fs.mulAdd(S, current_color));
                }

                if ((surface.material->isReflective() ||
                     surface.material->isRefractive()) &&
                    --depth_left) {
                    ray.depth++;
                    scene_tracer.aux_ray.depth++;

//                          surface.H = (surface.R + surface.V).normalized();
//                          surface.F = schlickFresnel(clampedValue(surface.H.dot(surface.R)), surface.material->reflectivity);
                    surface.F = schlickFresnel(clampedValue(surface.N.dot(surface.R)), surface.material->reflectivity);
                    next_throughput = surface.refracted ? (1.0f - surface.F) : surface.F;
                    ray.reset(hit.position, surface.RF);
                } else depth_left = 0;
            }
        } else { // Miss:
            depth_left = 0;
            if (settings.skybox_color_texture_id >= 0)
                current_color = scene.textures[settings.skybox_color_texture_id].sampleCube(
                    ray.direction.x,
                    ray.direction.y,
                    ray.direction.z
                ).color;
        }

        if (scene.lights)
            for (u32 i = 0; i < scene.counts.lights; i++)
                if (scene_tracer.hitLight(scene.lights[i], ray, hit))
                    current_color = scene.lights[i].color.scaleAdd(pow(scene_tracer.light_tracer.integrateDensity(), 8.0f) * 4, current_color);

        color = current_color.mulAdd(throughput, color);
        throughput *= next_throughput;
    }

    color.applyToneMapping();
}

INLINE_XPU void renderPixelDebugMode(
    const RayTracerSettings &settings,
    const CameraRayProjection &projection,
    Scene &scene,
    SceneTracer &scene_tracer,
    SurfaceShader &surface,
    Ray &ray,
    RayHit &hit,

    const vec3 &direction,

    Color &color,
    f32 &depth
) {
    color = Black;
    depth = INFINITY;

    ray.reset(projection.camera_position, direction.normalized());

    surface.geometry = scene_tracer.trace(ray, hit, scene);
    if (surface.geometry) {
        surface.prepareForShading(ray, hit, scene.materials, scene.textures);
        depth = projection.getDepthAt(hit.position);
        switch (settings.render_mode) {
            case RenderMode_UVs      : color = getColorByUV(hit.uv); break;
            case RenderMode_Depth    : color = getColorByDistance(hit.distance); break;
            case RenderMode_Normals  : color = directionToColor(hit.normal);  break;
            case RenderMode_NormalMap: color = directionToColor(sampleNormal(*surface.material, hit, scene.textures));  break;
            case RenderMode_MipLevel : color = scene.counts.textures ? settings.mip_level_colors[scene.textures[0].mipLevel(hit.uv_coverage)] : Grey;
            default: break;
        }
    }
}

INLINE_XPU void renderPixel(
    const RayTracerSettings &settings,
    const CameraRayProjection &projection,
    Scene &scene,
    SceneTracer &scene_tracer,
    SurfaceShader &surface,
    Ray &ray,
    RayHit &hit,

    const vec3 &direction,

    Color &color,
    f32 &depth
) {
    if (settings.render_mode == RenderMode_Beauty)
        renderPixelBeauty(settings, projection, scene, scene_tracer, surface, ray, hit, direction, color, depth);
    else
        renderPixelDebugMode(settings, projection, scene, scene_tracer, surface, ray, hit, direction, color, depth);
}