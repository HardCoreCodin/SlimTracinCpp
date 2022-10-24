#pragma once

#include "../common.h"

INLINE_XPU BoxSide hitBox(RayHit *hit, vec3 *Ro, vec3 *Rd, u8 flags) {
    vec3 octant, RD_rcp = 1.0f / *Rd;
    octant.x = signbit(Rd->x) ? 1.0f : -1.0f;
    octant.y = signbit(Rd->y) ? 1.0f : -1.0f;
    octant.z = signbit(Rd->z) ? 1.0f : -1.0f;

    f32 t[6];
    t[0] = ( octant.x - Ro->x) * RD_rcp.x;
    t[1] = ( octant.y - Ro->y) * RD_rcp.y;
    t[2] = ( octant.z - Ro->z) * RD_rcp.z;
    t[3] = (-octant.x - Ro->x) * RD_rcp.x;
    t[4] = (-octant.y - Ro->y) * RD_rcp.y;
    t[5] = (-octant.z - Ro->z) * RD_rcp.z;

    u8 max_axis = t[3] < t[4] ? 3 : 4; if (t[5] < t[max_axis]) max_axis = 5;
    f32 maxt = t[max_axis];
    if (maxt < 0) // Further-away hit is behind the ray - intersection can not occur.
        return BoxSide_None;

    u8 min_axis = t[0] > t[1] ? 0 : 1; if (t[2] > t[min_axis]) min_axis = 2;
    f32 mint = t[min_axis];
    if (maxt < (mint > 0 ? mint : 0))
        return BoxSide_None;

    hit->from_behind = mint < 0; // Further-away hit is in front of the ray, closer one is behind it.
    if (hit->from_behind) {
        mint = maxt;
        min_axis = max_axis;
    }

    BoxSide side = getBoxSide(octant, min_axis);
    hit->position = Rd->scaleAdd(mint, *Ro);
    hit->uv = getUVonUnitCube(hit->position, side);

    if (flags & MATERIAL_HAS_TRANSPARENT_UV && isTransparentUV(hit->uv)) {
        if (hit->from_behind)
            return BoxSide_None;

        side = getBoxSide(octant, max_axis);
        hit->position = Rd->scaleAdd(maxt, *Ro);
        hit->uv = getUVonUnitCube(hit->position, side);
        if (isTransparentUV(hit->uv))
            return BoxSide_None;

        hit->from_behind = true;
    }

    hit->normal = vec3{0.0f};
    switch (side) {
        case BoxSide_Left:   hit->normal.x = -1.0f; hit->NdotV = +Rd->x; break;
        case BoxSide_Right:  hit->normal.x = +1.0f; hit->NdotV = -Rd->x; break;
        case BoxSide_Bottom: hit->normal.y = -1.0f; hit->NdotV = +Rd->y; break;
        case BoxSide_Top:    hit->normal.y = +1.0f; hit->NdotV = -Rd->y; break;
        case BoxSide_Back:   hit->normal.z = -1.0f; hit->NdotV = +Rd->z; break;
        case BoxSide_Front:  hit->normal.z = +1.0f; hit->NdotV = -Rd->z; break;
        default: return BoxSide_None;
    }
    if (hit->from_behind) {
        hit->normal = -hit->normal;
        hit->NdotV = -hit->NdotV;
    }

    hit->area = 4;
    hit->uv_area = 1;

    return side;
}