#pragma once

#include "./mesh.h"
#include "./grid.h"
#include "./box.h"
#include "./camera.h"
#include "../core/ray.h"
#include "../core/texture.h"
#include "../core/transform.h"
#include "../serialization/mesh.h"
#include "../serialization/texture.h"

struct AmbientLight{
    Color color{0.004f, 0.004f, 0.007f};
};

struct Light {
    Color color;
    vec3 position_or_direction;
    f32 intensity = 1.0f;
    bool is_directional = false;

    Light(vec3 position_or_direction = 0.0f, Color color = White, f32 intensity = 1.0f, bool is_directional = false) :
            position_or_direction{position_or_direction},
            color{color},
            intensity{intensity},
            is_directional{is_directional}
    {}
};

enum BRDFType {
    BRDF_Lambert,
    BRDF_Blinn,
    BRDF_Phong,
    BRDF_CookTorrance
};

struct Material {
    Color albedo = 1.0f;
    Color reflectivity = 1.0f;
    Color emission = 0.0f;
    UV uv_repeat = 1.0f;
    f32 roughness = 1.0f;
    f32 metallic = 0.0f;
    f32 normal_magnitude = 1.0f;
    f32 n1_over_n2 = 1.0f;
    f32 n2_over_n1 = 1.0f;
    BRDFType brdf{BRDF_Phong};
    u8 texture_count = 0;
    u8 flags = 0;
    u8 texture_ids[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    Material(BRDFType brdf = BRDF_Phong,
             f32 roughness = 1.0f,
             f32 metallic = 0.0f,
             u8 flags = 0,
             u8 texture_count = 0,
             const Color &albedo = 1.0f,
             const Color &reflectivity = 1.0f,
             const Color &emission = 0.0f,
             UV uv_repeat = 1.0f,
             f32 normal_magnitude = 1.0f,
             f32 n1_over_n2 = 1.0f) :
            albedo{albedo},
            reflectivity{reflectivity},
            emission{emission},
            uv_repeat{uv_repeat},
            roughness{roughness},
            metallic{metallic},
            normal_magnitude{normal_magnitude},
            n1_over_n2{n1_over_n2},
            n2_over_n1{1.0f / n1_over_n2},
            brdf{brdf},
            texture_count{texture_count},
            flags{flags}
            {}

    INLINE_XPU bool isEmissive() const { return flags & MATERIAL_IS_EMISSIVE; }
    INLINE_XPU bool isReflective() const { return flags & MATERIAL_IS_REFLECTIVE; }
    INLINE_XPU bool isRefractive() const { return flags & MATERIAL_IS_REFRACTIVE; }
    INLINE_XPU bool isTextured() const { return texture_count && flags & (MATERIAL_HAS_ALBEDO_MAP | MATERIAL_HAS_NORMAL_MAP); }
    INLINE_XPU bool hasAlbedoMap() const { return texture_count && flags & MATERIAL_HAS_ALBEDO_MAP; }
    INLINE_XPU bool hasNormalMap() const { return texture_count && flags & MATERIAL_HAS_NORMAL_MAP; }
};

INLINE_XPU vec3 reflect(vec3 V, vec3 N, f32 NdotV) {
    return N.scaleAdd(-2.0f * NdotV, V);
}

INLINE_XPU vec3 refract(vec3 V, vec3 N, f32 NdotV, f32 IOR) {
    f32 c = IOR*IOR * (1 - (NdotV*NdotV));
    if (c > 1)
        return 0.0f;

    V *= IOR;
    V = N.scaleAdd(IOR * -NdotV - sqrtf(1 - c), V);
    return V.normalized();
}
INLINE_XPU f32 ggxNDF(f32 roughness_squared, f32 NdotH) { // Trowbridge-Reitz:
    f32 demon = NdotH * NdotH * (roughness_squared - 1.0f) + 1.0f;
    return ONE_OVER_PI * roughness_squared / (demon * demon);
}

INLINE_XPU f32 ggxSmithSchlick(f32 NdotL, f32 NdotV, f32 roughness) {
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

INLINE_XPU f32 ggxFresnelSchlick(f32 R0, f32 LdotH) {
    return R0 + (1.0f - R0) * powf(1.0f - LdotH, 5.0f);
}

static ColorID MIP_LEVEL_COLORS[9] = {
        BrightRed,
        BrightYellow,
        BrightGreen,
        BrightMagenta,
        BrightCyan,
        BrightBlue,
        BrightGrey,
        Grey,
        DarkGrey
};

struct Shaded : RayHit {
    Color albedo = White;
    vec3 viewing_direction, viewing_origin, reflected_direction, light_direction, emissive_quad_vertices[4];
    f32 light_distance, light_distance_squared, uv_coverage, NdotL, NdotV, R0, IOR, IOR2 = 1.0;
    Material *material = nullptr;
    Geometry *geometry = nullptr;

    INLINE_XPU void updateUVCoverage(f32 pixel_area_over_focal_length_squared) {
        uv_coverage = uv_coverage_over_surface_area * pixel_area_over_focal_length_squared * distance;
        uv_coverage /= NdotV;
    };

    INLINE_XPU void updateUV(UV uv_repeat) {
        uv *= uv_repeat;
        uv_coverage /= uv_repeat.u * uv_repeat.v;
    };

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


    INLINE_XPU void updateUV() {
        RayHit::updateUV(material->uv_repeat);
    };

    INLINE_XPU bool updateNdotL() {
        NdotL = clampedValue(normal.dot(light_direction));
        return NdotL > 0.0f;
    }

    INLINE_XPU void reset(Ray &ray, Geometry *geometries, Material *materials, Texture *textures = nullptr, f32 pixel_area_over_focal_length_squared = 0) {
        viewing_origin    = ray.origin;
        viewing_direction = ray.direction;
        geometry = geometries + id;
        material = materials + geometry->material_id;
        albedo = material->albedo;
        IOR = material->n1_over_n2;

        NdotV = -(normal.dot(ray.direction));
        R0 = IOR - IOR2;
        R0 /= IOR + IOR2;
        R0 *= R0;
        if (from_behind) {
            normal = -normal;
            NdotV = -NdotV;
            IOR = material->n2_over_n1;
        }
        reflected_direction = reflect(viewing_direction, normal, NdotV);
        if (textures && material->isTextured()) {
            updateUVCoverage(pixel_area_over_focal_length_squared);
            updateUV();
            if (material->hasAlbedoMap()) applyAlbedoMap(textures);
            if (material->hasNormalMap()) applyNormalMap(textures);
        }
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

    INLINE_XPU void applyNormalMap(const Texture *textures) {
        normal = rotateNormal(normal, sampleNormal(textures));
    }

    INLINE_XPU void applyAlbedoMap(const Texture *textures) {
        albedo *= sampleAlbedo(textures);
    }

    INLINE_XPU bool isFacing(const Light &light) {
        if (light.is_directional) {
            light_distance = light_distance_squared = INFINITY;
            light_direction = -light.position_or_direction;
        } else {
            light_direction = light.position_or_direction - position;
            light_distance_squared = light_direction.squaredLength();
            light_distance = sqrtf(light_distance_squared);
            light_direction /= light_distance;
        }
        return updateNdotL();
    }

    INLINE_XPU vec3 refractedDirection() {
        return refract(viewing_direction, normal, NdotV, IOR);
    }

    INLINE_XPU Color shadeClassic(const Color &diffuse, f32 NdotL, const Color &specular, f32 specular_factor, f32 exponent, f32 roughness) {
        f32 shininess = 1.0f - roughness;
        specular_factor = NdotL * powf(specular_factor,exponent * shininess) * shininess;
        return specular.scaleAdd(specular_factor, diffuse);
    }

    INLINE_XPU Color radianceFraction() {
        Color diffuse = albedo * (material->roughness * NdotL * ONE_OVER_PI);

        if (material->brdf == BRDF_Lambert) return diffuse;
        if (material->brdf == BRDF_Phong) {
            f32 RdotL = reflected_direction.dot(light_direction);
            return RdotL ? shadeClassic(diffuse, NdotL, material->reflectivity, RdotL, 4.0f, material->roughness) : diffuse;
        }

        vec3 H = (light_direction - viewing_direction).normalized();
        f32 NdotH = normal.dot(H);
        if (material->brdf == BRDF_Blinn ? !NdotH : (!NdotV || (!material->roughness && NdotH == 1.0f)))
            return diffuse;

        if (material->brdf == BRDF_Blinn)
            return shadeClassic(diffuse, NdotL, material->reflectivity, NdotH, 16.0f, material->roughness);

        // Cook-Torrance BRDF:
        f32 LdotH = clampedValue(light_direction.dot(H));
        f32 F = ggxFresnelSchlick(R0, LdotH);
        f32 D = ggxNDF(material->roughness * material->roughness, NdotH);
        f32 G = ggxSmithSchlick(NdotL, NdotV, material->roughness);
        return material->reflectivity.scaleAdd(F * D * G / (4.0f * NdotV), diffuse * (1.0f - F));
    }

};