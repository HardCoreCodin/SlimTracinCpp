#pragma once

#include "../intersection/scene.h"
#include "../common.h"
#include "../../scene/scene.h"


struct ClassicShader {
    const Color &shaded_position;
    const Color &shaded_normal;
    const vec3 &diffuse;
    const vec3 &specular;

    vec3 R, H, radiance;
    vec3 light_direction, light_radiance;
    f32 light_distance, light_distance_squared, NdotL;

    ClassicShader(const Material &material, const RayHit &hit) :
        shaded_normal{hit.normal},
        shaded_position{hit.position},
        diffuse{material.albedo},
        specular{material.reflectivity} {}

    ClassicShader(const Scene &scene, const RayHit &hit) :
        ClassicShader(scene.materials[scene.geometries[hit.geo_id].material_id], hit) {}



    bool isLit() {
        ray.direction = L;
        trace.closest_hit.distance = light_distance;
        trace.closest_hit.distance_squared = light_distance_squared;
        if (!inShadow(ray, trace, scene))
    }
};

INLINE_XPU vec3 shadeLambert(Ray &ray, Trace &trace, Scene &scene) {
    vec3 diffuse = scene.materials[scene.geometries[trace.closest_hit.geo_id].material_id].albedo;
    vec3 hit_position = ray.origin = trace.closest_hit.position;
    vec3 N = trace.closest_hit.normal;
    vec3 L, light_radiance, radiance;
    f32 light_distance, light_distance_squared, NdotL;

    vec3 color = scene.ambient_light.color;
    for (u32 i = 0; i < scene.counts.lights; i++) {
        const Light &light = scene.lights[i];

        NdotL = N.dot(L);
        if (NdotL <= 0)
            continue;

        ray.direction = L;
        trace.closest_hit.distance = light_distance;
        trace.closest_hit.distance_squared = light_distance_squared;
        if (!inShadow(ray, trace, scene)) {
            light_radiance = light.color * (light.intensity / light_distance_squared);
            radiance = diffuse * NdotL;
            color = radiance.mulAdd(light_radiance, color);
        }
    }

    return color;
}

INLINE_XPU vec3 shadePhong(Ray &ray, Trace &trace, Scene &scene) {
    Material &material = scene.materials[trace.closest_hit.material_id];
    f32 exp = 4.0f   * material.metallic;
    vec3 diffuse     = material.albedo;
    vec3 specular    = material.reflectivity;
    vec3 hit_position = ray.origin = trace.closest_hit.position;
    vec3 N = trace.closest_hit.normal;
    vec3 R = reflectWithDot(ray.direction, N, -invDotVec3(N, ray.direction));
    vec3 L, light_radiance, radiance;
    f32 light_distance, light_distance_squared, NdotL;

    vec3 color = scene.ambient_light.color;
    for (u32 i = 0; i < scene.counts.lights; i++) {
        const Light &light = scene.lights[i];
        if (light.is_directional) {
            light_distance = light_distance_squared = INFINITY;
            L = -light.position_or_direction;
        } else {
            L = light.position_or_direction - hit_position;
            light_distance_squared = L.squaredLength();
            light_distance = sqrtf(light_distance_squared);
            L /= light_distance;
        }

        NdotL = N.dot(L);
        if (NdotL <= 0)
            continue;

        NdotL = clampedValue(NdotL);
        ray.direction = L;
        trace.closest_hit.distance = light_distance;
        trace.closest_hit.distance_squared = light_distance_squared;
        if (!inShadow(ray, trace, scene)) {
            specular = material.reflectivity * powf(R.dot(L), exp);
            radiance = diffuse.scaleAdd(NdotL, specular);
            light_radiance = light.color * (light.intensity / light_distance_squared);
            color = radiance.mulAdd(light_radiance, color);
        }
    }

    return color;
}

INLINE_XPU vec3 shadeBlinn(Ray &ray, Trace &trace, Scene &scene) {
    Material &material = scene.materials[trace.closest_hit.material_id];
    f32 exp = 16.0f   * material.metallic;
    vec3 diffuse     = material.albedo;
    vec3 specular    = material.reflectivity;
    vec3 hit_position = ray.origin = trace.closest_hit.position;
    vec3 V = -ray.direction;
    vec3 N = trace.closest_hit.normal;
    vec3 L, H, light_radiance, radiance;
    f32 light_distance, light_distance_squared, NdotL;

    vec3 color = scene.ambient_light.color;
    for (u32 i = 0; i < scene.counts.lights; i++) {
        const Light &light = scene.lights[i];
        if (light.is_directional) {
            light_distance = light_distance_squared = INFINITY;
            L = -light.position_or_direction;
        } else {
            L = light.position_or_direction - hit_position;
            light_distance_squared = L.squaredLength();
            light_distance = sqrtf(light_distance_squared);
            L /= light_distance;
        }

        NdotL = N.dot(L);
        if (NdotL <= 0)
            continue;

        NdotL = clampedValue(NdotL);
        ray.direction = L;
        trace.closest_hit.distance = light_distance;
        trace.closest_hit.distance_squared = light_distance_squared;
        if (!inShadow(ray, trace, scene)) {
            H = (L + V).normalized();
            specular = material.reflectivity * powf(N.dot(H), exp);
            radiance = diffuse.scaleAdd(NdotL, specular);
            light_radiance = light.color * (light.intensity / light_distance_squared);
            color = radiance.mulAdd(light_radiance, color);
        }
    }

    return color;
}