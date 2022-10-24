#pragma once

#include "../math/vec3.h"
#include "../core/ray.h"
#include "./intersection/geometry.h"

INLINE_XPU bool traceScene(Ray *ray, Trace *trace, Scene *scene, bool any_hit) {
    ray->direction_reciprocal = 1.0f / ray->direction;
    ray->prePrepRay();

    bool hit_left, hit_right, found = false;
    f32 left_distance, right_distance;

    if (!ray->hitsAABB(scene->bvh.nodes->aabb, trace->closest_hit.distance, &left_distance))
        return false;

    u32 *indices = scene->bvh_leaf_geometry_indices;
    if (unlikely(scene->bvh.nodes->leaf_count))
        return hitGeometries(ray, trace, scene, indices, scene->counts.geometries, any_hit, false, 0, 0);

    BVHNode *left_node = scene->bvh.nodes + scene->bvh.nodes->first_index;
    BVHNode *right_node, *tmp_node;
    u32 *stack = trace->scene_stack;
    u32 stack_size = 0;

    while (true) {
        right_node = left_node + 1;

        hit_left  = ray->hitsAABB(left_node->aabb, trace->closest_hit.distance, &left_distance);
        hit_right = ray->hitsAABB(right_node->aabb, trace->closest_hit.distance, &right_distance);

        if (hit_left) {
            if (unlikely(left_node->leaf_count)) {
                if (hitGeometries(ray, trace, scene, indices + left_node->first_index, left_node->leaf_count, any_hit, false, 0, 0)) {
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
                if (hitGeometries(ray, trace, scene, indices + right_node->first_index, right_node->leaf_count, any_hit, false, 0, 0)) {
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
                stack[stack_size++] = right_node->first_index;
            }
            left_node = scene->bvh.nodes + left_node->first_index;
        } else if (right_node) {
            left_node = scene->bvh.nodes + right_node->first_index;
        } else {
            if (stack_size == 0) break;
            left_node = scene->bvh.nodes + stack[--stack_size];
        }
    }

    return found;
}
//
//INLINE_XPU bool tracePrimaryRay(Ray *ray, Trace *trace, Scene *scene, u16 x, u16 y) {
//    ray->direction_reciprocal = 1.0f / ray->direction;
//    trace->closest_hit.distance = trace->closest_hit.distance_squared = MAX_DISTANCE;
//
//    return hitGeometries(ray, trace, scene, scene->geometries, scene->counts.geometries, false, true, x, y);
//}

INLINE_XPU bool inShadow(Ray *ray, Trace *trace, Scene *scene) {
    return traceScene(ray, trace, scene, true);
}

INLINE_XPU bool traceRay(Ray *ray, Trace *trace, Scene *scene) {
    trace->closest_hit.distance = trace->closest_hit.distance_squared = INFINITY;
    return traceScene(ray, trace, scene, false);
}