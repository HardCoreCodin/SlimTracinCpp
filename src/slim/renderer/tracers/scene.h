#pragma once

#include "./mesh.h"
#include "../../scene/scene.h"

struct SceneTracer {
    Scene &scene;
    MeshTracer mesh_tracer{nullptr, 0};
    u32 stack_size = 0;
    u32 *stack = nullptr;

    Ray shadow_ray, _temp_ray;
    RayHit shadow_hit, _temp_hit;
    vec3 _closest_hit_ray_direction;

    bool scene_has_emissive_quads = false;

    INLINE_XPU SceneTracer(Scene &scene, u32 *stack, u32 stack_size, u32 *mesh_stack, u32 mesh_stack_size) :
        scene{scene}, mesh_tracer{mesh_stack, mesh_stack_size}, stack{stack}, stack_size{stack_size} {
        scene_has_emissive_quads = _hasEmissiveQuads();
    }

    SceneTracer(Scene &scene, u32 stack_size, u32 mesh_stack_size = 0, memory::MonotonicAllocator *memory_allocator = nullptr) :
        scene{scene}, mesh_tracer{nullptr, 0}, stack_size{stack_size}
    {
        memory::MonotonicAllocator temp_allocator;
        if (!memory_allocator) {
            temp_allocator = memory::MonotonicAllocator{sizeof(u32) * (mesh_stack_size + stack_size)};
            memory_allocator = &temp_allocator;
        }

        stack = (u32*)memory_allocator->allocate(sizeof(u32) * stack_size);
        mesh_tracer = MeshTracer{mesh_stack_size, memory_allocator};
        scene_has_emissive_quads = _hasEmissiveQuads();
    }

    INLINE_XPU bool inShadow(const vec3 &origin, const vec3 &direction, float max_distance = INFINITY) {
        shadow_ray.origin = origin;
        shadow_ray.direction = direction;
        return _trace(shadow_ray, shadow_hit, true, max_distance);
    }

    INLINE_XPU Geometry* findClosestGeometry(Ray &ray, RayHit &hit) {
        Geometry *closest_geometry = _trace(ray, hit);
        if (closest_geometry) finalizeHit(closest_geometry, ray, hit);
        return closest_geometry;
    }

