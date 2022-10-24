#pragma once

#include "./quad.h"

#define _0577 0.577350259f
#define _0288 0.288675159f

INLINE_XPU bool hitTetrahedron(RayHit *closest_hit, vec3 *Ro, vec3 *Rd, u8 flags) {
    mat3 tangent_matrix;
    vec3 tangent_pos;
    bool found_triangle = false;
    f32 closest_distance = INFINITY;
    RayHit hit;
    hit.cone_angle = 0;

    for (u8 t = 0; t < 4; t++) {
        tangent_pos = hit.normal = vec3{t == 3 ? _0577 : -_0577};
        switch (t) {
            case 0: hit.normal.y = _0577; break;
            case 1: hit.normal.x = _0577; break;
            case 2: hit.normal.z = _0577; break;
            case 3: tangent_pos.y = -_0577; break;
        }

        if (!hit.plane(tangent_pos, hit.normal, *Ro, *Rd))
            continue;

        switch (t) {
            case 0:
                tangent_matrix.X.x = _0577;
                tangent_matrix.X.y = -_0288;
                tangent_matrix.X.z = -_0577;

                tangent_matrix.Y.x = _0288;
                tangent_matrix.Y.y = _0288;
                tangent_matrix.Y.z = _0577;

                tangent_matrix.Z.x = -_0288;
                tangent_matrix.Z.y = _0577;
                tangent_matrix.Z.z = -_0577;
                break;
            case 1:
                tangent_matrix.X.x = _0288;
                tangent_matrix.X.y = _0288;
                tangent_matrix.X.z = _0577;

                tangent_matrix.Y.x = -_0288;
                tangent_matrix.Y.y = _0577;
                tangent_matrix.Y.z = -_0577;

                tangent_matrix.Z.x = _0577;
                tangent_matrix.Z.y = -_0288;
                tangent_matrix.Z.z = -_0577;
                break;
            case 2:
                tangent_matrix.X.x = -_0288;
                tangent_matrix.X.y = _0577;
                tangent_matrix.X.z = -_0577;

                tangent_matrix.Y.x = _0577;
                tangent_matrix.Y.y = -_0288;
                tangent_matrix.Y.z = -_0577;

                tangent_matrix.Z.x = _0288;
                tangent_matrix.Z.y = _0288;
                tangent_matrix.Z.z = _0577;
                break;
            case 3:
                tangent_matrix.X.x = -_0577;
                tangent_matrix.X.y = _0288;
                tangent_matrix.X.z = _0577;

                tangent_matrix.Y.x = _0288;
                tangent_matrix.Y.y = _0288;
                tangent_matrix.Y.z = _0577;

                tangent_matrix.Z.x = _0288;
                tangent_matrix.Z.y = -_0577;
                tangent_matrix.Z.z = _0577;
                break;
        }

        hit.position = Rd->scaleAdd(hit.distance, *Ro);
        tangent_pos = tangent_matrix * (hit.position - tangent_pos);

        if (tangent_pos.x < 0 || tangent_pos.y < 0 || tangent_pos.y + tangent_pos.x > 1)
            continue;

        hit.uv.x = tangent_pos.x;
        hit.uv.y = tangent_pos.y;

        if ((flags & MATERIAL_HAS_TRANSPARENT_UV) && isTransparentUV(hit.uv))
            continue;

        if (hit.distance < closest_distance) {
            hit.area = SQRT3 / 4.0f;
            hit.uv_area = 1;
            hit.NdotV = -hit.normal.dot(*Rd);
            closest_distance = hit.distance;
            *closest_hit = hit;
            found_triangle = true;
        }
    }

    return found_triangle;
}