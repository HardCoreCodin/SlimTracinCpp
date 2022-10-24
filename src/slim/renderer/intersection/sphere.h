#pragma once

#include "../common.h"

INLINE_XPU vec2 getUVonUnitSphere(vec3 direction) {
    vec2 uv;

    f32 x = direction.x;
    f32 y = direction.y;
    f32 z = direction.z;

    f32 z_over_x = x ? (z / x) : 2;
    f32 y_over_x = x ? (y / x) : 2;
    if (z_over_x <=  1 &&
        z_over_x >= -1 &&
        y_over_x <=  1 &&
        y_over_x >= -1) { // Right or Left
        uv.x = z_over_x;
        uv.y = x > 0 ? y_over_x : -y_over_x;
    } else {
        f32 x_over_z = z ? (x / z) : 2;
        f32 y_over_z = z ? (y / z) : 2;
        if (x_over_z <=  1 &&
            x_over_z >= -1 &&
            y_over_z <=  1 &&
            y_over_z >= -1) { // Front or Back:
            uv.x = -x_over_z;
            uv.y = z > 0 ? y_over_z : -y_over_z;
        } else {
            uv.x = x / (y > 0 ? y : -y);
            uv.y = z / y;
        }
    }

    uv.x += 1;  uv.x /= 2;
    uv.y += 1;  uv.y /= 2;


    return uv;
}

INLINE_XPU bool hitSphereSimple(vec3 Ro, vec3 Rd, vec3 target, f32 one_over_radius, SphereHit *hit) {
    vec3 rc{(target - Ro) * one_over_radius};

    hit->b = Rd.dot(rc);
    hit->c = rc.squaredLength() - 1;
    f32 h = hit->b*hit->b - hit->c;

    if (h < 0)
        return false;

    h = sqrtf(h);
    hit->t_near = hit->b - h;
    hit->t_far  = hit->b + h;

    return hit->t_far > 0 && hit->t_near < hit->furthest;
}

INLINE_XPU bool hitSphere(RayHit *hit, vec3 *Ro, vec3 *Rd, u8 flags) {
    f32 t_to_closest = -Ro->dot(*Rd);
    if (t_to_closest <= 0)// Ray is aiming away from the sphere
        return false;

    hit->distance_squared = Ro->squaredLength() - t_to_closest*t_to_closest;

    f32 delta_squared = 1 - hit->distance_squared;
    if (delta_squared <= 0) { // Ray missed the sphere
        hit->distance = t_to_closest;
        return false;
    }
    // Inside the geometry
    f32 delta = sqrtf(delta_squared);

    hit->distance = t_to_closest - delta;
    bool has_outer_hit = hit->distance > 0;
    if (has_outer_hit) {
        hit->position = Rd->scaleAdd(hit->distance, *Ro);
        hit->uv = getUVonUnitSphere(hit->position);
        if (flags & MATERIAL_HAS_TRANSPARENT_UV && isTransparentUV(hit->uv))
            has_outer_hit = false;
    }
    if (!has_outer_hit) {
        hit->distance = t_to_closest + delta;
        hit->position = Rd->scaleAdd(hit->distance, *Ro);
        hit->uv = getUVonUnitSphere(hit->position);
        if (flags & MATERIAL_HAS_TRANSPARENT_UV && isTransparentUV(hit->uv))
            return false;
    }

    hit->from_behind = !has_outer_hit;
    hit->normal = hit->position;
    hit->NdotV = -hit->normal.dot(*Rd);
    hit->area = hit->uv_area = UNIT_SPHERE_AREA_OVER_SIX;

    return true;
}