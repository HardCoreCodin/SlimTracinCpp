#pragma once

#include "../../core/ray.h"
#include "../../scene/mesh.h"

struct MeshTracer {
    u32 *stack = nullptr;
    u32 stack_size = 0;
    RayHit triangle_hit;

    INLINE_XPU MeshTracer(u32 *stack, u32 stack_size) : stack{stack}, stack_size{stack_size} {}
    explicit MeshTracer(u32 stack_size, memory::MonotonicAllocator *memory_allocator = nullptr) : stack_size{stack_size} {
        memory::MonotonicAllocator temp_allocator;
        if (!memory_allocator) {
            temp_allocator = memory::MonotonicAllocator{sizeof(u32) * stack_size};
            memory_allocator = &temp_allocator;
        }

        stack = stack_size ? (u32*)memory_allocator->allocate(sizeof(u32) * stack_size) : nullptr;
    }

    INLINE_XPU bool hitTriangles(Triangle *triangles, u32 triangle_count, Ray &ray, RayHit &hit, bool any_hit) {
        vec3 UV;
        bool found_triangle = false;
        Triangle *triangle = triangles;
        for (u32 i = 0; i < triangle_count; i++, triangle++) {
            if (ray.hitsPlane(triangle->position, triangle->normal, triangle_hit) &&
                triangle_hit.distance < hit.distance) {

                UV = triangle->local_to_tangent * (triangle_hit.position - triangle->position);
                if (UV.x < 0 || UV.y < 0 || (UV.x + UV.y) > 1)
                    continue;

                hit = triangle_hit;
                hit.uv.x = UV.x;
                hit.uv.y = UV.y;
                hit.geo_id = i;
                hit.uv_coverage_over_surface_area = triangle->area_of_uv / triangle->area_of_parallelogram;
                hit.NdotV = -hit.normal.dot(ray.direction);

                found_triangle = true;

                if (any_hit)
                    break;
            }
        }

        return found_triangle;
    }

    INLINE_XPU bool trace(const Mesh &mesh, Ray &ray, RayHit &hit, bool any_hit) {
        bool hit_left, hit_right, found = false;
        f32 left_distance, right_distance;

        if (!ray.hitsAABB(mesh.bvh.nodes->aabb, hit.distance, left_distance))
            return false;

        if (unlikely(mesh.bvh.nodes->leaf_count))
            return hitTriangles(mesh.triangles, mesh.triangle_count, ray, hit, any_hit);

        BVHNode *left_node = mesh.bvh.nodes + mesh.bvh.nodes->first_index;
        BVHNode *right_node, *tmp_node;
        u32 top = 0;

        while (true) {
            right_node = left_node + 1;

            hit_left  = ray.hitsAABB(left_node->aabb,  hit.distance, left_distance);
            hit_right = ray.hitsAABB(right_node->aabb, hit.distance, right_distance);

            if (hit_left) {
                if (unlikely(left_node->leaf_count)) {
                    if (hitTriangles(mesh.triangles + left_node->first_index, left_node->leaf_count, ray, hit, any_hit)) {
                        hit.geo_id += left_node->first_index;
                        found = true;
                        if (any_hit)
                            break;
                    }

                    left_node = nullptr;
                }
            } else
                left_node = nullptr;

            if (hit_right) {
                if (unlikely(right_node->leaf_count)) {
                    if (hitTriangles(mesh.triangles + right_node->first_index, right_node->leaf_count, ray, hit, any_hit)) {
                        hit.geo_id += right_node->first_index;
                        found = true;
                        if (any_hit)
                            break;
                    }

                    right_node = nullptr;
                }
            } else
                right_node = nullptr;

            if (left_node) {
                if (right_node) {
                    if (!any_hit && left_distance > right_distance) {
                        tmp_node = left_node;
                        left_node = right_node;
                        right_node = tmp_node;
                    }
                    stack[top++] = right_node->first_index;
                    if (top == stack_size)
                        return false;
                }
                left_node = mesh.bvh.nodes + left_node->first_index;
            } else if (right_node) {
                left_node = mesh.bvh.nodes + right_node->first_index;
            } else {
                if (top == 0) break;
                left_node = mesh.bvh.nodes + stack[--top];
            }
        }

        return found;
    }
};