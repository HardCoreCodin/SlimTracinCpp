#pragma once

#include "../core/ray.h"
#include "../core/transform.h"
#include "../scene/material.h"


// Filmic Tone-Mapping: https://www.slideshare.net/naughty_dog/lighting-shading-by-john-hable
// ====================
// A = Shoulder Strength (i.e: 0.22)
// B = Linear Strength   (i.e: 0.3)
// C = Linear Angle      (i.e: 0.1)
// D = Toe Strength      (i.e: 0.2)
// E = Toe Numerator     (i.e: 0.01)
// F = Toe Denumerator   (i.e: 0.3)
// LinearWhite = Linear White Point Value (i.e: 11.2)
//   Note: E/F = Toe Angle
//   Note: i.e numbers are NOT gamma corrected(!)
//
// f(x) = ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F
//
// FinalColor = f(LinearColor)/f(LinearWhite)
//
// i.e:
// x = max(0, LinearColor-0.004)
// GammaColor = (x*(6.2*x + 0.5))/(x*(6.2*x+1.7) + 0.06)
//
// A = 6.2
// B = 1.7
//
// C*B = 1/2
// D*F = 0.06
// D*E = 0
//
// C = 1/2*1/B = 1/2*1/1.7 = 1/(2*1.7) = 1/3.4 =
// D = 1
// F = 0.06
// E = 0

#define TONE_MAP__SHOULDER_STRENGTH 6.2f
#define TONE_MAP__TOE_STRENGTH 1
#define TONE_MAP__TOE_NUMERATOR 0
#define TONE_MAP__TOE_DENOMINATOR 1
#define TONE_MAP__TOE_ANGLE (TONE_MAP__TOE_NUMERATOR / TONE_MAP__TOE_DENOMINATOR)
#define TONE_MAP__LINEAR_ANGLE (1.0f/3.4f)
#define TONE_MAP__LINEAR_WHITE 1
#define TONE_MAP__LINEAR_STRENGTH 1
// LinearWhite = 1:
// f(LinearWhite) = f(1)
// f(LinearWhite) = (A + C*B + D*E)/(A + B + D*F) - E/F
#define TONE_MAPPED__LINEAR_WHITE ( \
    (                               \
        TONE_MAP__SHOULDER_STRENGTH + \
        TONE_MAP__LINEAR_ANGLE * TONE_MAP__LINEAR_STRENGTH + \
        TONE_MAP__TOE_STRENGTH * TONE_MAP__TOE_NUMERATOR \
    ) / (                           \
        TONE_MAP__SHOULDER_STRENGTH + TONE_MAP__LINEAR_STRENGTH + \
        TONE_MAP__TOE_STRENGTH * TONE_MAP__TOE_DENOMINATOR  \
    ) - TONE_MAP__TOE_ANGLE \
)

INLINE_XPU f32 toneMapped(f32 LinearColor) {
    f32 x = LinearColor - 0.004f;
    if (x < 0.0f) x = 0.0f;
    f32 x2 = x*x;
    f32 x2_times_sholder_strength = x2 * TONE_MAP__SHOULDER_STRENGTH;
    f32 x_times_linear_strength   =  x * TONE_MAP__LINEAR_STRENGTH;
    return (
                   (
                           (
                                   x2_times_sholder_strength + x*x_times_linear_strength + TONE_MAP__TOE_STRENGTH*TONE_MAP__TOE_NUMERATOR
                           ) / (
                                   x2_times_sholder_strength +   x_times_linear_strength + TONE_MAP__TOE_STRENGTH*TONE_MAP__TOE_DENOMINATOR
                           )
                   ) - TONE_MAP__TOE_ANGLE
           ) / (TONE_MAPPED__LINEAR_WHITE);
}


INLINE_XPU f32 gammaCorrected(f32 x) {
    return (x <= 0.0031308f ? (x * 12.92f) : (1.055f * powf(x, 1.0f/2.4f) - 0.055f));
}

INLINE_XPU f32 gammaCorrectedApproximately(f32 x) {
    return powf(x, 1.0f/2.2f);
}
INLINE_XPU f32 toneMappedBaked(f32 LinearColor) {
    // x = max(0, LinearColor-0.004)
    // GammaColor = (x*(6.2*x + 0.5))/(x*(6.2*x+1.7) + 0.06)
    // GammaColor = (x*x*6.2 + x*0.5)/(x*x*6.2 + x*1.7 + 0.06)

    f32 x = LinearColor - 0.004f;
    if (x < 0.0f) x = 0.0f;
    f32 x2_times_sholder_strength = x * x * 6.2f;
    return (x2_times_sholder_strength + x*0.5f)/(x2_times_sholder_strength + x*1.7f + 0.06f);
}

INLINE_XPU f32 invDotVec3(vec3 a, vec3 b) {
    return clampedValue(-a.dot(b));
}

INLINE_XPU vec3 reflectWithDot(vec3 V, vec3 N, f32 NdotV) {
    return N.scaleAdd(-2.0f * NdotV, V);
}

INLINE_XPU vec3 refract(vec3 V, vec3 N, f32 n1_over_n2, f32 NdotV) {
    f32 c = n1_over_n2*n1_over_n2 * (1 - (NdotV*NdotV));
    if (c > 1)
        return 0.0f;

    V *= n1_over_n2;
    V = N.scaleAdd(n1_over_n2 * -NdotV - sqrtf(1 - c), V);
    return V.normalized();
}

