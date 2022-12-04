#pragma once

#include "../common.h"

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
                t_far *= scale;
                if (t_far < closest_hit_distance)
                    closest_hit_distance = t_far;

                color = light.color.scaleAdd(  powf(density, 8), color);
            }
        }

        return hit_light;
    }
};