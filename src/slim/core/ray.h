#pragma once

#include "../math/vec2.h"
#include "../math/vec3.h"

struct RayHit {
    vec3 position, normal;
    UV uv;
    f32 distance, uv_coverage_over_surface_area;
    u32 id;
    bool from_behind = false;

    INLINE_XPU void updateUV(UV uv_repeat) {
        uv *= uv_repeat;
    };
};

struct Ray {
    vec3 origin, scaled_origin, direction, direction_reciprocal;
    Sides faces;
    OctantShifts octant_shifts;

    INLINE_XPU vec3 at(f32 t) const { return direction.scaleAdd(t, origin); }
    INLINE_XPU vec3 operator [](f32 t) const { return at(t); }

    INLINE_XPU void prePrepRay() {
        faces = direction.facing();
        octant_shifts = faces;
        scaled_origin = -origin * direction_reciprocal;
    }

    INLINE_XPU bool hitsAABB(const AABB &aabb, f32 &distance) {
        vec3 min_t{*(&aabb.min.x + octant_shifts.x), *(&aabb.min.y + octant_shifts.y), *(&aabb.min.z + octant_shifts.z)};
        vec3 max_t{*(&aabb.max.x - octant_shifts.x), *(&aabb.max.y - octant_shifts.y), *(&aabb.max.z - octant_shifts.z)};
        min_t = min_t.mulAdd(direction_reciprocal, scaled_origin);
        max_t = max_t.mulAdd(direction_reciprocal, scaled_origin);
        distance = Max(0, min_t.maximum());
        return distance <= max_t.minimum();
    }

    INLINE_XPU bool hitsPlane(const vec3 &plane_origin, const vec3 &plane_normal, RayHit &hit) {
        f32 NdotRd = plane_normal.dot(direction);
        if (NdotRd == 0) // The ray is parallel to the plane
            return false;

        f32 NdotRoP = plane_normal.dot(plane_origin - origin);
        if (NdotRoP == 0) // The ray originated within the plane
            return false;

        bool ray_is_facing_the_plane = NdotRd < 0;
        hit.from_behind = NdotRoP > 0;
        if (hit.from_behind == ray_is_facing_the_plane) // The ray can't hit the plane
            return false;

        f32 t =  NdotRoP / NdotRd;
        if (t > hit.distance)
            return false;

        hit.distance = t;
        hit.position = at(t);
        hit.normal = plane_normal;

        return true;
    }

    INLINE_XPU bool hitsDefaultQuad(RayHit &hit, bool is_transparent = false) {
        if (direction.y == 0) // The ray is parallel to the plane
            return false;

        if (origin.y == 0) // The ray start in the plane
            return false;

        hit.from_behind = origin.y < 0;
        if (hit.from_behind == direction.y < 0) // The ray can't hit the plane
            return false;

        f32 t = fabsf(origin.y * direction_reciprocal.y);
        if (t > hit.distance)
            return false;

        hit.position = at(t);
        if (hit.position.x < -1 ||
            hit.position.x > +1 ||
            hit.position.z < -1 ||
            hit.position.z > +1) {
            return false;
        }

        hit.uv.x = hit.position.x;
        hit.uv.y = hit.position.z;
        hit.uv.shiftToNormalized();

        if (is_transparent && hit.uv.onCheckerboard())
            return false;

        hit.distance = t;
        hit.normal.x = hit.normal.z = 0.0f;
        hit.normal.y = 1.0f;
        hit.uv_coverage_over_surface_area = 0.25f;

        return true;
    }

