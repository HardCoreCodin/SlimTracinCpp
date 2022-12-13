#pragma once

#include "./mesh.h"

struct SceneTracer {
    const Scene &scene;
    MeshTracer mesh_tracer{nullptr, 0};
    u32 stack_size = 0;
    u32 *stack = nullptr;
    Ray local_ray, shadow_ray;
    RayHit local_hit, shadow_hit;
    f32 pixel_area_over_focal_length_squared;
    bool scene_has_emissive_quads = false;

    INLINE_XPU SceneTracer(const Scene &scene, u32 *stack, u32 stack_size, u32 *mesh_stack, u32 mesh_stack_size) :
        scene{scene}, mesh_tracer{mesh_stack, mesh_stack_size}, stack{stack}, stack_size{stack_size} {
        scene_has_emissive_quads = _hasEmissiveQuads();
    }

    SceneTracer(const Scene &scene, u32 stack_size, u32 mesh_stack_size = 0, memory::MonotonicAllocator *memory_allocator = nullptr) :
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

    INLINE_XPU bool _hasEmissiveQuads() {
        for (u32 i = 0; i < scene.counts.geometries; i++)
            if (scene.geometries[i].type == GeometryType_Quad &&
                scene.materials[scene.geometries[i].material_id].isEmissive())
                return true;

        return false;
    }

    INLINE_XPU bool inShadow(const vec3 &origin, const vec3 &direction, float max_distance = INFINITY, float max_distance_squared = INFINITY) {
        shadow_hit.distance = max_distance;
        shadow_hit.distance_squared = max_distance_squared;
        shadow_ray.origin = origin;
        shadow_ray.direction = direction;
        return trace(shadow_ray, shadow_hit, true);
    }

