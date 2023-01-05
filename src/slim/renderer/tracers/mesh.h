#pragma once

#include "../../core/ray.h"
#include "../../scene/mesh.h"

struct MeshTracer {
    u32 *stack = nullptr;
    u32 stack_size = 0;

    mutable RayHit triangle_hit;

    INLINE_XPU MeshTracer(u32 *stack, u32 stack_size) : stack{stack}, stack_size{stack_size} {}
    explicit MeshTracer(u32 stack_size, memory::MonotonicAllocator *memory_allocator = nullptr) : stack_size{stack_size} {
        memory::MonotonicAllocator temp_allocator;
        if (!memory_allocator) {
            temp_allocator = memory::MonotonicAllocator{sizeof(u32) * stack_size};
            memory_allocator = &temp_allocator;
        }

        stack = stack_size ? (u32*)memory_allocator->allocate(sizeof(u32) * stack_size) : nullptr;
    }

    INLINE_XPU bool hitTriangles(Triangle *triangles, u32 triangle_count, const Ray &ray, RayHit &hit, bool any_hit) const {
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
                hit.id = i;
                hit.uv_coverage = triangle->area_of_uv / triangle->area_of_parallelogram;

                found_triangle = true;

                if (any_hit)
                    break;
            }
        }

        return found_triangle;
    }

    INLINE_XPU bool trace(const Mesh &mesh, const Ray &ray, RayHit &hit, bool any_hit) {
        bool hit_left, hit_right, found = false;
        f32 left_distance, right_distance;

        if (!(ray.hitsAABB(mesh.bvh.nodes->aabb, left_distance) && left_distance < hit.distance))
            return false;

        if (unlikely(mesh.bvh.nodes->leaf_count))
            return hitTriangles(mesh.triangles, mesh.triangle_count, ray, hit, any_hit);

        BVHNode *left_node = mesh.bvh.nodes + mesh.bvh.nodes->first_index;
        BVHNode *right_node, *tmp_node;
        u32 top = 0;

        while (true) {
            right_node = left_node + 1;

            hit_left  = ray.hitsAABB(left_node->aabb, left_distance) && left_distance < hit.distance;
            hit_right = ray.hitsAABB(right_node->aabb, right_distance) && right_distance < hit.distance;

            if (hit_left) {
                if (unlikely(left_node->leaf_count)) {
                    if (hitTriangles(mesh.triangles + left_node->first_index, left_node->leaf_count, ray, hit, any_hit)) {
                        hit.id += left_node->first_index;
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
                        hit.id += right_node->first_index;
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

        if (found && !any_hit && mesh.normals_count | mesh.uvs_count) {
            u32 triangle_index = hit.id;
            TriangleVertexIndices ids;
            f32 u = hit.uv.u;
            f32 v = hit.uv.v;
            f32 w = 1 - u - v;
            if (mesh.uvs_count) {
                ids = mesh.vertex_uvs_indices[triangle_index];
                vec2 &UV1 = mesh.vertex_uvs[ids.v1];
                vec2 &UV2 = mesh.vertex_uvs[ids.v2];
                vec2 &UV3 = mesh.vertex_uvs[ids.v3];
                hit.uv.x = fast_mul_add(UV3.x, u, fast_mul_add(UV2.u, v, UV1.u * w));
                hit.uv.y = fast_mul_add(UV3.y, u, fast_mul_add(UV2.v, v, UV1.v * w));
            }
            if (mesh.normals_count) {
                ids = mesh.vertex_normal_indices[triangle_index];
                vec3 &N1 = mesh.vertex_normals[ids.v1];
                vec3 &N2 = mesh.vertex_normals[ids.v2];
                vec3 &N3 = mesh.vertex_normals[ids.v3];
                hit.normal.x = fast_mul_add(N3.x, u, fast_mul_add(N2.x, v, N1.x * w));
                hit.normal.y = fast_mul_add(N3.y, u, fast_mul_add(N2.y, v, N1.y * w));
                hit.normal.z = fast_mul_add(N3.z, u, fast_mul_add(N2.z, v, N1.z * w));
            }
        }

        return found;
    }
};