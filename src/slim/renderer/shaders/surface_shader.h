#pragma once

#include "../../core/ray.h"
#include "../../core/texture.h"
#include "../../scene/material.h"
#include "../tracers/scene_tracer.h"
#include "./light_shader.h"


INLINE_XPU Color sample(const Material &material, u8 slot, const Texture *textures, vec2 uv, f32 uv_coverage) {
    return (textures && material.texture_count > slot) ? textures[material.texture_ids[slot]].sample(uv.u, uv.v, uv_coverage).color : Black;
}

INLINE_XPU Color sample(const Texture &texture, const RayHit &hit) {
    return texture.sample(hit.uv.u, hit.uv.v, hit.uv_coverage).color;
}

INLINE_XPU Color sample(Material &material, const RayHit &hit, u8 texture_slot, const Texture *textures) {
    return sample(material, texture_slot, textures, hit.uv, hit.uv_coverage);
}

INLINE_XPU Color sampleAlbedo(Material &material, const RayHit &hit, const Texture *textures) {
    return sample(material, hit, 0, textures);
}

INLINE_XPU Color sampleNormal(Material &material, const RayHit &hit, const Texture *textures) {
    return sample(material, hit, 1, textures);
}

INLINE_XPU vec3 sampleNormal(Material &material, const RayHit &hit, const Texture *textures, const vec3 &normal) {
    return rotateNormal(normal, sampleNormal(material, hit, textures), material.normal_magnitude);
}

struct SurfaceShader {
    Geometry *geometry = nullptr;
    Material *material = nullptr;
    Color F, albedo_from_map, Fs, Fd;
    vec3 P, N, V, L, R, RF, H, emissive_quad_vertices[4];
    f32 Ld, Ld2, NdotL, NdotV, NdotH, HdotL, IOR;

    bool refracted = false;

    INLINE_XPU void shadeFromLight(const Light &light, const Scene &scene, SceneTracer &tracer, Color &color) {
        if (isFacingLight(light) && (
                !(light.flags & Light_IsShadowing) ||
                !tracer.inShadow(scene, P, L, Ld)
            )
        ) {
            // color += fr(p, L, V) * Li(p, L) * cos(w)
            radianceFraction();
            color = (Fs + Fd).mulAdd(light.color * (NdotL * light.intensity / Ld2), color);
        }
    }

    INLINE_XPU void prepareForShading(Ray &ray, RayHit &hit, Material *materials, const Texture *textures) {
        material = materials + geometry->material_id;
        if (material->hasNormalMap())
            hit.normal = rotateNormal(
                    hit.normal,
                    sampleNormal(*material, hit, textures),
                    material->normal_magnitude
        );

        P = hit.position;
        N = hit.normal;
        R = ray.direction.reflectedAround(N);
        V = -ray.direction;
        NdotV = clampedValue(N.dot(V));
        albedo_from_map = material->hasAlbedoMap() ? sampleAlbedo(*material, hit, textures) : White;

        refracted = material->isRefractive();
        if (refracted) {
            IOR = hit.from_behind ? (material->IOR / IOR_AIR) : (IOR_AIR / material->IOR);
            f32 c = IOR*IOR * (1.0f - (NdotV * NdotV));
            refracted = c < 1.0f;
            RF = refracted ? N.scaleAdd(IOR * NdotV - sqrtf(1 - c), ray.direction * IOR).normalized() : R;
        }
    }

    INLINE_XPU void radianceFraction() {
        Fs = Black;
        Fd = material->albedo * albedo_from_map;
        if (material->brdf == BRDF_CookTorrance) {
            Fd *= (1.0f - material->metalness) * ONE_OVER_PI;

            if (NdotV > 0.0f) { // TODO: This should not be necessary to check for, because rays would miss in that scenario so the code should never even get to this point - and yet it seems like it does.
                // If the viewing direction is perpendicular to the normal, no light can reflect
                // Both the numerator and denominator would evaluate to 0 in this case, resulting in NaN

                // If roughness is 0 then the NDF (and hence the entire brdf) evaluates to 0
                // Otherwise, a negative roughness makes no sense logically and would be a user-error
                if (material->roughness > 0.0f) {
                    H = (L + V).normalized();
                    NdotH = clampedValue(N.dot(H));
                    HdotL = clampedValue(H.dot(L));
                    Fs = cookTorrance(material->roughness, NdotL, NdotV, HdotL, NdotH, material->reflectivity, F);
                    Fd *= 1.0f - F;
                }
            }
        } else {
            Fd *= material->roughness * ONE_OVER_PI;

            if (material->brdf != BRDF_Lambert) {
                f32 specular_factor, exponent;
                if (material->brdf == BRDF_Phong) {
                    exponent = 4.0f;
                    specular_factor = clampedValue(R.dot(L));
                } else {
                    exponent = 16.0f;
                    specular_factor = clampedValue(N.dot((L + V).normalized()));;
                }
                if (specular_factor > 0.0f)
                    Fs = material->reflectivity * (powf(specular_factor, exponent) * (1.0f - material->roughness));
            }
        }
    }

