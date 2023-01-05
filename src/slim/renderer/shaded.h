#pragma once

#include "../core/ray.h"
#include "../core/texture.h"
#include "../scene/material.h"


struct Shaded : RayHit {
    Color albedo = White;
    vec3 V, reflected_direction, refracted_direction, light_direction, emissive_quad_vertices[4];
    f32 light_distance, light_distance_squared, NdotL, NdotV, R0, IOR, IOR2 = IOR_AIR;

    Material *material = nullptr;
    Geometry *geometry = nullptr;

    INLINE_XPU const Shaded& operator=(const RayHit &hit) {
        *((RayHit*)(this)) = hit;
        return *this;
    }

    INLINE_XPU static f32 distanceToColor(f32 distance) {                                     return 4.0f / distance; }
    INLINE_XPU static vec3 directionAndDistanceToColor(const vec3 &direction, f32 distance) { return (direction + 1.0f) * distanceToColor(distance); }
    INLINE_XPU static vec3 directionToColor(const vec3 &direction) {                          return (direction + 1.0f) / 2.0f; }

    INLINE_XPU Color getColorByDirection(const vec3 &direction) {            return directionToColor(direction).toColor(); }
    INLINE_XPU Color getColorByDirectionAndDistance(const vec3 &direction) { return directionAndDistanceToColor(direction, distance).toColor(); }
    INLINE_XPU Color getColorByDistance() {                                  return distanceToColor(distance); }
    INLINE_XPU Color getColorByUV() {                                        return {uv.u, uv.v, 1.0f}; }
    INLINE_XPU Color getColorByMipLevel(const Texture &texture) {            return MIP_LEVEL_COLORS[texture.mipLevel(uv_coverage)]; }

    INLINE_XPU static vec3 decodeNormal(const Color &color) {
        return vec3{color.r, color.b, color.g}.scaleAdd(2.0f, -1.0f).normalized();
    }

    INLINE_XPU static quat getNormalRotation(vec3 N, f32 magnitude = 1.0f) {
        // axis      = up ^ normal = [0, 1, 0] ^ [x, y, z] = [1*z - 0*y, 0*x - 0*z, 0*y - 1*x] = [z, 0, -x]
        // cos_angle = up . normal = [0, 1, 0] . [x, y, z] = 0*x + 1*y + 0*z = y
        // (Pre)Swizzle N.z <-> N.y
        return quat::AxisAngle(vec3{N.z, 0, -N.x}.normalized(),  acosf(N.y) * magnitude);
    }

    INLINE_XPU static Color sample(const Material &material, u8 material_texture_slot, const Texture *textures, f32 u, f32 v, f32 uv_area) {
        return material.texture_count > material_texture_slot ? textures[material.texture_ids[material_texture_slot]].sample(u, v, uv_area).color : Black;
    }

    INLINE_XPU static quat convertNormalSampleToRotation(const Color &normal_sample, f32 magnitude) {
        vec3 N = decodeNormal(normal_sample);
        return getNormalRotation(N, magnitude);
    }

    INLINE_XPU static vec3 rotateNormal(const vec3 &normal, const Color &normal_sample, f32 magnitude) {
         return convertNormalSampleToRotation(normal_sample, magnitude) * normal;
    }

    INLINE_XPU f32 ggxNDF(f32 roughness_squared, f32 NdotH) { // Trowbridge-Reitz:
        f32 demon = NdotH * NdotH * (roughness_squared - 1.0f) + 1.0f;
        return ONE_OVER_PI * roughness_squared / (demon * demon);
    }

    INLINE_XPU f32 ggxSmithSchlick(f32 roughness) {
        f32 k = roughness * 0.5f; // Approximation from Karis (UE4)
//    f32 k = roughness + 1.0f;
//    k *= k * 0.125f;
        f32 one_minus_k = 1.0f - k;
        f32 denom = fast_mul_add(NdotV, one_minus_k, k);
        f32 result = NdotV / fmaxf(denom, EPS);
        denom = fast_mul_add(NdotL, one_minus_k, k);
        result *= NdotL / fmaxf(denom, EPS);
        return result;
    }

