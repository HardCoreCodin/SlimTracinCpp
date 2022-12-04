#pragma once

#include "../intersection/scene.h"
#include "../common.h"
#include "../../scene/scene.h"


INLINE vec3 shadeReflection(Ray &ray, Trace &trace, Scene &scene) {
    RayHit *hit = &trace->closest_hit;
    Light *light;
    f32 exp, light_distance, light_distance_squared, NdotL;
    vec3 diffuse, specular, hit_position, N, L, H, light_radiance, radiance, half_vector,
         current_color, color, throughput{1.0f}, Rd = ray->direction;

    u32 depth = trace->depth;
    while (depth) {
        hit_position = ray->origin = hit->position;
        N = hit->normal;
        Material &material = scene->materials[hit->material_id];
        exp = 16.0f   * material.metallic;
        diffuse = material.albedo;
        specular = material.reflectivity;

        current_color = 0.0f;
        light = scene->lights;
        for (u32 i = 0; i < scene->counts.lights; i++, light++) {
            if (light->is_directional) {
                light_distance = light_distance_squared = INFINITY;
                L = -light->position_or_direction;
            } else {
                L = light->position_or_direction - hit_position;
                light_distance_squared = L.squaredLength();
                light_distance = sqrtf(light_distance_squared);
                L /= light_distance;
            }

            NdotL = N.dot(L);
            if (NdotL <= 0)
                continue;

            NdotL = clampedValue(NdotL);
            ray->direction = L;
            trace->closest_hit.distance = light_distance;
            trace->closest_hit.distance_squared = light_distance_squared;
            if (!inShadow(ray, trace, scene)) {
                H = (L - Rd).normalized();
                specular = material.reflectivity * powf(N.dot(H), exp);
                radiance = diffuse.scaleAdd(NdotL, specular);
                light_radiance = light->color * (light->intensity / light_distance_squared);
                current_color = radiance.mulAdd(light_radiance, current_color);
            }
        }

        color = current_color.mulAdd(throughput, color);

        if (--depth) {
            ray->direction = Rd = Rd.reflectedAround(N);
            ray->origin    = hit_position;
            if (traceRay(ray, trace, scene)) {
                throughput *= specular;
                continue;
            }
        }

        break;
    }

    return color;
}