    INLINE_XPU bool hitGeometryInLocalSpace(const Geometry &geo, const Ray &ray, RayHit &hit, bool any_hit = false) {
        _temp_ray.localize(ray, geo.transform);
        _temp_ray.pixel_coords = ray.pixel_coords;
        _temp_ray.depth = ray.depth;
//        if (geo.type == GeometryType_Mesh)
//            return false;
        switch (geo.type) {
            case GeometryType_Quad  : return _temp_ray.hitsDefaultQuad(       hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Box   : return _temp_ray.hitsDefaultBox(        hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Sphere: return _temp_ray.hitsDefaultSphere(     hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Tet   : return _temp_ray.hitsDefaultTetrahedron(hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Mesh  :
                return mesh_tracer.trace(scene.meshes[geo.id], _temp_ray, hit, any_hit);
            default: return false;
        }
    }

    INLINE_XPU void finalizeHit(Geometry *geometry, Ray &ray, RayHit &hit) {
        const vec2 &uv_repeat{scene.materials[geometry->material_id].uv_repeat};
        hit.uv *= uv_repeat;
        if (hit.from_behind) hit.normal = -hit.normal;

        // Compute uvs and uv-coverage using Ray Cones:
        // Note: This is done while the hit is still in LOCAL space and using its LOCAL and PRE-NORMALIZED ray direction
        hit.cone_width = hit.distance * hit.cone_width_scaling_factor;
        hit.uv_coverage *= hit.cone_width * hit.cone_width / _closest_hit_ray_direction.squaredLength();;
        hit.uv_coverage /= -(hit.normal.dot(_closest_hit_ray_direction)) * uv_repeat.u * uv_repeat.v;

        // Convert Ray Hit to world space, using the "t" value from the local-space trace:
        hit.position = ray[hit.distance];
        hit.normal = geometry->transform.externDir(hit.normal); // Normalized
    }

protected:

    Geometry* _trace(Ray &ray, RayHit &hit, bool any_hit = false, f32 max_distance = INFINITY) {
        ray.reset(ray.direction.scaleAdd(TRACE_OFFSET, ray.origin), ray.direction);
        hit.distance = max_distance;

        bool hit_left, hit_right;
        f32 left_near_distance, right_near_distance, left_far_distance, right_far_distance;
        if (!(ray.hitsAABB(scene.bvh.nodes->aabb, left_near_distance, left_far_distance) && left_near_distance < hit.distance))
            return nullptr;

        u32 *indices = scene.bvh_leaf_geometry_indices;
        if (unlikely(scene.bvh.nodes->leaf_count))
            return _hitGeometries(indices, scene.counts.geometries, left_far_distance, ray, hit, any_hit);

        BVHNode *left_node = scene.bvh.nodes + scene.bvh.nodes->first_index;
        BVHNode *right_node, *tmp_node;
        Geometry *hit_geo, *closest_hit_geo = nullptr;
        u32 top = 0;

        while (true) {
            right_node = left_node + 1;

            hit_left  = ray.hitsAABB(left_node->aabb, left_near_distance, left_far_distance) && left_near_distance < hit.distance;
            hit_right = ray.hitsAABB(right_node->aabb, right_near_distance, right_far_distance) && right_near_distance < hit.distance;

            if (hit_left) {
                if (unlikely(left_node->leaf_count)) {
                    hit_geo = _hitGeometries(indices + left_node->first_index, left_node->leaf_count, left_far_distance, ray, hit, any_hit);
                    if (hit_geo) {
                        closest_hit_geo = hit_geo;
                        if (any_hit)
                            break;
                    }

                    left_node = nullptr;
                }
            } else
                left_node = nullptr;

            if (hit_right) {
                if (unlikely(right_node->leaf_count)) {
                    hit_geo = _hitGeometries(indices + right_node->first_index, right_node->leaf_count, right_far_distance, ray, hit, any_hit);
                    if (hit_geo) {
                        closest_hit_geo = hit_geo;
                        if (any_hit)
                            break;
                    }
                    right_node = nullptr;
                }
            } else
                right_node = nullptr;

            if (left_node) {
                if (right_node) {
                    if (!any_hit && left_near_distance > right_near_distance) {
                        tmp_node = left_node;
                        left_node = right_node;
                        right_node = tmp_node;
                    }
                    stack[top++] = right_node->first_index;
                }
                left_node = scene.bvh.nodes + left_node->first_index;
            } else if (right_node) {
                left_node = scene.bvh.nodes + right_node->first_index;
            } else {
                if (top == 0) break;
                left_node = scene.bvh.nodes + stack[--top];
            }
        }

        return closest_hit_geo;
    }

    Geometry* _hitGeometries(const u32 *geometry_indices, u32 geo_count, f32 closest_distance, const Ray &ray, RayHit &hit, bool any_hit) {
        Geometry *geo, *hit_geo = nullptr;
        u8 visibility_flag = any_hit ? GEOMETRY_IS_SHADOWING : GEOMETRY_IS_VISIBLE;

        _temp_hit.distance = hit.distance;

        for (u32 i = 0; i < geo_count; i++) {
            geo = scene.geometries + geometry_indices[i];

            if (!(geo->flags & visibility_flag))
                continue;

            if (hitGeometryInLocalSpace(*geo, ray, _temp_hit, any_hit)) {
                if (any_hit)
                    return geo;

                if (_temp_hit.distance < hit.distance) {
                    hit_geo = geo;
                    hit = _temp_hit;
                    _closest_hit_ray_direction = _temp_ray.direction;
                }
            }
        }

        return hit_geo;
    }

    INLINE_XPU bool _hasEmissiveQuads() {
        for (u32 i = 0; i < scene.counts.geometries; i++)
            if (scene.geometries[i].type == GeometryType_Quad &&
                scene.materials[scene.geometries[i].material_id].isEmissive())
                return true;

        return false;
    }
};