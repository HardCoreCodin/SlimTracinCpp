#pragma once

#include "../../core/texture.h"
#include "../tracers/scene.h"
#include "../common.h"
#include "./lights.h"
//
//INLINE bool shadeFromEmissiveQuads(Shaded &shaded, Ray &ray, SceneTracer &scene_tracer, Color &color) {
//    Ray &local_ray = scene_tracer.local_ray;
//    RayHit &local_hit = scene_tracer.local_hit;
//    vec3 &Rd = local_ray.direction;
//    vec3 &Ro = local_ray.origin;
//    vec3 emissive_quad_normal;
//    bool found = false;
//
//    for (u32 i = 0; i < scene_tracer.scene.counts.geometries; i++) {
//        Geometry &quad = scene_tracer.scene.geometries[i];
//        Material &emissive_material = scene_tracer.scene.materials[quad.material_id];
//        if (quad.type != GeometryType_Quad || !(emissive_material.isEmissive()))
//            continue;
//
//        quad.transform.internPosAndDir(shaded.viewing_origin, shaded.viewing_direction, Ro, Rd);
//        if (local_ray.hitsDefaultQuad(shaded, quad.flags & GEOMETRY_IS_TRANSPARENT)) {
//            shaded.position = quad.transform.externPos(shaded.position);
//            f32 t2 = (shaded.position - shaded.viewing_origin).squaredLength();
//            if (t2 < shaded.distance*shaded.distance) {
//                local_hit.distance = sqrtf(t2);
//                local_hit.id = i;
////                local_hit.geo_type = GeometryType_Quad;
////                local_hit.material_id = quad.material_id;
//                *((RayHit*)&shaded) = local_hit;
//                found = true;
//            }
//        }
//
//        emissive_quad_normal.x = emissive_quad_normal.z = 0;
//        emissive_quad_normal.y = 1;
//        emissive_quad_normal = quad.transform.rotation * emissive_quad_normal;
//        shaded.light_direction = ray.direction = quad.transform.position - shaded.position;
//        if (emissive_quad_normal.dot(shaded.light_direction) >= 0)
//            continue;
//
//        f32 emission_intensity = shaded.normal.dot(getAreaLightVector(quad.transform, shaded.position, shaded.emissive_quad_vertices));
//        if (emission_intensity > 0) {
//            bool skip = true;
//            for (u8 j = 0; j < 4; j++) {
//                if (shaded.normal.dot(shaded.emissive_quad_vertices[j] - shaded.position) >= 0) {
//                    skip = false;
//                    break;
//                }
//            }
//            if (skip)
//                continue;
//
//            f32 shaded_light = 1;
//            for (u32 s = 0; s < scene_tracer.scene.counts.geometries; s++) {
//                Geometry &shadowing_primitive = scene_tracer.scene.geometries[s];
//                if (&quad == shaded.geometry ||
//                    &quad == &shadowing_primitive ||
//                    emissive_quad_normal.dot(shadowing_primitive.transform.position - quad.transform.position) <= 0)
//                    continue;
//
//                shadowing_primitive.transform.internPosAndDir(shaded.position, shaded.light_direction, Ro, Rd);
//                Ro = Rd.scaleAdd(TRACE_OFFSET, Ro);
//
//                f32 d = 1;
//                if (shadowing_primitive.type == GeometryType_Sphere) {
//                    if (scene_tracer.local_ray.hitsDefaultSphere(local_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT))
//                        d -= (1.0f - local_hit.distance) / (local_hit.distance * emission_intensity * 3);
//                } else if (shadowing_primitive.type == GeometryType_Quad) {
//                    if (local_ray.hitsDefaultQuad(local_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT)) {
//                        local_hit.position.y = 0;
//                        local_hit.position.x = local_hit.position.x < 0 ? -local_hit.position.x : local_hit.position.x;
//                        local_hit.position.z = local_hit.position.z < 0 ? -local_hit.position.z : local_hit.position.z;
//                        if (local_hit.position.x > local_hit.position.z) {
//                            local_hit.position.y = local_hit.position.z;
//                            local_hit.position.z = local_hit.position.x;
//                            local_hit.position.x = local_hit.position.y;
//                            local_hit.position.y = 0;
//                        }
//                        d -= (1.0f - local_hit.position.z) / (local_hit.distance * emission_intensity);
//                    }
//                }
//                if (d < shaded_light)
//                    shaded_light = d;
//            }
//            if (shaded_light > 0 && shaded.updateNdotL())
//                shaded.radianceFraction().mulAdd(emissive_material.emission * (emission_intensity * shaded_light), color);
//        }
//    }
//
//    return found;
//}