    INLINE_XPU f32 ggxFresnelSchlick(f32 LdotH) {
        return R0 + (1.0f - R0) * powf(1.0f - LdotH, 5.0f);
    }

    INLINE_XPU Color sample(const Texture &texture) const {
        return texture.sample(uv.u, uv.v, uv_coverage).color;
    }

    INLINE_XPU Color sample(const Texture *textures, u8 material_texture_slot) const {
        return material->texture_count > material_texture_slot ? sample(textures[material->texture_ids[material_texture_slot]]) : Black;
    }

    INLINE_XPU Color sampleAlbedo(const Texture *textures) const {
        return sample(textures, 0);
    }

    INLINE_XPU Color sampleNormal(const Texture *textures) const {
        return sample(textures, 1);
    }

    INLINE_XPU quat convertNormalSampleToRotation(const Color &normal_sample) const {
        return convertNormalSampleToRotation(normal_sample, material->normal_magnitude);
    }

    INLINE_XPU vec3 rotateNormal(const vec3 &normal, const Color &normal_sample) const {
        return rotateNormal(normal, normal_sample, material->normal_magnitude);
    }

    INLINE_XPU void setLight(const Light &light) {
        if (light.is_directional) {
            light_distance = light_distance_squared = INFINITY;
            light_direction = -light.position_or_direction;
        } else {
            light_direction = light.position_or_direction - position;
            light_distance_squared = light_direction.squaredLength();
            light_distance = sqrtf(light_distance_squared);
            light_direction /= light_distance;
        }
        NdotL = clampedValue(normal.dot(light_direction));
    }

    INLINE_XPU Color shadeClassic(const Color &diffuse, f32 specular_factor, f32 exponent) {
        f32 shininess = 1.0f - material->roughness;
        specular_factor = NdotL * powf(specular_factor,exponent * shininess) * shininess;
        return material->reflectivity.scaleAdd(specular_factor, diffuse);
    }

    INLINE_XPU Color radianceFraction() {
        Color diffuse = albedo * (material->roughness * NdotL * ONE_OVER_PI);

        if (material->brdf == BRDF_Lambert) return diffuse;
        if (material->brdf == BRDF_Phong) {
            f32 RdotL = clampedValue(reflected_direction.dot(light_direction));
            return RdotL ? shadeClassic(diffuse, RdotL, 4.0f) : diffuse;
        }

        vec3 H = (light_direction + V).normalized();
        f32 NdotH = clampedValue(normal.dot(H));
        if (material->brdf == BRDF_Blinn ? !NdotH : (!NdotV || (!material->roughness && NdotH == 1.0f)))
            return diffuse;

        if (material->brdf == BRDF_Blinn)
            return shadeClassic(diffuse, NdotH, 16.0f);

        // Cook-Torrance BRDF:
        f32 F = ggxFresnelSchlick(clampedValue(light_direction.dot(H)));
        f32 D = ggxNDF(material->roughness * material->roughness, NdotH);
        f32 G = ggxSmithSchlick(material->roughness);
        return material->reflectivity.scaleAdd(F * D * G / (4.0f * NdotV), diffuse * (1.0f - F));
    }

    INLINE_XPU void prepareForShading(const Ray &ray, Material *materials, Texture *textures) {
        material = materials + geometry->material_id;
        albedo = material->albedo;
        if (material->hasAlbedoMap()) albedo *= sampleAlbedo(textures);
        if (material->hasNormalMap()) normal = rotateNormal(normal, sampleNormal(textures));

        V = -ray.direction;
        NdotV = clampedValue(normal.dot(V));

        IOR = from_behind ? (material->IOR / IOR_AIR) : (IOR_AIR / material->IOR);
        IOR2 = 1.0f / IOR;
        R0 = IOR - IOR2;
        R0 /= IOR + IOR2;
        R0 *= R0;

        reflected_direction = ray.direction.reflectedAround(normal);
        f32 c = IOR*IOR * (1 - (NdotV * NdotV));
        refracted_direction = c > 1 ? reflected_direction : (normal.scaleAdd(IOR * NdotV - sqrtf(1 - c), ray.direction * IOR)).normalized();
    }
};