    INLINE_XPU bool isFacingLight(const Light &light) {
        if (light.flags & Light_IsDirectional) {
            Ld = INFINITY;
            Ld2 = 1.0f;
            L = -light.position_or_direction;
        } else {
            L = light.position_or_direction - P;
            Ld2 = L.squaredLength();
            Ld = sqrtf(Ld2);
            L /= Ld;
        }
        NdotL = clampedValue(L.dot(N));
        return NdotL > 0.0f;
    }

    INLINE_XPU bool shadeFromEmissiveQuads(const Scene &scene, Ray &shadow_ray, RayHit &shadow_hit, Color &color) {
        bool found = false;

        vec3 *v = emissive_quad_vertices;
        vec3 Ro;
        f32 Ld_rcp, sphere_squared_distance_To_center;

//        if (geometry->type != GeometryType_Sphere) return false;

        Transform *xform;
        Geometry *shadowing_geo, *emissive_quad = scene.geometries;
        for (u32 i = 0; i < scene.counts.geometries; i++, emissive_quad++) {
            if (emissive_quad == geometry ||
                emissive_quad->type != GeometryType_Quad ||
                !(scene.materials[emissive_quad->material_id].isEmissive()))
                continue;

            xform = &emissive_quad->transform;
            L = emissive_quad->transform.position - P;
            NdotL = N.dot(L);
            if (NdotL <= 0.0f)// || L.dot(xform->orientation * vec3{0.0f, -1.0f, 1.0f}) <= 0.0f)
                continue;

            Ld2 = L.squaredLength();
            Ld = sqrt(Ld2);
            Ld_rcp = 1.0f / Ld;
            L *= Ld_rcp;
            NdotL *= Ld_rcp;
            Ro = L.scaleAdd(TRACE_OFFSET, P);
            shadow_ray.localize(Ro, L, *xform);
            shadow_hit.distance = INFINITY;
            shadow_ray.direction = shadow_ray.direction.normalized();
            if (shadow_ray.hitsDefaultQuad(shadow_hit, emissive_quad->flags & GEOMETRY_IS_TRANSPARENT))
                found = true;

            f32 emission_intensity = N.dot(getAreaLightVector(*xform, P, v));
            if (emission_intensity > 0) {
                bool skip = true;
                for (u8 j = 0; j < 4; j++) {
                    if (N.dot(v[j] - P) >= 0) {
                        skip = false;
                        break;
                    }
                }
                if (skip)
                    continue;

                f32 shaded_light = 1.0f;
                shadowing_geo = scene.geometries;
                for (u32 s = 0; s < scene.counts.geometries; s++, shadowing_geo++) {
                    if (shadowing_geo == emissive_quad||
                        shadowing_geo == geometry)
                        continue;

                    shadow_ray.localize(Ro, L, shadowing_geo->transform);
                    shadow_hit.distance = INFINITY;
                    shadow_ray.direction = shadow_ray.direction.normalized();
                    f32 d = 1.0f;
                    if (shadowing_geo->type == GeometryType_Sphere) {
                        if (shadow_ray.hitsDefaultSphere(shadow_hit, shadowing_geo->flags & GEOMETRY_IS_TRANSPARENT, &sphere_squared_distance_To_center)) {
                            d -= (1.0f - sqrtf(sphere_squared_distance_To_center)) /
                                 (shadow_hit.distance * emission_intensity * 3.0f);
                        }
                    } else if (shadowing_geo->type == GeometryType_Quad) {
                        if (shadow_ray.hitsDefaultQuad(shadow_hit, shadowing_geo->flags & GEOMETRY_IS_TRANSPARENT))
                            d -= 3.0f * (1.0f - Max(abs(shadow_hit.position.x), abs(shadow_hit.position.z))) /
                                (shadow_hit.distance * emission_intensity);
                    }
                    shaded_light = Min(shaded_light, d);
                }

                if (shaded_light > 0.0f) {
                    radianceFraction();
                    color = (Fd + Fs).mulAdd(scene.materials[emissive_quad->material_id].emission * (emission_intensity * shaded_light * 7.0f), color);
                }
            }
        }

        return found;
    }
};
