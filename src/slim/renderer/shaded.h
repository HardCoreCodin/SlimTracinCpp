#pragma once

#include "../core/ray.h"
#include "../core/texture.h"
#include "../scene/material.h"
#include "./tracers/scene.h"


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

struct Surface {
    RayHit hit;
    Geometry *geometry = nullptr;
    Material *material = nullptr;

    Color F, albedo_from_map;
    vec3 P, N, V, L, R, RF, H, emissive_quad_vertices[4];
    f32 Ld, Ld2, NdotL, NdotV, NdotH, HdotL, IOR;

    bool refracted = false;

    INLINE_XPU void shadeFromLight(const Light &light, SceneTracer &tracer, Color &color) {
        if (isFacingLight(light) &&
            !tracer.inShadow(P, L, Ld))
            // color += fr(p, L, V) * Li(p, L) * cos(w)
            color = radianceFraction().mulAdd(light.color * (NdotL * light.intensity / Ld2), color);
    }

    INLINE_XPU void prepareForShading(SceneTracer &tracer, Ray &ray) {
        material = tracer.scene.materials + geometry->material_id;
        if (material->hasNormalMap())
            hit.normal = rotateNormal(
                    hit.normal,
                    sampleNormal(*material, hit, tracer.scene.textures),
                    material->normal_magnitude
        );

        P = hit.position;
        N = hit.normal;
        R = ray.direction.reflectedAround(N);
        V = -ray.direction;
        NdotV = clampedValue(N.dot(V));
        albedo_from_map = material->hasAlbedoMap() ? sampleAlbedo(*material, hit, tracer.scene.textures) : White;

        refracted = material->isRefractive();
        if (refracted) {
            IOR = hit.from_behind ? (material->IOR / IOR_AIR) : (IOR_AIR / material->IOR);
            f32 c = IOR*IOR * (1.0f - (NdotV * NdotV));
            refracted = c < 1.0f;
            RF = refracted ? N.scaleAdd(IOR * NdotV - sqrtf(1 - c), ray.direction * IOR).normalized() : R;
        }
    }

    INLINE_XPU Color radianceFraction() {
        Color radiance_fraction{material->albedo * albedo_from_map};
        if (material->brdf == BRDF_CookTorrance) {
            radiance_fraction *= (1.0f - material->metalness) * ONE_OVER_PI;

            if (NdotV > 0.0f) { // TODO: This should not be necessary to check for, because rays would miss in that scenario so the code should never even get to this point - and yet it seems like it does.
                // If the viewing direction is perpendicular to the normal, no light can reflect
                // Both the numerator and denominator would evaluate to 0 in this case, resulting in NaN

                // If roughness is 0 then the NDF (and hence the entire brdf) evaluates to 0
                // Otherwise, a negative roughness makes no sense logically and would be a user-error
                if (material->roughness > 0.0f) {
                    H = (L + V).normalized();
                    NdotH = clampedValue(N.dot(H));
                    HdotL = clampedValue(H.dot(L));
                    Color Fs = cookTorrance(material->roughness, NdotL, NdotV, HdotL, NdotH, material->reflectivity, F);
                    radiance_fraction = radiance_fraction.mulAdd(1.0f - F, Fs);
                }
            }
        } else {
            radiance_fraction *= material->roughness * ONE_OVER_PI;

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
                    radiance_fraction = material->reflectivity.scaleAdd(
                            powf(specular_factor, exponent) * (1.0f - material->roughness),
                            radiance_fraction);
            }
        }

        return radiance_fraction;
    }

    INLINE_XPU bool isFacingLight(const Light &light) {
        if (light.is_directional) {
            Ld = Ld2 = INFINITY;
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

    INLINE_XPU bool shadeFromEmissiveQuads(SceneTracer &tracer, Color &color) {
        bool found = false;

        Ray &ray{tracer.shadow_ray};
        vec3 &p{tracer.shadow_hit.position};
        vec3 *v = emissive_quad_vertices;
        vec3 Ro;

        for (u32 i = 0; i < tracer.scene.counts.geometries; i++) {
            Geometry &quad{tracer.scene.geometries[i]};
            Transform &area_light{quad.transform};
            Material &emissive_material = tracer.scene.materials[quad.material_id];
            if (quad.type != GeometryType_Quad || !(emissive_material.isEmissive()))
                continue;

            L = area_light.position - P;
            NdotL = N.dot(L);
            if (NdotL <= 0.0f || L.dot(area_light.orientation * vec3{0.0f, -1.0f, 1.0f}) <= 0.0f)
                continue;

            L = L.normalized();
            Ro = L.scaleAdd(TRACE_OFFSET, P);
            ray.localize(Ro, L, area_light);
            if (ray.hitsDefaultQuad(tracer.shadow_hit, quad.flags & GEOMETRY_IS_TRANSPARENT) &&
                tracer.shadow_hit.distance < hit.distance) {
                found = true;
            }

            f32 emission_intensity = N.dot(getAreaLightVector(area_light, P, v));
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

                f32 shaded_light = 1;
                for (u32 s = 0; s < tracer.scene.counts.geometries; s++) {
                    if (s == i)
                        continue;

                    Geometry &shadowing_primitive = tracer.scene.geometries[s];
//                    if (N.dot(shadowing_primitive.transform.position - quad.transform.position) >= 0.0f)
//                        continue;

                    ray.localize(Ro, L, shadowing_primitive.transform);
                    tracer.shadow_hit.distance = hit.distance;
                    f32 d = 1;
                    if (shadowing_primitive.type == GeometryType_Sphere) {
                        if (ray.hitsDefaultSphere(tracer.shadow_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT))
                            d -= (1.0f - tracer.shadow_hit.distance) / (tracer.shadow_hit.distance * emission_intensity * 3);
                    } else if (shadowing_primitive.type == GeometryType_Quad) {
                        if (ray.hitsDefaultQuad(tracer.shadow_hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT)) {
                            p.y = 0;
                            p.x = p.x < 0 ? -p.x : p.x;
                            p.z = p.z < 0 ? -p.z : p.z;
                            if (p.x > p.z) {
                                p.y = p.z;
                                p.z = p.x;
                                p.x = p.y;
                                p.y = 0;
                            }
                            d -= (1.0f - p.z) / (tracer.shadow_hit.distance * emission_intensity);
                        }
                    }
                    if (d < shaded_light)
                        shaded_light = d;
                }

                if (shaded_light > 0.0f)
                    color = radianceFraction().mulAdd(emissive_material.emission * (emission_intensity * shaded_light), color);
            }
        }

        return found;
    }
};