INLINE_XPU bool isTransparentUV(vec2 uv) {
    u8 v = (u8)(uv.y * 4);
    u8 u = (u8)(uv.x * 4);
    return v % 2 != 0 == u % 2;
}

INLINE_XPU BoxSide getBoxSide(vec3 octant, u8 axis) {
    switch (axis) {
        case 0 : return octant.x > 0 ? BoxSide_Right : BoxSide_Left;
        case 3 : return octant.x > 0 ? BoxSide_Left : BoxSide_Right;
        case 1 : return octant.y > 0 ? BoxSide_Top : BoxSide_Bottom;
        case 4 : return octant.y > 0 ? BoxSide_Bottom : BoxSide_Top;
        case 2 : return octant.z > 0 ? BoxSide_Front : BoxSide_Back;
        default: return octant.z > 0 ? BoxSide_Back : BoxSide_Front;
    }
}

INLINE_XPU vec2 getUVonUnitCube(vec3 pos, BoxSide side) {
    vec2 uv;

    switch (side) {
        case BoxSide_Top: {
            uv.x = pos.x;
            uv.y = pos.z;
        } break;
        case BoxSide_Bottom: {
            uv.x = -pos.x;
            uv.y = -pos.z;
        } break;
        case BoxSide_Left: {
            uv.x = -pos.z;
            uv.y = pos.y;
        } break;
        case BoxSide_Right:  {
            uv.x = pos.z;
            uv.y = pos.y;
        } break;
        case BoxSide_Front: {
            uv.x = pos.x;
            uv.y = pos.y;
        } break;
        default: {
            uv.x = -pos.x;
            uv.y =  pos.y;
        } break;
    }

    uv.x += 1;
    uv.y += 1;
    uv.x *= 0.5f;
    uv.y *= 0.5f;

    return uv;
}

INLINE_XPU vec3 getAreaLightVector(Transform &transform, vec3 P, vec3 *v) {
    const f32 sx = transform.scale.x;
    const f32 sz = transform.scale.z;
    if (sx == 0 || sz == 0)
        return 0.0f;

    vec3 U{transform.rotation * vec3{sx < 0 ? -sx : sx, 0.0f, 0.0f}};
    vec3 V{transform.rotation * vec3{0.0f, 0.0f ,sz < 0 ? -sz : sz}};
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
INLINE_XPU vec3 ggxFresnelSchlick(vec3 reflectivity, f32 LdotH) {
    return (1.0f - reflectivity).scaleAdd(powf(1.0f - LdotH, 5.0f), reflectivity);
}
INLINE_XPU vec3 shadeClassic(vec3 diffuse, f32 NdotL, vec3 specular, f32 specular_factor, f32 exponent, f32 roughness) {
    f32 shininess = 1.0f - roughness;
    specular_factor = NdotL * powf(specular_factor,exponent * shininess) * shininess;
    return specular.scaleAdd(specular_factor, diffuse);
}
INLINE_XPU vec3 shadePointOnSurface(Shaded *shaded, f32 NdotL) {
    Material *M = shaded->material;
    vec3 nothing;
    vec3 diffuse = shaded->albedo * M->roughness * NdotL * ONE_OVER_PI;

    if (M->brdf == BRDF_Lambert) return diffuse;
    if (M->brdf == BRDF_Phong) {
        f32 RdotL = shaded->reflected_direction.dot(shaded->light_direction);
        return RdotL ? shadeClassic(diffuse, NdotL, M->reflectivity, RdotL, 4.0f, M->roughness) : diffuse;
    }

    vec3 N = shaded->normal;
    vec3 L = shaded->light_direction;
    vec3 V = -shaded->viewing_direction;
    vec3 H = (L - V).normalized();
    f32 NdotH = N.dot(H);
    if (M->brdf == BRDF_Blinn)
        return NdotH ? shadeClassic(diffuse, NdotL, M->reflectivity, NdotH, 16.0f, M->roughness) : diffuse;

    // Cook-Torrance BRDF:
    vec3 F = nothing;
    diffuse = shaded->albedo * ((1.0f - M->metallic) * NdotL * ONE_OVER_PI);
    f32 LdotH = L.dot(H);
    if (LdotH != 1.0f) {
        F = ggxFresnelSchlick(M->reflectivity, LdotH);
        diffuse *= 1.0f - F;
    }
    if (!M->roughness && NdotH == 1.0f)
        return diffuse;

    f32 NdotV = N.dot(V);
    if (!NdotV) return diffuse;

    f32 D = ggxNDF(M->roughness * M->roughness, NdotH);
    f32 G = ggxSmithSchlick(NdotL, NdotV, M->roughness);
    f32 factor = D * G / (4.0f * NdotV);
    vec3 radiance{F.scaleAdd(factor, diffuse)};
    return radiance;
}

INLINE_XPU f32 dUVbyRayCone(f32 NdotV, f32 cone_width, f32 area, f32 uv_area) {
    f32 projected_cone_width = cone_width / fabsf(NdotV);
    return sqrtf((projected_cone_width * projected_cone_width) * (uv_area / area));
}