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

enum BRDFType {
    BRDF_Lambert,
    BRDF_Blinn,
    BRDF_Phong,
    BRDF_CookTorrance
};

struct Material {
    vec3 albedo{1.0f};
    vec3 reflectivity{1.0f};
    vec3 emission{0.0f};
    vec2 uv_repeat{1.0f};
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
             const vec3 &albedo = 1.0f,
             const vec3 &reflectivity = 1.0f,
             const vec3 &emission = 0.0f,
             vec2 uv_repeat = vec2{1.0f},
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
    INLINE_XPU bool isTextured() const { return texture_count && flags & (MATERIAL_HAS_ALBEDO_MAP | MATERIAL_HAS_NORMAL_MAP); }
    INLINE_XPU bool hasDiffuseMap() const { return texture_count && flags & MATERIAL_HAS_ALBEDO_MAP; }
    INLINE_XPU bool hasNormalMap() const { return texture_count && flags & MATERIAL_HAS_NORMAL_MAP; }
};

struct Shaded {
    Color color;
    vec3 position, normal, viewing_direction, viewing_origin, reflected_direction, light_direction, albedo, emissive_quad_vertices[4];
    vec2 uv;
    f32 opacity, u, v, uv_area;

    Material *material;
    Geometry *geometry;


    INLINE_XPU static vec3 decodeNormal(const Color &color) {
        return vec3{color.r, color.b, color.g}.scaleAdd(2.0f, {-1.0f}).normalized();
    }

    INLINE_XPU static quat getNormalRotation(vec3 N, f32 magnitude = 1.0f) {
        // axis      = up ^ normal = [0, 1, 0] ^ [x, y, z] = [1*z - 0*y, 0*x - 0*z, 0*y - 1*x] = [z, 0, -x]
        // cos_angle = up . normal = [0, 1, 0] . [x, y, z] = 0*x + 1*y + 0*z = y
        // (Pre)Swizzle N.z <-> N.y
        return quat::AxisAngle(vec3{N.y, 0, -N.x}.normalized(),  acosf(N.z) * magnitude);
    }

    INLINE_XPU static vec3 sampleNormal(const Texture &normal_map, vec2 uv, f32 uv_area) {
        return vec3{normal_map.sample(uv.u, uv.v, uv_area).color}.scaleAdd(2.0f, {-1.0f}).normalized();
    }

    INLINE_XPU static quat sampleNormalRotation(const Texture &normal_map, vec2 uv, f32 uv_area, f32 magnitude = 1.0f) {
        return getNormalRotation(sampleNormal(normal_map, uv, uv_area), magnitude);
    }

    INLINE_XPU static vec3 applyNormalRotation(const vec3 &N, const Texture &normal_map, vec2 uv, f32 uv_area, f32 magnitude = 1.0f) {
        return getNormalRotation(sampleNormal(normal_map, uv, uv_area), magnitude) * N;
    }

    INLINE_XPU Pixel sample(const Texture &texture) const {
        return texture.sample(uv.u, uv.v, uv_area);
    }

    INLINE_XPU vec3 sampleNormal(const Texture &normal_map) {
        return sampleNormal(normal_map, uv, uv_area);
    }

    INLINE_XPU quat sampleNormalRotation(const Texture &normal_map, f32 magnitude = 1.0f) {
        return sampleNormalRotation(normal_map, uv, uv_area, magnitude);
    }

    INLINE_XPU vec3 applyNormalRotation(const vec3 &N, const Texture &normal_map, f32 magnitude = 1.0f) {
        return applyNormalRotation(N, normal_map, uv, uv_area, magnitude);
    }
};


struct AmbientLight{
    Color color{0.004f, 0.004f, 0.007f};
};

struct Light {
    vec3 position_or_direction, color;
    f32 intensity = 1.0f;
    bool is_directional = false;

    Light(vec3 position_or_direction = vec3{0.0f}, vec3 color = Color{White}, f32 intensity = 1.0f, bool is_directional = false) :
            position_or_direction{position_or_direction},
            color{color},
            intensity{intensity},
            is_directional{is_directional}
    {}
};
