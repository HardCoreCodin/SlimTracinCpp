#pragma once

#include "../math/vec2.h"
#include "../math/vec3.h"

struct RayHit {
    vec3 position, normal;
    UV uv;
    f32 distance, distance_squared, NdotV, uv_coverage_over_surface_area, uv_coverage;
    u32 geo_id, material_id;
    enum GeometryType geo_type = GeometryType_None;
    bool from_behind = false;

    INLINE_XPU void updateUVCoverage(f32 pixel_area_over_focal_length_squared) {
        uv_coverage = uv_coverage_over_surface_area * pixel_area_over_focal_length_squared * distance_squared;
        uv_coverage /= NdotV;
    };

    INLINE_XPU void updateUV(UV uv_repeat) {
        uv *= uv_repeat;
        uv_coverage /= uv_repeat.u * uv_repeat.v;
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

    INLINE_XPU bool hitsAABB(const AABB &aabb, f32 closest_distance, f32 &distance) {
        vec3 min_t{*(&aabb.min.x + octant_shifts.x), *(&aabb.min.y + octant_shifts.y), *(&aabb.min.z + octant_shifts.z)};
        vec3 max_t{*(&aabb.max.x - octant_shifts.x), *(&aabb.max.y - octant_shifts.y), *(&aabb.max.z - octant_shifts.z)};
        min_t = min_t.mulAdd(direction_reciprocal, scaled_origin);
        max_t = max_t.mulAdd(direction_reciprocal, scaled_origin);

        max_t.x = max_t.x < max_t.y ? max_t.x : max_t.y;
        max_t.x = max_t.x < max_t.z ? max_t.x : max_t.z;
        max_t.x = max_t.x < closest_distance ? max_t.x : closest_distance;

        min_t.x = min_t.x > min_t.y ? min_t.x : min_t.y;
        min_t.x = min_t.x > min_t.z ? min_t.x : min_t.z;
        min_t.x = min_t.x > 0 ? min_t.x : 0;

        distance = min_t.x;

        return min_t.x <= max_t.x;
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

        hit.distance = NdotRoP / NdotRd;
        hit.distance_squared = hit.distance * hit.distance;
        hit.position = at(hit.distance);
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

        hit.distance = fabsf(origin.y * direction_reciprocal.y);
        hit.position = at(hit.distance);

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

        hit.normal = {0, 1, 0};
        hit.NdotV = -direction.y;
        hit.uv_coverage_over_surface_area = 0.25f;
        hit.distance_squared = hit.distance * hit.distance;

        return true;
    }

    INLINE_XPU BoxSide hitsDefaultBox(RayHit &hit, bool is_transparent = false) {
        vec3 signed_rcp{faces};
        signed_rcp *= direction_reciprocal;
        vec3 Near{scaled_origin - signed_rcp};
        vec3 Far{scaled_origin + signed_rcp};

        BoxSide near_side, far_side, side;
        f32 far_hit_t = Far.minimum(&far_side);
        if (far_hit_t < 0) // Further-away hit is behind the ray - tracers can not occur.
            return BoxSide_None;

        f32 near_hit_t = Near.maximum(&near_side);
        if (far_hit_t < (near_hit_t > 0 ? near_hit_t : 0))
            return BoxSide_None;

        hit.distance = near_hit_t;
        hit.from_behind = near_hit_t < 0; // Near hit is behind the ray, far hit is in front of it: hit is from behind
        if (hit.from_behind) {
            hit.distance = far_hit_t;
            side = far_side;
        } else
            side = near_side;

        hit.position = at(hit.distance);
        hit.uv.setByBoxSide(side, hit.position.x, hit.position.y, hit.position.z);

        if (is_transparent && hit.uv.onCheckerboard()) {
            if (hit.from_behind)
                return BoxSide_None;

            hit.from_behind = true;
            side = far_side;
            hit.distance = far_hit_t;
            hit.position = at(far_hit_t);
            hit.uv.setByBoxSide(side, hit.position.x, hit.position.y, hit.position.z);
            if (hit.uv.onCheckerboard())
                return BoxSide_None;

        }

        hit.normal.x = side == BoxSide_Right ? 1.0f : 0.0f;
        hit.normal.y = side == BoxSide_Top ? 1.0f : 0.0f;
        hit.normal.z = side == BoxSide_Front ? 1.0f : 0.0f;
        hit.NdotV = -hit.normal.dot(direction);
        hit.distance_squared = hit.distance * hit.distance;
        hit.uv_coverage_over_surface_area = 0.25f;

        return side;
    }

    INLINE_XPU bool hitsDefaultSphere(RayHit &hit, bool is_transparent = false) {
        f32 t_to_closest = -origin.dot(direction);
        if (t_to_closest <= 0)// Ray is aiming away from the sphere
            return false;

        hit.distance_squared = origin.squaredLength() - t_to_closest*t_to_closest;

        f32 delta_squared = 1 - hit.distance_squared;
        if (delta_squared <= 0) { // Ray missed the sphere
            hit.distance = t_to_closest;
            return false;
        }
        // Inside the geometry
        f32 delta = sqrtf(delta_squared);

        hit.distance = t_to_closest - delta;
        bool has_outer_hit = hit.distance > 0;
        if (has_outer_hit) {
            hit.position = at(hit.distance);
            if (is_transparent) {
                hit.uv.setBySphere(hit.position.x, hit.position.y, hit.position.z);
                if (!hit.uv.onCheckerboard())
                    has_outer_hit = false;
            }
        }
        if (!has_outer_hit) {
            hit.distance = t_to_closest + delta;
            hit.position = at(hit.distance);
            if (is_transparent) {
                hit.uv.setBySphere(hit.position.x, hit.position.y, hit.position.z);
                if (!hit.uv.onCheckerboard())
                    return false;
            }
        }

        hit.from_behind = !has_outer_hit;
        hit.normal = hit.position;
        hit.NdotV = -hit.normal.dot(direction);
        hit.uv_coverage_over_surface_area = UNIT_SPHERE_AREA_OVER_SIX;

        return true;
    }

    INLINE_XPU bool hitsDefaultTetrahedron(RayHit &hit, bool is_transparent = false) {
        mat3 tangent_matrix;
        vec3 tangent_pos;
        vec3 face_normal;
        bool found_triangle = false;
        f32 closest_distance = INFINITY;

        const f32 _0577 = 0.577350259f;
        const f32 _0288 = 0.288675159f;

        RayHit closest_hit;

        for (u8 t = 0; t < 4; t++) {
            tangent_pos = face_normal = vec3{t == 3 ? _0577 : -_0577};
            switch (t) {
                case 0: face_normal.y = _0577; break;
                case 1: face_normal.x = _0577; break;
                case 2: face_normal.z = _0577; break;
                case 3: tangent_pos.y = -_0577; break;
            }

            if (!hitsPlane(tangent_pos, face_normal, hit))
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

            tangent_pos = tangent_matrix * (hit.position - tangent_pos);
            if (tangent_pos.x < 0 || tangent_pos.y < 0 || tangent_pos.y + tangent_pos.x > 1)
                continue;

            hit.uv.x = tangent_pos.x;
            hit.uv.y = tangent_pos.y;

            if (is_transparent && hit.uv.onCheckerboard())
                continue;

            if (hit.distance < closest_distance) {
                closest_distance = hit.distance;
                closest_hit = hit;
                closest_hit.uv_coverage_over_surface_area = 4.0f / SQRT3;
                closest_hit.NdotV = -hit.normal.dot(direction);
                found_triangle = true;
            }
        }

        if (found_triangle)
            hit = closest_hit;

        return found_triangle;
    }
};