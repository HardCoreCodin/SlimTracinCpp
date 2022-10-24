#pragma once

#include "../trace.h"
#include "../common.h"
#include "../intersection/sphere.h"

INLINE_XPU f32 getSphericalVolumeDensity(SphereHit *hit) {
    f32 t1 = hit->t_near > 0 ? hit->t_near : 0;
    f32 t2 = hit->t_far  < hit->furthest ? hit->t_far : hit->furthest;

    // analytical integration of an inverse squared density
    return (hit->c*t1 - hit->b*t1*t1 + t1*t1*t1/3.0f - (
            hit->c*t2 - hit->b*t2*t2 + t2*t2*t2/3.0f)
            ) * (3.0f / 4.0f);
}

INLINE_XPU bool shadeLights(Light *lights, u32 light_count, vec3 Ro, vec3 Rd, f32 max_distance, SphereHit *sphere_hit, vec3 *color) {
    Light *light = lights;
    f32 one_over_light_radius, density, attenuation;
    bool hit_light = false;
    sphere_hit->closest_hit_distance = INFINITY;
    for (u32 i = 0; i < light_count; i++, light++) {
        f32 size = light->intensity * (1.0f / 4.0f);
        one_over_light_radius = 8.0f / size;
        attenuation = one_over_light_radius * one_over_light_radius;
        sphere_hit->furthest = max_distance * one_over_light_radius;
        if (hitSphereSimple(Ro, Rd, light->position_or_direction, one_over_light_radius, sphere_hit)) {
            density = getSphericalVolumeDensity(sphere_hit);
            *color = light->color.scaleAdd( density * attenuation, *color);

            hit_light = true;
        }
    }
    return hit_light;
}