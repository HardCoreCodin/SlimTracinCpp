#pragma once

#include "../math/vec2.h"
#include "../math/vec3.h"

enum RayIsFacing {
    RayIsFacing_Left = 1,
    RayIsFacing_Down = 2,
    RayIsFacing_Back = 4
};

struct RayHit {
    vec3 position, normal;
    vec2 uv, dUV;
    f32 distance, distance_squared, cone_angle, cone_width, NdotV, area, uv_area;
    u32 geo_id, material_id;
    enum GeometryType geo_type = GeometryType_None;
    bool from_behind = false;

    INLINE_XPU bool plane(const vec3 &plane_origin, const vec3 &plane_normal, const vec3 &ray_origin, const vec3 &ray_direction) {
        f32 NdotRd = plane_normal.dot(ray_direction);
        if (NdotRd == 0) // The ray is parallel to the plane
            return false;

        f32 NdotRoP = plane_normal.dot(plane_origin - ray_origin);
        if (NdotRoP == 0) // The ray originated within the plane
            return false;

        bool ray_is_facing_the_plane = NdotRd < 0;
        from_behind = NdotRoP > 0;
        if (from_behind == ray_is_facing_the_plane) // The ray can't hit the plane
            return false;

        distance = NdotRoP / NdotRd;
        position = ray_direction.scaleAdd(distance, ray_origin);;
        normal = plane_normal;

        return true;
    }
};

struct Ray {
    struct Octant {
        unsigned int x:2;
        unsigned int y:2;
        unsigned int z:2;
    };

    RayHit hit;
    vec3 origin, scaled_origin, direction, direction_reciprocal;
    Octant octant;

    INLINE_XPU vec3 at(f32 t) const { return direction.scaleAdd(t, origin); }
    INLINE_XPU vec3 operator [](f32 t) const { return at(t); }

    INLINE_XPU void prePrepRay() {
        octant.x = signbit(direction.x) ? 3 : 0;
        octant.y = signbit(direction.y) ? 3 : 0;
        octant.z = signbit(direction.z) ? 3 : 0;
        scaled_origin.x = -origin.x * direction_reciprocal.x;
        scaled_origin.y = -origin.y * direction_reciprocal.y;
        scaled_origin.z = -origin.z * direction_reciprocal.z;
    }

    INLINE_XPU bool hitsAABB(const AABB &aabb, f32 closest_distance, f32 *distance) {
        vec3 min_t{*(&aabb.min.x + octant.x), *(&aabb.min.y + octant.y), *(&aabb.min.z + octant.z)};
        vec3 max_t{*(&aabb.max.x - octant.x), *(&aabb.max.y - octant.y), *(&aabb.max.z - octant.z)};
        min_t = min_t.mulAdd(direction_reciprocal, scaled_origin);
        max_t = max_t.mulAdd(direction_reciprocal, scaled_origin);

        max_t.x = max_t.x < max_t.y ? max_t.x : max_t.y;
        max_t.x = max_t.x < max_t.z ? max_t.x : max_t.z;
        max_t.x = max_t.x < closest_distance ? max_t.x : closest_distance;

        min_t.x = min_t.x > min_t.y ? min_t.x : min_t.y;
        min_t.x = min_t.x > min_t.z ? min_t.x : min_t.z;
        min_t.x = min_t.x > 0 ? min_t.x : 0;

        *distance = min_t.x;

        return min_t.x <= max_t.x;
    }

