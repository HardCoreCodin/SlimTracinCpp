#pragma once

#include "../trace.h"
#include "../common.h"
#include "../intersection/sphere.h"
#include "../../core/texture.h"
#include "../../scene/scene.h"
#include "./lights.h"

INLINE vec3 shadeFromLights(Shaded *shaded, Ray *ray, Trace *trace, Scene *scene, vec3 color) {
    RayHit *hit = &trace->closest_hit;
    Light *light = scene->lights;
    f32 light_intensity, NdotL;
    for (u32 i = 0; i < scene->counts.lights; i++, light++) {
        hit->position = light->position_or_direction;
        shaded->light_direction = hit->position - shaded->position;
        hit->distance_squared = shaded->light_direction.squaredLength();
        hit->distance = sqrtf(hit->distance_squared);
        shaded->light_direction = shaded->light_direction / hit->distance;

        NdotL = shaded->normal.dot(shaded->light_direction);
        if (NdotL <= 0)
            continue;

        NdotL = clampedValue(NdotL);
        light_intensity = light->intensity / hit->distance_squared;

        ray->origin    = shaded->position;
        ray->direction = shaded->light_direction;
        if (inShadow(ray, trace, scene))
            continue;

        vec3 radiance = shadePointOnSurface(shaded, NdotL);
        color = radiance.mulAdd(light->color * light_intensity, color);
    }

    return color;
}

INLINE bool shadeFromEmissiveQuads(Shaded *shaded, Ray *ray, Trace *trace, Scene *scene, vec3 *color) {
    RayHit *hit = &trace->current_hit;
    Geometry *quad = scene->geometries;
    Material *emissive_material;
    vec3 emissive_quad_normal;
    vec3 &Rd = trace->local_space_ray.direction;
    vec3 &Ro = trace->local_space_ray.origin;
    bool found = false;

    for (u32 i = 0; i < scene->counts.geometries; i++, quad++) {
        emissive_material = scene->materials + quad->material_id;
        if (quad->type != GeometryType_Quad || !(emissive_material->isEmissive()))
            continue;

        quad->transform.internPosAndDir(shaded->viewing_origin, shaded->viewing_direction, Ro, Rd);
        if (hitQuad(hit, &Ro, &Rd, quad->flags)) {
            hit->position = quad->transform.externPos(hit->position);
            hit->distance_squared = (hit->position - shaded->viewing_origin).squaredLength();
            if (hit->distance_squared < trace->closest_hit.distance_squared) {
                hit->geo_id = i;
                hit->geo_type = GeometryType_Quad;
                hit->material_id = quad->material_id;
                trace->closest_hit = *hit;
                found = true;
            }
        }

        emissive_quad_normal.x = emissive_quad_normal.z = 0;
        emissive_quad_normal.y = 1;
        emissive_quad_normal = quad->transform.rotation * emissive_quad_normal;
        shaded->light_direction = ray->direction = quad->transform.position - shaded->position;
        if (emissive_quad_normal.dot(shaded->light_direction) >= 0)
            continue;

        f32 emission_intensity = shaded->normal.dot(getAreaLightVector(quad->transform, shaded->position, shaded->emissive_quad_vertices));
        if (emission_intensity > 0) {
            bool skip = true;
            for (u8 j = 0; j < 4; j++) {
                if (shaded->normal.dot(shaded->emissive_quad_vertices[j] - shaded->position) >= 0) {
                    skip = false;
                    break;
                }
            }
            if (skip)
                continue;

            f32 shaded_light = 1;
            Geometry *shadowing_primitive = scene->geometries;
            for (u32 s = 0; s < scene->counts.geometries; s++, shadowing_primitive++) {
                if (quad == shaded->geometry ||
                    quad == shadowing_primitive ||
                    emissive_quad_normal.dot(shadowing_primitive->transform.position - quad->transform.position) <= 0)
                    continue;

                shadowing_primitive->transform.internPosAndDir(shaded->position, shaded->light_direction, Ro, Rd);
                Ro = Rd.scaleAdd(TRACE_OFFSET, Ro);

                f32 d = 1;
                if (shadowing_primitive->type == GeometryType_Sphere) {
                    if (hitSphere(hit, &Ro, &Rd, shadowing_primitive->flags))
                        d -= (1.0f - sqrtf(hit->distance_squared)) / (hit->distance * emission_intensity * 3);
                } else if (shadowing_primitive->type == GeometryType_Quad) {
                    if (hitQuad(hit, &Ro, &Rd, shadowing_primitive->flags)) {
                        hit->position.y = 0;
                        hit->position.x = hit->position.x < 0 ? -hit->position.x : hit->position.x;
                        hit->position.z = hit->position.z < 0 ? -hit->position.z : hit->position.z;
                        if (hit->position.x > hit->position.z) {
                            hit->position.y = hit->position.z;
                            hit->position.z = hit->position.x;
                            hit->position.x = hit->position.y;
                            hit->position.y = 0;
                        }
                        d -= (1.0f - hit->position.z) / (hit->distance * emission_intensity);
                    }
                }
                if (d < shaded_light)
                    shaded_light = d;
            }
            if (shaded_light > 0) {
                f32 NdotL = shaded->normal.dot(shaded->light_direction);
                if (NdotL)
                    *color = shadePointOnSurface(shaded, NdotL).mulAdd(emissive_material->emission * (emission_intensity * shaded_light), *color);
            }
        }
    }

    if (found)
        trace->closest_hit.distance = sqrtf(trace->closest_hit.distance_squared);

    return found;
}