    INLINE_XPU bool trace(Ray &ray, RayHit &hit, bool any_hit = false) {
        if (!any_hit) hit.distance = hit.distance_squared = INFINITY;
        ray.direction_reciprocal = 1.0f / ray.direction;
        ray.prePrepRay();

        bool hit_left, hit_right, found = false;
        f32 left_distance, right_distance;

        if (!ray.hitsAABB(scene.bvh.nodes->aabb, hit.distance, left_distance))
            return false;

        u32 *indices = scene.bvh_leaf_geometry_indices;
        if (unlikely(scene.bvh.nodes->leaf_count))
            return hitGeometries(indices, scene.counts.geometries, ray, hit, any_hit, false, 0, 0);

        BVHNode *left_node = scene.bvh.nodes + scene.bvh.nodes->first_index;
        BVHNode *right_node, *tmp_node;
        u32 top = 0;

        while (true) {
            right_node = left_node + 1;

            hit_left  = ray.hitsAABB(left_node->aabb, hit.distance, left_distance);
            hit_right = ray.hitsAABB(right_node->aabb, hit.distance, right_distance);

            if (hit_left) {
                if (unlikely(left_node->leaf_count)) {
                    if (hitGeometries(indices + left_node->first_index, left_node->leaf_count, ray, hit, any_hit, false, 0, 0)) {
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
                    if (hitGeometries(indices + right_node->first_index, right_node->leaf_count, ray, hit, any_hit, false, 0, 0)) {
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
                }
                left_node = scene.bvh.nodes + left_node->first_index;
            } else if (right_node) {
                left_node = scene.bvh.nodes + right_node->first_index;
            } else {
                if (top == 0) break;
                left_node = scene.bvh.nodes + stack[--top];
            }
        }

        return found;
    }

    INLINE_XPU bool hitGeometries(const u32 *geometry_indices, u32 geo_count, Ray &ray, RayHit &hit, bool any_hit, bool check_visibility, u16 x, u16 y) {
        bool current_found, found = false;
        u32 hit_geometry_index, geometry_index;;
        for (u32 i = 0; i < geo_count; i++) {
            geometry_index = geometry_indices[i];
            Geometry &geo{scene.geometries[geometry_index]};

            if (any_hit && !(geo.flags & GEOMETRY_IS_SHADOWING))
                continue;

            if (check_visibility) {
                if (!(geo.flags & GEOMETRY_IS_VISIBLE))
                    continue;

                if (x <   geo.screen_bounds.left ||
                    x >=  geo.screen_bounds.right ||
                    y <   geo.screen_bounds.top ||
                    y >=  geo.screen_bounds.bottom)
                    continue;
            }

            geo.transform.internPosAndDir(ray.origin, ray.direction, local_ray.origin, local_ray.direction);
            local_ray.origin = local_ray.direction.scaleAdd(TRACE_OFFSET, local_ray.origin);
            if (geo.type == GeometryType_Mesh || geo.type == GeometryType_Box || geo.type == GeometryType_Quad) {
                local_ray.direction_reciprocal = 1.0f / local_ray.direction;
                if (geo.type != GeometryType_Quad) local_ray.prePrepRay();
            }

            switch (geo.type) {
                case GeometryType_Quad       : current_found = local_ray.hitsDefaultQuad(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT); break;
                case GeometryType_Box        : current_found = local_ray.hitsDefaultBox(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT); break;
                case GeometryType_Sphere     : current_found = local_ray.hitsDefaultSphere(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT); break;
                case GeometryType_Tetrahedron: current_found = local_ray.hitsDefaultTetrahedron(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT); break;
                case GeometryType_Mesh       :
                    local_hit.distance = local_hit.distance == INFINITY ? INFINITY : geo.transform.internPos(hit.position - local_ray.origin).length();
                    current_found = mesh_tracer.trace(scene.meshes[geo.id], local_ray, local_hit, any_hit);
                    break;
                default: continue;
            }

            if (current_found) {
                local_hit.updateUVCoverage(pixel_area_over_focal_length_squared);
                local_hit.position = geo.transform.externPos(local_hit.position);
                local_hit.distance_squared = (local_hit.position - ray.origin).squaredLength();
                if (local_hit.distance_squared < hit.distance_squared) {
                    if (any_hit)
                        return true;

                    hit_geometry_index = geometry_index;
                    hit = local_hit;
                    found = true;
                }
            }
        }

        if (found) {
            Geometry &hit_geometry{scene.geometries[hit_geometry_index]};
            if (hit.geo_type == GeometryType_Mesh) {
                Mesh &mesh{scene.meshes[hit_geometry.id]};
                if (mesh.normals_count | mesh.uvs_count) {
                    u32 triangle_index = hit.geo_id;
                    TriangleVertexIndices ids;
                    f32 u = hit.uv.u;
                    f32 v = hit.uv.v;
                    f32 w = 1 - u - v;
                    if (mesh.uvs_count) {
                        ids = mesh.vertex_uvs_indices[triangle_index];
                        vec2 UV1{mesh.vertex_uvs[ids.v1]};
                        vec2 UV2{mesh.vertex_uvs[ids.v2]};
                        vec2 UV3{mesh.vertex_uvs[ids.v3]};
                        hit.uv.x = fast_mul_add(UV3.x, u, fast_mul_add(UV2.u, v, UV1.u * w));
                        hit.uv.y = fast_mul_add(UV3.y, u, fast_mul_add(UV2.v, v, UV1.v * w));
                    }
                    if (mesh.normals_count) {
                        ids = mesh.vertex_normal_indices[triangle_index];
                        vec3 N1{mesh.vertex_normals[ids.v1]};
                        vec3 N2{mesh.vertex_normals[ids.v2]};
                        vec3 N3{mesh.vertex_normals[ids.v3]};
                        hit.normal.x = fast_mul_add(N3.x, u, fast_mul_add(N2.x, v, N1.x * w));
                        hit.normal.y = fast_mul_add(N3.y, u, fast_mul_add(N2.y, v, N1.y * w));
                        hit.normal.z = fast_mul_add(N3.z, u, fast_mul_add(N2.z, v, N1.z * w));
                        hit.normal = hit.normal.normalized();
                    }
                }
            }

            Transform &xform{hit_geometry.transform};
            hit.normal = xform.externDir(hit.normal).normalized();
//            hit.area *= (1.0f - hit.normal).dot(xform.rotation * xform.scale);
//            hit.cone_width = hit.distance * one_over_focal_length_squared;
            hit.distance = sqrtf(hit.distance_squared);
            hit.geo_id = hit_geometry_index;
            hit.material_id = hit_geometry.material_id;
            hit.geo_type = hit_geometry.type;
        }

        return found;
    }
};