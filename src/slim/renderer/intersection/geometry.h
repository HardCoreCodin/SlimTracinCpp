#pragma once

#include "./tetrahedra.h"
#include "./sphere.h"
#include "./quad.h"
#include "./box.h"
#include "./mesh.h"

INLINE bool hitGeometries(Ray *ray, Trace *trace, Scene *scene, u32 *geometry_indices, u32 geo_count, bool any_hit, bool check_visibility, u16 x, u16 y) {
    bool current_found, found = false;
    vec3 *Rd = &trace->local_space_ray.direction;
    vec3 *Ro = &trace->local_space_ray.origin;

    RayHit *hit = &trace->current_hit;
    RayHit *closest_hit = &trace->closest_hit;
    f32 cone_angle = closest_hit->cone_angle;

    Geometry *hit_geometry, *geometry;
    u32 *geometry_index = geometry_indices;
    u32 hit_geo_id;
    for (u32 i = 0; i < geo_count; i++, geometry_index++) {
        geometry = scene->geometries + *geometry_index;
        if (any_hit && !(geometry->flags & GEOMETRY_IS_SHADOWING))
            continue;

        if (check_visibility) {
            if (!(geometry->flags & GEOMETRY_IS_VISIBLE))
                continue;

            if (x <   geometry->screen_bounds.left ||
                x >=  geometry->screen_bounds.right ||
                y <   geometry->screen_bounds.top ||
                y >=  geometry->screen_bounds.bottom)
                continue;
        }

        geometry->transform.internPosAndDir(ray->origin, ray->direction, *Ro, *Rd);
        *Ro = Rd->scaleAdd(TRACE_OFFSET, *Ro);

        switch (geometry->type) {
            case GeometryType_Quad       : current_found = hitQuad(       hit, Ro, Rd, geometry->flags); break;
            case GeometryType_Box        : current_found = hitBox(        hit, Ro, Rd, geometry->flags); break;
            case GeometryType_Sphere     : current_found = hitSphere(     hit, Ro, Rd, geometry->flags); break;
            case GeometryType_Tetrahedron: current_found = hitTetrahedron(hit, Ro, Rd, geometry->flags); break;
            case GeometryType_Mesh:
                trace->local_space_ray.direction_reciprocal = 1.0f / *Rd;
                trace->closest_mesh_hit.distance = closest_hit->distance == INFINITY ? INFINITY : geometry->transform.internPos(closest_hit->position - *Ro).length();

                trace->local_space_ray.prePrepRay();
                current_found = traceMesh(trace, scene->meshes + geometry->id, any_hit);
                if (current_found) *hit = trace->closest_mesh_hit;
                break;
            default:
                continue;
        }

        if (current_found) {
            hit->position       = geometry->transform.externPos(hit->position);
            hit->distance_squared = (hit->position - ray->origin).squaredLength();
            if (hit->distance_squared < closest_hit->distance_squared) {
                *closest_hit = *hit;
                hit_geo_id = geometry->id;
                hit_geometry = geometry;

                closest_hit->geo_type = geometry->type;
                closest_hit->material_id = geometry->material_id;

                found = true;

                if (any_hit)
                    break;
            }
        }
    }

    if (found) {
        Mesh *mesh;
        if (closest_hit->geo_type == GeometryType_Mesh) {
            mesh = scene->meshes + hit_geometry->id;
            u32 triangle_index = closest_hit->geo_id;
            if (mesh->normals_count | mesh->uvs_count) {
                TriangleVertexIndices ids;
                f32 u = closest_hit->uv.u;
                f32 v = closest_hit->uv.v;
                f32 w = 1 - u - v;
                if (mesh->uvs_count) {
                    ids = mesh->vertex_uvs_indices[triangle_index];
                    vec2 UV1{mesh->vertex_uvs[ids.v1]};
                    vec2 UV2{mesh->vertex_uvs[ids.v2]};
                    vec2 UV3{mesh->vertex_uvs[ids.v3]};
                    closest_hit->uv.x = fast_mul_add(UV3.x, u, fast_mul_add(UV2.u, v, UV1.u * w));
                    closest_hit->uv.y = fast_mul_add(UV3.y, u, fast_mul_add(UV2.v, v, UV1.v * w));
                }
                if (mesh->normals_count) {
                    ids = mesh->vertex_normal_indices[triangle_index];
                    vec3 N1{mesh->vertex_normals[ids.v1]};
                    vec3 N2{mesh->vertex_normals[ids.v2]};
                    vec3 N3{mesh->vertex_normals[ids.v3]};
                    closest_hit->normal.x = fast_mul_add(N3.x, u, fast_mul_add(N2.x, v, N1.x * w));
                    closest_hit->normal.y = fast_mul_add(N3.y, u, fast_mul_add(N2.y, v, N1.y * w));
                    closest_hit->normal.z = fast_mul_add(N3.z, u, fast_mul_add(N2.z, v, N1.z * w));
                    closest_hit->normal = closest_hit->normal.normalized();
                }
            }
        }
        Transform &xform{hit_geometry->transform};
        closest_hit->geo_id = hit_geo_id;
        closest_hit->normal = xform.externDir(closest_hit->normal).normalized();
        closest_hit->distance = sqrtf(closest_hit->distance_squared);
        closest_hit->cone_width = 2.0f * cone_angle * closest_hit->distance;
        closest_hit->area *= (1.0f - closest_hit->normal).dot(xform.rotation * xform.scale);
    }

    return found;
}