vec3 shadeSurface(Ray *ray, Trace *trace, Scene *scene, bool *lights_shaded) {
    RayHit *hit = &trace->closest_hit;
    vec3 color;
    Material *M = scene->materials  + hit->material_id;
    if (M->flags & MATERIAL_IS_EMISSIVE)
        return hit->from_behind ? color : M->emission;

    Shaded shaded;
    shaded.viewing_origin    = ray->origin;
    shaded.viewing_direction = ray->direction;
    shaded.geometry = scene->geometries + hit->geo_id;
    shaded.material = M;
    shaded.albedo = shaded.material->albedo;
    if (M->isTextured()) {
        Texture &diffuse_map = scene->textures[M->texture_ids[0]];
        Texture &normal_map  = scene->textures[M->texture_ids[1]];

        hit->uv_area /= M->uv_repeat.u / M->uv_repeat.v;
        shaded.uv = hit->uv * M->uv_repeat;
        shaded.uv_area = dUVbyRayCone(hit->NdotV, hit->cone_width, hit->area, hit->uv_area);

        if (M->hasDiffuseMap()) shaded.albedo = shaded.sample(diffuse_map).color;
        if (M->hasNormalMap()) hit->normal = shaded.applyNormalRotation(hit->normal, normal_map, M->normal_magnitude);
    }

    f32 NdotRd, ior, max_distance = hit->distance;

    bool scene_has_emissive_quads = false;
    for (u32 i = 0; i < scene->counts.geometries; i++)
        if (scene->geometries[i].type == GeometryType_Quad &&
            scene->materials[scene->geometries[i].material_id].flags & MATERIAL_IS_EMISSIVE) {
            scene_has_emissive_quads = true;
            break;
        }

    vec3 current_color, throughput{1};
    u32 depth = trace->depth;
    while (depth) {
        shaded.position = ray->origin = hit->position;
        shaded.normal = hit->from_behind ? -hit->normal : hit->normal;
        ior = hit->from_behind ? shaded.material->n2_over_n1 : shaded.material->n1_over_n2;

        bool is_ref = (M->flags & MATERIAL_IS_REFLECTIVE) || (M->flags & MATERIAL_IS_REFRACTIVE);
        if (is_ref || M->brdf == BRDF_Phong) {
            NdotRd = -invDotVec3(shaded.normal, shaded.viewing_direction);
            shaded.reflected_direction = reflectWithDot(shaded.viewing_direction, shaded.normal, NdotRd);
        }

        current_color = is_ref ? vec3{0.0f} : scene->ambient_light.color;
        if (scene->lights)
            current_color = shadeFromLights(&shaded, ray, trace, scene, current_color);

        if (scene_has_emissive_quads) {
            if (shadeFromEmissiveQuads(&shaded, ray, trace, scene, &current_color))
                max_distance = hit->distance;
        }

        color = current_color.mulAdd(throughput, color);

        if (scene->lights && shadeLights(scene->lights,
                                        scene->counts.lights,
                                        shaded.viewing_origin,
                                        shaded.viewing_direction,
                                        max_distance,
                                        &trace->sphere_hit,
                                        &color))
            *lights_shaded = true;

        if (is_ref && --depth) {
            if (M->flags & MATERIAL_IS_REFLECTIVE) {
                ray->direction = shaded.reflected_direction;
            } else {
                ray->direction = refract(shaded.viewing_direction, shaded.normal, ior, NdotRd);
                if (ray->direction.x == 0 &&
                    ray->direction.y == 0 &&
                    ray->direction.z == 0)
                    ray->direction = shaded.reflected_direction;
            }
            if (traceRay(ray, trace, scene)) {
                shaded.geometry = scene->geometries + hit->geo_id;
                shaded.material = M = scene->materials + hit->material_id;
                shaded.albedo = M->albedo;
                if (M->isTextured()) {
                    Texture &diffuse_map = scene->textures[M->texture_ids[0]];
                    Texture &normal_map  = scene->textures[M->texture_ids[1]];
                    hit->uv_area /= M->uv_repeat.u / M->uv_repeat.v;
                    shaded.uv = hit->uv * M->uv_repeat;
                    shaded.uv_area = dUVbyRayCone(hit->NdotV, hit->cone_width, hit->area, hit->uv_area);

                    if (M->hasDiffuseMap()) shaded.albedo = shaded.sample(diffuse_map).color;
                    if (M->hasNormalMap()) hit->normal = shaded.applyNormalRotation(hit->normal, normal_map, M->normal_magnitude);
                }

                if (hit->geo_type == GeometryType_Quad && M->isEmissive()) {
                    color = hit->from_behind ? vec3{0.0f} : M->emission;
                    break;
                }
                if (M->brdf != BRDF_CookTorrance)
                    throughput = throughput * M->reflectivity;

                shaded.viewing_origin    = ray->origin;
                shaded.viewing_direction = ray->direction;

                continue;
            }
        }

        break;
    }

    return color;
}