    INLINE_XPU BoxSide hitsCube() {
        u8 ray_is_facing = 0;

        if (signbit(direction.x)) ray_is_facing |= RayIsFacing_Left;
        if (signbit(direction.y)) ray_is_facing |= RayIsFacing_Down;
        if (signbit(direction.z)) ray_is_facing |= RayIsFacing_Back;

        f32 x = ray_is_facing & RayIsFacing_Left ? 1.0f : -1.0f;
        f32 y = ray_is_facing & RayIsFacing_Down ? 1.0f : -1.0f;
        f32 z = ray_is_facing & RayIsFacing_Back ? 1.0f : -1.0f;

        vec3 RD_rcp = 1.0f / direction;
        vec3 Near{(x - origin.x) * RD_rcp.x,
                  (y - origin.y) * RD_rcp.y,
                  (z - origin.z) * RD_rcp.z};
        vec3 Far{(-x - origin.x) * RD_rcp.x,
                 (-y - origin.y) * RD_rcp.y,
                 (-z - origin.z) * RD_rcp.z};

        Axis far_hit_axis;
        f32 far_hit_t = Far.minimum(&far_hit_axis);
        if (far_hit_t < 0) // Further-away hit is behind the ray - intersection can not occur.
            return BoxSide_None;

        Axis near_hit_axis;
        f32 near_hit_t = Near.maximum(&near_hit_axis);
        if (far_hit_t < (near_hit_t > 0 ? near_hit_t : 0))
            return BoxSide_None;

        BoxSide side;
        f32 t = near_hit_t;
        hit.from_behind = t < 0; // Near hit is behind the ray, far hit is in front of it: hit is from behind
        if (hit.from_behind) {
            t = far_hit_t;
            switch (far_hit_axis) {
                case Axis_X : side = ray_is_facing & RayIsFacing_Left ? BoxSide_Left : BoxSide_Right; break;
                case Axis_Y : side = ray_is_facing & RayIsFacing_Down ? BoxSide_Bottom : BoxSide_Top; break;
                case Axis_Z : side = ray_is_facing & RayIsFacing_Back ? BoxSide_Back : BoxSide_Front; break;
            }
        } else {
            switch (near_hit_axis) {
                case Axis_X: side = ray_is_facing & RayIsFacing_Left ? BoxSide_Right : BoxSide_Left; break;
                case Axis_Y: side = ray_is_facing & RayIsFacing_Down ? BoxSide_Top : BoxSide_Bottom; break;
                case Axis_Z: side = ray_is_facing & RayIsFacing_Back ? BoxSide_Front : BoxSide_Back; break;
            }
        }

        hit.position = at(t);
        hit.normal = 0.0f;
        switch (side) {
            case BoxSide_Left  : hit.normal.x = hit.from_behind ?  1.0f : -1.0f; break;
            case BoxSide_Right : hit.normal.x = hit.from_behind ? -1.0f :  1.0f; break;
            case BoxSide_Bottom: hit.normal.y = hit.from_behind ?  1.0f : -1.0f; break;
            case BoxSide_Top   : hit.normal.y = hit.from_behind ? -1.0f :  1.0f; break;
            case BoxSide_Back  : hit.normal.z = hit.from_behind ?  1.0f : -1.0f; break;
            case BoxSide_Front : hit.normal.z = hit.from_behind ? -1.0f :  1.0f; break;
            default: return BoxSide_None;
        }

        return side;
    }

    INLINE_XPU bool hitsPlane(const vec3 &plane_origin, const vec3 &plane_normal) {
        return hit.plane(plane_origin, plane_normal, origin, direction);
    }
};

struct Trace {
    u8 scene_stack_size = 0, mesh_stack_size = 0, depth = 2;
    u32 *scene_stack = nullptr, *mesh_stack = nullptr;
    SphereHit sphere_hit;
    RayHit closest_hit, closest_mesh_hit, current_hit;
    Ray local_space_ray;

    Trace(u8 scene_stack_size, u8 mesh_stack_size = 0, memory::MonotonicAllocator *memory_allocator = nullptr) :
        scene_stack_size{scene_stack_size},
        mesh_stack_size{mesh_stack_size}
    {
        memory::MonotonicAllocator temp_allocator;
        if (!memory_allocator) {
            temp_allocator = memory::MonotonicAllocator{sizeof(u32) * (mesh_stack_size + scene_stack_size)};
            memory_allocator = &temp_allocator;
        }

        mesh_stack = mesh_stack_size ? (u32*)memory_allocator->allocate(sizeof(u32) * mesh_stack_size) : nullptr;
        scene_stack = (u32*)memory_allocator->allocate(sizeof(u32) * scene_stack_size);
    }
};