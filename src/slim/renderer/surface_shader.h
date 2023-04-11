#pragma once

#include "../core/ray.h"
#include "../core/texture.h"
#include "../scene/material.h"
#include "./scene_tracer.h"


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

INLINE_XPU vec3 getAreaLightVector(const Transform &transform, vec3 P, vec3 *v) {
    const f32 sx = transform.scale.x;
    const f32 sz = transform.scale.z;
    if (sx == 0 || sz == 0)
        return 0.0f;

    vec3 U{transform.orientation * vec3{sx < 0 ? -sx : sx, 0.0f, 0.0f}};
    vec3 V{transform.orientation * vec3{0.0f, 0.0f , sz < 0 ? -sz : sz}};
    v[0] = transform.position - U - V;
    v[1] = transform.position + U - V;
    v[2] = transform.position + U + V;
    v[3] = transform.position - U + V;

    vec3 u1n{(v[0] - P).normalized()};
    vec3 u2n{(v[1] - P).normalized()};
    vec3 u3n{(v[2] - P).normalized()};
    vec3 u4n{(v[3] - P).normalized()};

    return {
        u1n.cross(u2n) * (acosf(u1n.dot(u2n)) * 0.5f) +
        u2n.cross(u3n) * (acosf(u2n.dot(u3n)) * 0.5f) +
        u3n.cross(u4n) * (acosf(u3n.dot(u4n)) * 0.5f) +
        u4n.cross(u1n) * (acosf(u4n.dot(u1n)) * 0.5f)
    };
}

struct LightsShader {
    f32 b = 0, c = 0, t_near = 0, t_far = 0, t_max = 0, closest_hit_distance = 0;

    INLINE_XPU bool hit(const vec3 &Ro, const vec3 &Rd, const vec3 &target, f32 inverse_scale) {
        vec3 rc{(target - Ro) * inverse_scale};

        b = Rd.dot(rc);
        c = rc.squaredLength() - 1;
        f32 h = b*b - c;

        if (h < 0)
            return false;

        h = sqrtf(h);
        t_near = b - h;
        t_far  = b + h;

        return t_far > 0 && t_near < t_max;
    }

    INLINE_XPU f32 getVolumeDensity() {
        f32 t1 = t_near > 0 ? t_near : 0;
        f32 t2 = t_far < t_max ? t_far : t_max;

        // analytical integration of an inverse squared density
        return (c*t1 - b*t1*t1 + t1*t1*t1/3.0f - (
            c*t2 - b*t2*t2 + t2*t2*t2/3.0f)
               ) * (3.0f / 4.0f);
    }

    INLINE_XPU bool shadeLights(const Light *lights, const u32 light_count, const vec3 &Ro, const vec3 &Rd, f32 max_distance, Color &color) {
        f32 scale, inverse_scale, density;
        bool hit_light = false;

        closest_hit_distance = INFINITY;
        for (u32 i = 0; i < light_count; i++) {
            const Light &light = lights[i];

            scale = light.intensity / 64.0f;
            inverse_scale = 1.0f / scale;
            t_max = max_distance * inverse_scale;
            if (hit(Ro, Rd, light.position_or_direction, inverse_scale)) {
                hit_light = true;
                density = getVolumeDensity();
                closest_hit_distance = Min(closest_hit_distance, t_far);
                color = light.color.scaleAdd(  pow(density, 8.0f) * 4, color);
            }
        }

        return hit_light;
    }
};

struct SurfaceShader {
    Geometry *geometry = nullptr;
    Material *material = nullptr;

    Color F, albedo_from_map;
    vec3 P, N, V, L, R, RF, H, emissive_quad_vertices[4];
    f32 Ld, Ld2, NdotL, NdotV, NdotH, HdotL, IOR;

    bool refracted = false;

    INLINE_XPU void shadeFromLight(const Light &light, const Scene &scene, SceneTracer &tracer, Color &color) {
        if (isFacingLight(light) &&
            !tracer.inShadow(scene, P, L, Ld))
            // color += fr(p, L, V) * Li(p, L) * cos(w)
            color = radianceFraction().mulAdd(light.color * (NdotL * light.intensity / Ld2), color);
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

    INLINE_XPU bool shadeFromEmissiveQuads(const Scene &scene, Ray &ray, RayHit &hit, Color &color) {
        bool found = false;

        vec3 &p{hit.position};
        vec3 *v = emissive_quad_vertices;
        vec3 Ro;

        for (u32 i = 0; i < scene.counts.geometries; i++) {
            Geometry &quad{scene.geometries[i]};
            Transform &area_light{quad.transform};
            Material &emissive_material = scene.materials[quad.material_id];
            if (quad.type != GeometryType_Quad || !(emissive_material.isEmissive()))
                continue;

            L = area_light.position - P;
            NdotL = N.dot(L);
            if (NdotL <= 0.0f || L.dot(area_light.orientation * vec3{0.0f, -1.0f, 1.0f}) <= 0.0f)
                continue;

            L = L.normalized();
            Ro = L.scaleAdd(TRACE_OFFSET, P);
            ray.localize(Ro, L, area_light);
            if (ray.hitsDefaultQuad(hit, quad.flags & GEOMETRY_IS_TRANSPARENT) &&
                hit.distance < hit.distance) {
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
                for (u32 s = 0; s < scene.counts.geometries; s++) {
                    if (s == i)
                        continue;

                    Geometry &shadowing_primitive = scene.geometries[s];
//                    if (N.dot(shadowing_primitive.transform.position - quad.transform.position) >= 0.0f)
//                        continue;

                    ray.localize(Ro, L, shadowing_primitive.transform);
                    hit.distance = hit.distance;
                    f32 d = 1;
                    if (shadowing_primitive.type == GeometryType_Sphere) {
                        if (ray.hitsDefaultSphere(hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT))
                            d -= (1.0f - hit.distance) / (hit.distance * emission_intensity * 3);
                    } else if (shadowing_primitive.type == GeometryType_Quad) {
                        if (ray.hitsDefaultQuad(hit, shadowing_primitive.flags & GEOMETRY_IS_TRANSPARENT)) {
                            p.y = 0;
                            p.x = p.x < 0 ? -p.x : p.x;
                            p.z = p.z < 0 ? -p.z : p.z;
                            if (p.x > p.z) {
                                p.y = p.z;
                                p.z = p.x;
                                p.x = p.y;
                                p.y = 0;
                            }
                            d -= (1.0f - p.z) / (hit.distance * emission_intensity);
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
