#pragma once

#include "../common.h"

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

struct LightsShader {
    Light *lights;
    u32 light_count;
    f32 b = 0, c = 0, t_near = 0, t_far = 0, t_max = 0, closest_hit_distance = 0;

    INLINE_XPU LightsShader(Light *lights, u32 light_count) : lights{lights}, light_count{light_count} {}

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

    INLINE_XPU bool shadeLights(const vec3 &Ro, const vec3 &Rd, f32 max_distance, Color &color) {
        f32 scale, inverse_scale, density;
        bool hit_light = false;

        closest_hit_distance = INFINITY;
        for (u32 i = 0; i < light_count; i++) {
            Light &light = lights[i];

            scale = light.intensity / 32.0f;
            inverse_scale = 1.0f / scale;
            t_max = max_distance * inverse_scale;
            if (hit(Ro, Rd, light.position_or_direction, inverse_scale)) {
                hit_light = true;
                density = getVolumeDensity();
                t_far  = t_far - scale * 8;
                if (t_far < closest_hit_distance)
                    closest_hit_distance = t_far;

                color = light.color.scaleAdd(  powf(density, 32) * 32, color);
            }
        }

        return hit_light;
    }
};