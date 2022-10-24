#pragma once

#include "../common.h"
#include "../../scene/scene.h"

INLINE_XPU bool hitQuad(RayHit *hit, vec3 *Ro, vec3 *Rd, u8 flags) {
    if (Rd->y == 0) // The ray is parallel to the plane
        return false;

    if (Ro->y == 0) // The ray start in the plane
        return false;

    hit->from_behind = Ro->y < 0;
    if (hit->from_behind == Rd->y < 0) // The ray can't hit the plane
        return false;

    hit->distance = fabsf(Ro->y / Rd->y);
    hit->position = Rd->scaleAdd(hit->distance, *Ro);

    if (hit->position.x < -1 ||
        hit->position.x > +1 ||
        hit->position.z < -1 ||
        hit->position.z > +1) {
        return false;
    }

    hit->uv.x = (hit->position.x + 1.0f) * 0.5f;
    hit->uv.y = (hit->position.z + 1.0f) * 0.5f;

    if (flags & MATERIAL_HAS_TRANSPARENT_UV && isTransparentUV(hit->uv))
        return false;

    hit->normal.x = 0;
    hit->normal.y = 1;
    hit->normal.z = 0;
    hit->NdotV = -Rd->y;
    hit->area = 4;
    hit->uv_area = 1;

    return true;
}

INLINE_XPU bool hitEmissiveQuads(Ray *ray, Trace *trace, Scene *scene) {
    bool found = false;
    vec3 *Rd = &trace->local_space_ray.direction;
    vec3 *Ro = &trace->local_space_ray.origin;

    RayHit *hit = &trace->current_hit;
    RayHit *closest_hit = &trace->closest_hit;

    Geometry *geometry = scene->geometries;
    for (u32 i = 0; i < scene->counts.geometries; i++, geometry++) {
        if (!(geometry->type == GeometryType_Quad && scene->materials[geometry->material_id].isEmissive()))
            continue;

        geometry->transform.internPosAndDir(ray->origin, ray->direction, *Ro, *Rd);
        *Ro = Rd->scaleAdd( TRACE_OFFSET, *Ro);

        if (hitQuad(hit, Ro, Rd, geometry->flags)) {
            hit->position         = geometry->transform.externPos(hit->position);
            hit->distance_squared = (hit->position - ray->origin).squaredLength();
            if (hit->distance_squared < closest_hit->distance_squared) {
                *closest_hit = *hit;
                closest_hit->geo_id = i;
                closest_hit->geo_type = GeometryType_Quad;
                closest_hit->material_id = geometry->material_id;

                found = true;
            }
        }
    }

    if (found) {
        closest_hit->normal = scene->geometries[closest_hit->geo_id].transform.externPos(closest_hit->normal).normalized();
        closest_hit->distance = sqrtf(closest_hit->distance_squared);
    }

    return found;
}