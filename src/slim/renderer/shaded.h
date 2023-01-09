#pragma once

#include "../core/ray.h"
#include "../core/texture.h"
#include "../scene/material.h"

struct Sampler {
    const RayHit &hit;
    const Texture *textures = nullptr;
    Material *material = nullptr;

    Sampler(const RayHit &hit, const Texture *textures, Material *material = nullptr) : hit{hit}, textures{textures}, material{material} {}

    INLINE_XPU static Color sample(const Material &material, u8 slot, const Texture *textures, vec2 uv, f32 uv_coverage) {
        return (textures && material.texture_count > slot) ? textures[material.texture_ids[slot]].sample(uv.u, uv.v, uv_coverage).color : Black;
    }

    INLINE_XPU Color sample(const Texture &texture) const { return texture.sample(hit.uv.u, hit.uv.v, hit.uv_coverage).color; }
    INLINE_XPU Color sample(u8 texture_slot) const { return sample(*material, texture_slot, textures, hit.uv, hit.uv_coverage); }
    INLINE_XPU Color sampleAlbedo() const { return sample(0); }
    INLINE_XPU Color sampleNormal() const { return sample(1); }
    INLINE_XPU vec3 sampleNormal(const vec3 &normal) const { return rotateNormal(normal, sampleNormal(), material->normal_magnitude); }
};



struct Shader {
    Material *M;
    Color F, albedo_from_map;
    vec3 N, V, L, R, RF, H;
    f32 Ld, Ld2, NdotL, NdotV, NdotH, HdotL, RdotL, IOR;
    bool refracted = false;

    INLINE_XPU void reset(Material &material, const vec3 &normal, const vec3 &direction, bool from_behind, const Color &sampled_albedo = White) {
        M = &material;
        V = -direction;
        N = normal;
        R = direction.reflectedAround(N);
        NdotV = clampedValue(N.dot(V));
        albedo_from_map = sampled_albedo;

        refracted = material.isRefractive();
        if (refracted) {
            IOR = from_behind ? (material.IOR / IOR_AIR) : (IOR_AIR / material.IOR);
            f32 c = IOR*IOR * (1.0f - (NdotV * NdotV));
            refracted = c < 1.0f;
            RF = refracted ? normal.scaleAdd(IOR * NdotV - sqrtf(1 - c), direction * IOR).normalized() : R;
        }

        if (M->brdf == BRDF_CookTorrance) {
            // Metals have no albedo color, and so typically in PBR the albedo component is re-purposed to be used for
            // the specular color of the reflective metal, represents the reflected color at a zero angle (F0).
            // So if a metalic material is textured, it can encode F0 values per-texel in the albedo map, to be used
            // instead of the constant material parameter, and so based on metalness the kd (albedo sample) is take.
            // Similarly, Kd is lerped towards black by metalness, to nullify the diffuse contributions (for metals).
//            F0 = material.F0.lerpTo(Kd, material.metalness);
//            Kd = Kd.lerpTo(Black, material.metalness);
            // This allows for layered material containing both metalic and dialectric surfaces using a single texture.
            // When 'shadeFromLight()' below is invoked for a material with the Cook Torrance BRDF, it assumes that for
            // metalic materials the F0 will contain the specular color, and so stored the result in Ks.
        } else {
            // When not using the Cook-Torrance BRDF, F0 is computed from the index of refraction.
//            IOR2 = 1.0f / IOR;
//            R0 = IOR - IOR2;
//            R0 /= IOR + IOR2;
//            R0 *= R0;
        }
    }

    INLINE_XPU Color fr(const Color& Kd, f32 Fd, const Color &Ks, f32 Fs) {
        // The reflectance portion of the reflectance equation with Cook Torrance:
        // Kd*Fd + Ks*Fs (FMA used here for performance)
        return Kd.scaleAdd(Fd, Ks * Fs);
    }

    INLINE_XPU Color Li(const Light &light) {
        return light.color * (light.intensity / Ld2);
    }

    INLINE_XPU void shadeFromLight(const Light &light, Color &color) {
        Color Kd = M->albedo * albedo_from_map * M->roughness;
        Color Ks = M->reflectivity;
        f32 Fd = ONE_OVER_PI;
        f32 Fs = 1.0f;

        // Compute specular component coefficient:
        if (M->brdf == BRDF_Phong) {
            RdotL = clampedValue(R.dot(L));
            // If the light is perpendicular-to/has-obtuse-angle-towards the reflected direction, the light is hitting
            // the surface from behind the viewing direction, so no specular contribution should be accounted for.
            if (RdotL > 0.0f)
                Fs = classicSpecularFactor(1.0f - M->roughness, RdotL, 4.0f);
            else
                Ks = 0.0f;
        } else {
            // Both Blinn and Cook-Torrance require the half vector and the cosine of its angle to the normal direction:
            H = (L + V).normalized();
            NdotH = N.dot(H);
            // Note: If the half vector is perpendicular to the normal, the light aims directly opposite to the normal.
            // If the half vector has obtuse angle to the normal, the light aims away from the normal.
            // In either cases, the cosine of the normal and the light would also been zero/negative.
            // That means the code would never even get here, because this is checked for before.
            // Because of that, there is no need for a check the angle between the normal and the half vector
            // There is also no need to clamp the cosine, for the same reason.

//            NdotH = clampedValue(N.dot(H);
//            if (NdotH > 0.0f) {
            if (M->brdf == BRDF_CookTorrance) {
                // if (NdotV != 0.0f) {
                // If the viewing direction is perpendicular to the normal, no light can reflect
                // Both the numerator and denominator would evaluate to 0 in this case, resulting in NaN
                // Note: This should not be necessary to check for, because rays would miss in that scenario,
                // so the code should never even get to this point.

                // If roughness is 0 then the NDF (and hence the entire brdf) evaluates to 0
                // Otherwise, a negative roughness makes no sense logically and would be a user-error
                if (M->roughness > 0.0f) {
                    HdotL = clampedValue(H.dot(L));
                    Ks = cookTorrance(M->roughness, NdotL, NdotV, HdotL, NdotH, M->reflectivity, F);
                } else
                    Ks = 0.0f;

                Kd = M->albedo * albedo_from_map * (1.0f - F) * (1.0f - M->metalness);
            } else // brdf == BRDF_Blinn
                Fs = classicSpecularFactor(1.0f - M->roughness, NdotH, 16.0f);
        }

        // The reflectance equation (FMA used here for performance):
        // color += fr(p, L, V) * Li(p, L) * cos(w)
        Color result = fr(Kd, Fd, Ks, Fs) * NdotL;
        Color lighting = Li(light);
        color = (
                result.mulAdd(lighting,
                            color)
        );
    }

    INLINE_XPU bool isFacingLight(const Light &light, const vec3 &P) {
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
};
