#pragma once

#include "../math/vec2.h"
#include "../math/vec3.h"


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

struct Material {
    Color albedo = 1.0f;
    Color reflectivity = 1.0f;
    Color emission = 0.0f;
    UV uv_repeat = 1.0f;
    f32 roughness = 1.0f;
    f32 metallic = 0.0f;
    f32 normal_magnitude = 1.0f;
    f32 IOR = 1.0f;
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
             f32 IOR = 1.0f) :
            albedo{albedo},
            reflectivity{reflectivity},
            emission{emission},
            uv_repeat{uv_repeat},
            roughness{roughness},
            metallic{metallic},
            normal_magnitude{normal_magnitude},
            IOR{IOR},
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