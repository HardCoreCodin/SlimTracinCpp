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

    INLINE_XPU bool inShadow(const vec3 &origin, const vec3 &direction, float max_distance = INFINITY) {
        shadow_hit.distance = local_hit.distance = max_distance;
        shadow_ray.origin = origin;
        shadow_ray.direction = direction;
        return trace(shadow_ray, shadow_hit, true);
    }

    INLINE_XPU Geometry* trace(Ray &ray, RayHit &hit, bool any_hit = false) {
        if (!any_hit) {
            hit.distance = local_hit.distance = INFINITY;
        }
        ray.direction_reciprocal = 1.0f / ray.direction;
        ray.prePrepRay();

        bool hit_left, hit_right;
        f32 left_distance, right_distance;
        if (!(ray.hitsAABB(scene.bvh.nodes->aabb, left_distance) && left_distance < hit.distance))
            return nullptr;

        u32 *indices = scene.bvh_leaf_geometry_indices;
        if (unlikely(scene.bvh.nodes->leaf_count))
            return hitGeometries(indices, scene.counts.geometries, ray, hit, any_hit);

        BVHNode *left_node = scene.bvh.nodes + scene.bvh.nodes->first_index;
        BVHNode *right_node, *tmp_node;
        Geometry *hit_geo, *closest_hit_geo = nullptr;
        u32 top = 0;

        while (true) {
            right_node = left_node + 1;

            hit_left  = ray.hitsAABB(left_node->aabb, left_distance) && left_distance < hit.distance;
            hit_right = ray.hitsAABB(right_node->aabb, right_distance) && right_distance < hit.distance;

            if (hit_left) {
                if (unlikely(left_node->leaf_count)) {
                    hit_geo = hitGeometries(indices + left_node->first_index, left_node->leaf_count, ray, hit, any_hit);
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
                    hit_geo = hitGeometries(indices + right_node->first_index, right_node->leaf_count, ray, hit, any_hit);
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

        return closest_hit_geo;
    }

    INLINE_XPU bool hitGeometry(const Geometry &geo, Ray &ray, bool any_hit = false) {
        geo.transform.internPosAndDir(ray.origin, ray.direction, local_ray.origin, local_ray.direction);
        local_ray.origin = local_ray.direction.scaleAdd(TRACE_OFFSET, local_ray.origin);
        if (geo.type == GeometryType_Mesh || geo.type == GeometryType_Box || geo.type == GeometryType_Quad) {
            local_ray.direction_reciprocal = 1.0f / local_ray.direction;
            if (geo.type != GeometryType_Quad)
                local_ray.prePrepRay();
        }

        switch (geo.type) {
            case GeometryType_Quad       : return local_ray.hitsDefaultQuad(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Box        : return local_ray.hitsDefaultBox(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Sphere     : return local_ray.hitsDefaultSphere(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Tetrahedron: return local_ray.hitsDefaultTetrahedron(local_hit, geo.flags & GEOMETRY_IS_TRANSPARENT);
            case GeometryType_Mesh       : return mesh_tracer.trace(scene.meshes[geo.id], local_ray, local_hit, any_hit);
            default: return false;
        }
    }

    INLINE_XPU Geometry* hitGeometries(const u32 *geometry_indices, u32 geo_count, Ray &ray, RayHit &hit, bool any_hit) {
        Geometry *geo, *hit_geo = nullptr;
        u8 visibility_flag = any_hit ? GEOMETRY_IS_SHADOWING : GEOMETRY_IS_VISIBLE;

        local_hit.distance = hit.distance;

        for (u32 i = 0; i < geo_count; i++) {
            geo = scene.geometries + geometry_indices[i];

            if (!(geo->flags & visibility_flag))
                continue;

//                if (x <   screen_bounds[geometry_index].left ||
//                    x >=  screen_bounds[geometry_index].right ||
//                    y <   screen_bounds[geometry_index].top ||
//                    y >=  screen_bounds[geometry_index].bottom)
//                    continue;
//            }

//            if (geo.type == GeometryType_Mesh) {
//                local_hit.distance_squared = geo.transform.internPos(hit.position - local_ray.origin).squaredLength();
//                local_hit.distance = sqrtf(local_hit.distance_squared);
//            } else
//                local_hit.distance_squared = local_hit.distance == INFINITY;

            if (hitGeometry(*geo, ray, any_hit)) {
                if (any_hit)
                    return geo;

                if (local_hit.distance < hit.distance) {
                    hit = local_hit;
                    hit_geo = geo;
                }
            }
        }

        return hit_geo;
    }

    INLINE_XPU void finalizeHitFromGeo(const Ray &ray, RayHit &hit, Geometry *hit_geo) {
        if (hit_geo->type == GeometryType_Mesh) {
            Mesh &mesh{scene.meshes[hit_geo->id]};
            if (mesh.normals_count | mesh.uvs_count) {
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
        }

        hit.position = ray[hit.distance];
        hit.normal = hit_geo->transform.externDir(hit.normal);
        hit.id = hit_geo - scene.geometries;
//        if (hit.from_behind) {
//            hit.normal = -hit.normal;
//            hit.position = hit.normal.scaleAdd(-TRACE_OFFSET*2, hit.position);
//        }
//        hit.geo_id = hit_geo->id;
//        hit.distance_squared = hit.distance * hit.distance;
//            hit.area *= (1.0f - hit.normal).dot(xform.rotation * xform.scale);
//            hit.cone_width = hit.distance * one_over_focal_length_squared;
    }
};