    INLINE_XPU BoxSide hitsDefaultBox(RayHit &hit, bool is_transparent = false) {
        vec3 signed_rcp{faces};
        signed_rcp *= direction_reciprocal;
        vec3 near_t{scaled_origin - signed_rcp};
        vec3 far_t{scaled_origin + signed_rcp};

        BoxSide side;
        Axis near_axis, far_axis;
        f32 far_hit_t = far_t.minimum(far_axis);
        if (far_hit_t < 0) // Further-away hit is behind the ray - tracers can not occur.
            return BoxSide_None;

        f32 near_hit_t = near_t.maximum(near_axis);
        if (near_hit_t > hit.distance || far_hit_t < (near_hit_t > 0 ? near_hit_t : 0))
            return BoxSide_None;

        f32 t = near_hit_t;
        hit.from_behind = near_hit_t < 0; // Near hit is behind the ray, far hit is in front of it: hit is from behind
        if (hit.from_behind) {
            if (far_hit_t > hit.distance)
                return BoxSide_None;

            t = far_hit_t;
            side = octant_shifts.getBoxSide(far_axis);
        } else
            side = octant_shifts.getBoxSide(near_axis);

        hit.position = at(t);
        hit.uv.setByBoxSide(side, hit.position.x, hit.position.y, hit.position.z);

        if (is_transparent && hit.uv.onCheckerboard()) {
            if (hit.from_behind || far_hit_t > hit.distance)
                return BoxSide_None;

            side = octant_shifts.getBoxSide(far_axis);
            hit.from_behind = true;
            t = far_hit_t;
            hit.position = at(t);
            hit.uv.setByBoxSide(side, hit.position.x, hit.position.y, hit.position.z);
            if (hit.uv.onCheckerboard())
                return BoxSide_None;

        }

        hit.distance = t;
        hit.normal.x = (f32)((i32)(side == BoxSide_Right) - (i32)(side == BoxSide_Left));
        hit.normal.y = (f32)((i32)(side == BoxSide_Top) - (i32)(side == BoxSide_Bottom));
        hit.normal.z = (f32)((i32)(side == BoxSide_Front) - (i32)(side == BoxSide_Back));
        if (hit.from_behind) hit.normal = -hit.normal;
        hit.uv_coverage_over_surface_area = 0.25f;

        return side;
    }

    INLINE_XPU bool hitsDefaultSphere(RayHit &hit, bool is_transparent = false) {
        f32 b = -(direction.dot(origin));
        f32 a = direction.squaredLength();
        f32 c = 1.0f - origin.squaredLength();
        f32 delta_squared = b*b + a*c;
        if (delta_squared < 0)
            return false;

        a = 1.0f / a;
        b *= a;
        f32 delta = sqrtf(delta_squared) * a;

        f32 t = b - delta;
        if (t > hit.distance)
            return false;

        bool has_outer_hit = t > 0;
        if (has_outer_hit) {
            if (is_transparent) {
                hit.normal = at(t);
                hit.uv.setBySphere(hit.normal.x, hit.normal.y, hit.normal.z);
                if (!hit.uv.onCheckerboard())
                    has_outer_hit = false;
            }
        }
        if (!has_outer_hit) {
            t = b + delta;
            if (t <= 0 || t > hit.distance)
                return false;

            if (is_transparent) {
                hit.normal = at(t);
                hit.uv.setBySphere(hit.normal.x, hit.normal.y, hit.normal.z);
                if (!hit.uv.onCheckerboard())
                    return false;
            }
        }

        hit.normal = at(t);
        hit.from_behind = !has_outer_hit;
        hit.distance = t;
        hit.uv_coverage_over_surface_area = UNIT_SPHERE_AREA_OVER_SIX;

        return true;
    }

    INLINE_XPU bool hitsDefaultTetrahedron(RayHit &hit, bool is_transparent = false) {
        mat3 tangent_matrix;
        vec3 tangent_pos;
        vec3 face_normal;
        bool found_triangle = false;

        const f32 _0577 = 0.577350259f;
        const f32 _0288 = 0.288675159f;

        RayHit current_hit;
        current_hit.uv_coverage_over_surface_area = 4.0f / SQRT3;

        for (u8 t = 0; t < 4; t++) {
            tangent_pos = face_normal = vec3{t == 3 ? _0577 : -_0577};
            switch (t) {
                case 0: face_normal.y = _0577; break;
                case 1: face_normal.x = _0577; break;
                case 2: face_normal.z = _0577; break;
                case 3: tangent_pos.y = -_0577; break;
            }

            current_hit.distance = hit.distance;
            if (!hitsPlane(tangent_pos, face_normal, current_hit))
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

            tangent_pos = tangent_matrix * (current_hit.position - tangent_pos);
            if (tangent_pos.x < 0 || tangent_pos.y < 0 || tangent_pos.y + tangent_pos.x > 1)
                continue;

            current_hit.uv.x = tangent_pos.x;
            current_hit.uv.y = tangent_pos.y;

            if (is_transparent && current_hit.uv.onCheckerboard())
                continue;

            if (current_hit.distance < hit.distance) {
                hit = current_hit;
                found_triangle = true;
            }
        }

        return found_triangle;
    }
};