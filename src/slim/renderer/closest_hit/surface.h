#pragma once

#include "../../core/texture.h"
#include "../tracers/scene.h"
#include "../common.h"
#include "./lights.h"

INLINE bool shadeFromEmissiveQuads(Shaded &shaded, Ray &ray, SceneTracer &scene_tracer, Color &color) {
    Ray &local_ray = scene_tracer.local_ray;
    RayHit &local_hit = scene_tracer.local_hit;
    vec3 &Rd = local_ray.direction;
    vec3 &Ro = local_ray.origin;
    vec3 emissive_quad_normal;
    bool found = false;

    for (u32 i = 0; i < scene_tracer.scene.counts.geometries; i++) {
        Geometry &quad = scene_tracer.scene.geometries[i];
        Material &emissive_material = scene_tracer.scene.materials[quad.material_id];
        if (quad.type != GeometryType_Quad || !(emissive_material.isEmissive()))
            continue;

        quad.transform.internPosAndDir(shaded.viewing_origin, shaded.viewing_direction, Ro, Rd);
        if (local_ray.hitsDefaultQuad(quad.flags, shaded)) {
            shaded.position = quad.transform.externPos(shaded.position);
            shaded.distance_squared = (shaded.position - shaded.viewing_origin).squaredLength();
            if (local_hit.distance_squared < shaded.distance_squared) {
                local_hit.geo_id = i;
                local_hit.geo_type = GeometryType_Quad;
                local_hit.material_id = quad.material_id;
                *((RayHit*)&shaded) = local_hit;
                found = true;
            }
        }

        emissive_quad_normal.x = emissive_quad_normal.z = 0;
        emissive_quad_normal.y = 1;
        emissive_quad_normal = quad.transform.rotation * emissive_quad_normal;
        shaded.light_direction = ray.direction = quad.transform.position - shaded.position;
        if (emissive_quad_normal.dot(shaded.light_direction) >= 0)
            continue;

        f32 emission_intensity = shaded.normal.dot(getAreaLightVector(quad.transform, shaded.position, shaded.emissive_quad_vertices));
        if (emission_intensity > 0) {
            bool skip = true;
            for (u8 j = 0; j < 4; j++) {
                if (shaded.normal.dot(shaded.emissive_quad_vertices[j] - shaded.position) >= 0) {
                    skip = false;
                    break;
                }
            }
            if (skip)
                continue;

            f32 shaded_light = 1;
            for (u32 s = 0; s < scene_tracer.scene.counts.geometries; s++) {
                Geometry &shadowing_primitive = scene_tracer.scene.geometries[s];
                if (&quad == shaded.geometry ||
                    &quad == &shadowing_primitive ||
                    emissive_quad_normal.dot(shadowing_primitive.transform.position - quad.transform.position) <= 0)
                    continue;

                shadowing_primitive.transform.internPosAndDir(shaded.position, shaded.light_direction, Ro, Rd);
                Ro = Rd.scaleAdd(TRACE_OFFSET, Ro);

                f32 d = 1;
                if (shadowing_primitive.type == GeometryType_Sphere) {
                    if (scene_tracer.local_ray.hitsDefaultSphere(shadowing_primitive.flags, local_hit))
                        d -= (1.0f - sqrtf(local_hit.distance_squared)) / (local_hit.distance * emission_intensity * 3);
                } else if (shadowing_primitive.type == GeometryType_Quad) {
                    if (local_ray.hitsDefaultQuad(shadowing_primitive.flags, local_hit)) {
                        local_hit.position.y = 0;
                        local_hit.position.x = local_hit.position.x < 0 ? -local_hit.position.x : local_hit.position.x;
                        local_hit.position.z = local_hit.position.z < 0 ? -local_hit.position.z : local_hit.position.z;
                        if (local_hit.position.x > local_hit.position.z) {
                            local_hit.position.y = local_hit.position.z;
                            local_hit.position.z = local_hit.position.x;
                            local_hit.position.x = local_hit.position.y;
                            local_hit.position.y = 0;
                        }
                        d -= (1.0f - local_hit.position.z) / (local_hit.distance * emission_intensity);
                    }
                }
                if (d < shaded_light)
                    shaded_light = d;
            }
            if (shaded_light > 0 && shaded.updateNdotL())
                shaded.radianceFraction().mulAdd(emissive_material.emission * (emission_intensity * shaded_light), color);
        }
    }

    if (found)
        shaded.distance = sqrtf(shaded.distance_squared);

    return found;
}

Color shadeSurface(Ray &ray, SceneTracer &scene_tracer, LightsShader &lights_shader, u32 max_depth, Shaded &shaded) {
    const Scene &scene = scene_tracer.scene;
    f32 max_distance = shaded.distance;

    Color current_color, color;
    Color throughput = 1.0f;
    u32 depth = max_depth;
    while (depth) {
        bool is_ref = shaded.material->isReflective() ||
                      shaded.material->isRefractive();
        current_color = is_ref ? Black : scene.ambient_light.color;
        if (scene.lights) {
            for (u32 i = 0; i < scene_tracer.scene.counts.lights; i++) {
                const Light &light = scene_tracer.scene.lights[i];
                if (shaded.isFacing(light) &&
                    !scene_tracer.inShadow(shaded.position, shaded.light_direction))
                    color = ((light.intensity / shaded.light_distance_squared) * light.color).mulAdd(shaded.radianceFraction(), color);
            }
        }

        if (scene_tracer.scene_has_emissive_quads &&
            shadeFromEmissiveQuads(shaded, ray, scene_tracer, current_color))
            max_distance = shaded.distance;

        color = current_color.mulAdd(throughput, color);
        if (scene.lights)
            lights_shader.shadeLights(ray.origin, ray.direction, max_distance, color);

        if (is_ref && --depth) {
            ray.origin = shaded.position;
            ray.direction = shaded.reflected_direction;
            if (shaded.material->isRefractive()) {
                ray.direction = shaded.refractedDirection();
                if (!ray.direction.nonZero())
                    ray.direction = shaded.reflected_direction;
            }
            if (scene_tracer.trace(ray, shaded)) {
                shaded.reset(ray, scene.geometries, scene.materials, scene.textures, scene_tracer.pixel_area_over_focal_length_squared);

                if (shaded.geo_type == GeometryType_Quad && shaded.material->isEmissive()) {
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