#pragma once

#include "../math/vec3.h"
#include "../math/quat.h"
#include "../scene/box.h"
#include "../core/hud.h"
#include "../scene/selection.h"
#include "./common.h"
#include "./trace.h"
#include "./closest_hit/debug.h"
#include "./closest_hit/surface.h"
#include "./closest_hit/lights.h"
#include "./SSB.h"
#include "../viewport/viewport.h"


//#include "../draw/rectangle.h"
//void drawSSB(Scene *scene, Canvas &canvas) {
//    ColorID color;
//    Geometry *geometry = scene->geometries;
//    for (u32 i = 0; i < scene->counts.geometries; i++, geometry++) {
//        if (geometry->flags & GEOMETRY_IS_VISIBLE) {
//            switch (geometry->type) {
//                case GeometryType_Box        : color = Cyan;    break;
//                case GeometryType_Quad       : color = White;   break;
//                case GeometryType_Sphere     : color = Yellow;  break;
//                case GeometryType_Tetrahedron: color = Magenta; break;
//                case GeometryType_Mesh       : color = Red;     break;
//                default:
//                    continue;
//            }
//            canvas.drawRect(geometry->screen_bounds, color);
//        }
//    }
//}
//
//void setBoxGeometryFromAABB(Geometry *box_geometry, AABB *aabb) {
//    box_geometry->transform.rotation = {};
//    box_geometry->transform.position = (aabb->max + aabb->min) * 0.5f;
//    box_geometry->transform.scale    = (aabb->max - aabb->min) * 0.5f;
//    box_geometry->flags = 0;
//}
//
//void setBoxGeometryFromGeometryAndAABB(Geometry *box_geometry, Geometry *geometry, AABB *aabb) {
//    setBoxGeometryFromAABB(box_geometry, aabb);
//    box_geometry->transform.scale = box_geometry->transform.scale * geometry->transform.scale;
//    box_geometry->transform.rotation = geometry->transform.rotation;
//    box_geometry->transform.position = geometry->transform.externPos(box_geometry->transform.position);
//}
//
//void drawMeshAccelerationStructure(Viewport *viewport, Mesh *mesh, Geometry *geometry, Geometry *box_geometry) {
//    BVHNode *node = mesh->bvh.nodes;
//    vec3 color;
//    u32 node_count = mesh->bvh.node_count;
//    for (u32 node_id = 0; node_id < node_count; node_id++, node++) {
//        if (node->depth > 5) continue;
//        color = Color(node->leaf_count ? Magenta : (node_id ? Green : Blue));
//        setBoxGeometryFromGeometryAndAABB(box_geometry, geometry, &node->aabb);
//
//        drawBox(&viewport->default_box, BOX__ALL_SIDES, box_geometry, color, 0.125f, 0, viewport);
//    }
//}
//
//void drawBVH(Scene *scene, Viewport *viewport) {
//    static Geometry box_geometry;
//    BVHNode *node = scene->bvh.nodes;
//    Geometry *geometry;
//    box_geometry.transform.rotation = {};
//    enum ColorID color;
//    u32 *geometry_id;
//
//    for (u8 node_id = 0; node_id < scene->bvh.node_count; node_id++, node++) {
//        if (node->child_count) {
//            geometry_id = scene->bvh.leaf_ids + node->first_child_id;
//            for (u32 i = 0; i < node->child_count; i++, geometry_id++) {
//                geometry = &scene->geometries[*geometry_id];
//                switch (geometry->type) {
//                    case GeometryType_Box        : color = Cyan;    break;
//                    case GeometryType_Quad       : color = White;   break;
//                    case GeometryType_Sphere     : color = Yellow;  break;
//                    case GeometryType_Tetrahedron: color = Magenta; break;
//                    case GeometryType_Mesh       :
//                        drawMeshAccelerationStructure(viewport, scene->meshes + geometry->id, geometry, &box_geometry);
//                        continue;
//                    default:
//                        continue;
//                }
//                drawBox(&viewport->default_box, BOX__ALL_SIDES, geometry, Color(color), 1, 1, viewport);
//            }
//            color = White;
//        } else
//            color = node_id ? Grey : Blue;
//
//        setBoxGeometryFromAABB(&box_geometry, &node->aabb);
//        drawBox(&viewport->default_box, BOX__ALL_SIDES, &box_geometry, Color(color), 1, 1, viewport);
//    }
//}

INLINE void rayTrace(Ray *ray, Trace *trace, Scene *scene, enum RenderMode mode, u16 x, u16 y, vec3 camera_position, mat3 camera_rotation, Canvas &canvas) {
    RayHit *hit = &trace->closest_hit;
    vec3 Ro = ray->origin;
    vec3 Rd = ray->direction;
    vec3 color;

    bool lights_shaded = false;
    bool hit_found = traceRay(ray, trace, scene);
//    bool hit_found = hitGeometries(ray, trace, scene, scene->geometries, scene->counts.geometries, false, true, x, y);
    f32 closest_distance = hit_found ? hit->distance : INFINITY;
    f32 z = INFINITY;
    if (hit_found) {
        z = (camera_rotation * (hit->position - camera_position)).z;

        switch (mode) {
            case RenderMode_Beauty:
                color = shadeSurface(ray, trace, scene, &lights_shaded);
                break;
            case RenderMode_Depth  : color = shadeDepth(hit->distance); break;
            case RenderMode_Normals: {
                    Material *M = scene->materials  + hit->material_id;
                    if (M->hasNormalMap()) {
                        hit->uv_area /= M->uv_repeat.u / M->uv_repeat.v;
                        vec2 uv = hit->uv * M->uv_repeat;
                        f32 uv_area = dUVbyRayCone(hit->NdotV, hit->cone_width, hit->area, hit->uv_area);
                        Texture &normal_map  = scene->textures[M->texture_ids[1]];
                        if (M->hasNormalMap()) hit->normal = Shaded::applyNormalRotation(hit->normal, normal_map, uv, uv_area, M->normal_magnitude);
                    }

                    color = shadeDirection(hit->normal);

                }
                break;
            case RenderMode_UVs    : color = shadeUV(hit->uv);            break;
        }
    }

    if (mode == RenderMode_Beauty && scene->lights && !lights_shaded) {
        if (shadeLights(scene->lights, scene->counts.lights, Ro, Rd, closest_distance, &trace->sphere_hit, &color)) {
            hit_found = true;
            z = trace->sphere_hit.closest_hit_distance;
        }
    }

    if (hit_found) {
        if (mode == RenderMode_Beauty) {
            color.x = toneMappedBaked(color.x);
            color.y = toneMappedBaked(color.y);
            color.z = toneMappedBaked(color.z);
        }
        canvas.setPixel(x, y, color.toColor(), 1, z);
    }
}

void renderSceneOnCPU(Scene *scene, Trace *trace, Camera *camera, Canvas &canvas, RenderMode mode) {
    mat3 camera_rotation = camera->rotation.inverted();
    vec3 camera_position = camera->position;

    f32 normalization_factor = 2.0f / canvas.dimensions.f_height;

    vec3 start = camera->getTopLeftCornerOfProjectionPlane(canvas.dimensions.width_over_height);
    vec3 right = camera->right * normalization_factor;
    vec3 down  = camera->up * -normalization_factor;
    vec3 current;

    Ray ray;
    ray.origin = camera_position;

    Pixel *pixel = canvas.pixels;
    for (u16 y = 0; y < canvas.dimensions.height; y++) {
        current = start;
        for (u16 x = 0; x < canvas.dimensions.width; x++, pixel++) {
            ray.origin = camera_position;
            ray.direction = current.normalized();
            ray.direction_reciprocal = 1.0f / ray.direction;

            trace->closest_hit.distance = trace->closest_hit.distance_squared = INFINITY;
            trace->closest_hit.cone_width = 0;

            rayTrace(&ray, trace, scene, mode, x, y, camera_position, camera_rotation, canvas);

            current += right;
        }
        start += down;
    }
}

#ifdef __CUDACC__

__global__ void d_render(ProjectionPlane projection_plane, enum RenderMode mode, vec3 camera_position, quat camera_rotation, Trace trace,
                         u16 width,
                         u32 pixel_count,

                         Scene scene,
                         u32        *scene_bvh_leaf_ids,
                         BVHNode    *scene_bvh_nodes,
                         BVHNode    *mesh_bvh_nodes,
                         Mesh       *meshes,
                         Triangle   *mesh_triangles,
                         Light *lights,
                         AreaLight  *area_lights,
                         Material   *materials,
                         Texture *textures,
                         TextureMip *texture_mips,
                         TexelQuad *texel_quads,
                         Geometry  *geometries,

                         u32       *mesh_bvh_leaf_ids,
                         const u32 *mesh_bvh_node_counts,
                         const u32 *mesh_triangle_counts
) {
    u32 i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= pixel_count)
        return;

    Pixel *pixel = d_pixels + i;
    pixel->color = Color(Black);
    pixel->opacity = 1;
    pixel->depth = INFINITY;

    u16 x = i % width;
    u16 y = i / width;

    Ray ray;
    ray.origin = camera_position;
    ray.direction = normVec3(scaleAddVec3(projection_plane.down, y, scaleAddVec3(projection_plane.right, x, projection_plane.start)));

    scene.lights = lights;
    scene.area_lights  = area_lights;
    scene.materials    = materials;
    scene.textures     = textures;
    scene.geometries   = geometries;
    scene.meshes       = meshes;
    scene.bvh.nodes    = scene_bvh_nodes;
    scene.bvh.leaf_ids = scene_bvh_leaf_ids;

    u32 scene_stack[6], mesh_stack[5];
    trace.mesh_stack  = mesh_stack;
    trace.scene_stack = scene_stack;

    Mesh *mesh = meshes;
    u32 nodes_offset = 0;
    u32 triangles_offset = 0;
    for (u32 m = 0; m < scene.settings.meshes; m++, mesh++) {
        mesh->bvh.node_count = mesh_bvh_node_counts[m];
        mesh->triangles      = mesh_triangles + triangles_offset;
        mesh->bvh.leaf_ids   = mesh_bvh_leaf_ids + triangles_offset;
        mesh->bvh.nodes      = mesh_bvh_nodes + nodes_offset;

        nodes_offset        += mesh->bvh.node_count;
        triangles_offset    += mesh->triangle_count;
    }

    TextureMip *mip = texture_mips;
    TexelQuad *quads = texel_quads;
    Texture *texture = textures;
    for (u32 t = 0; t < scene.settings.textures; t++, texture++) {
        texture->mips = mip;
        for (u32 m = 0; m < texture->mip_count; m++, mip++) {
            mip->texel_quads = quads;
            quads += mip->width * mip->height;
        }
    }

    ray.direction_reciprocal = oneOverVec3(ray.direction);
    trace.closest_hit.distance = trace.closest_hit.distance_squared = INFINITY;
    trace.closest_hit.cone_angle = projection_plane.cone_angle;
    trace.closest_hit.cone_width = 0;

    rayTrace(&ray, &trace, &scene, mode, pixel, x, y, camera_position, camera_rotation);
}

void renderSceneOnGPU(Scene *scene, Trace *trace, Camera *camera, Canvas &canvas, RenderMode mode) {
    Dimensions *dim = &viewport->frame_buffer->dimensions;
    u32 pixel_count = dim->width_times_height;
    u32 threads = 256;
    u32 blocks  = pixel_count / threads;
    if (pixel_count < threads) {
        threads = pixel_count;
        blocks = 1;
    } else if (pixel_count % threads)
        blocks++;

    d_render<<<blocks, threads>>>(
            viewport->projection_plane,
            viewport->settings.render_mode,
            viewport->camera->transform.position,
            viewport->camera->transform.rotation_inverted,
            viewport->trace,

            dim->width,
            pixel_count,

            *scene,
            d_scene_bvh_leaf_ids,
            d_scene_bvh_nodes,
            d_mesh_bvh_nodes,
            d_meshes,
            d_triangles,
            d_lights,
            d_area_lights,
            d_materials,
            d_textures,
            d_texture_mips,
            d_texel_quads,
            d_geometries,

            d_mesh_bvh_leaf_ids,
            d_mesh_bvh_node_counts,
            d_mesh_triangle_counts);

    checkErrors()
    downloadN(d_pixels, viewport->frame_buffer->float_pixels, dim->width_times_height)
}
#endif

void renderScene(Scene *scene, Viewport *viewport, Trace *trace, RenderMode mode, bool use_gpu = false) {
#ifdef __CUDACC__
    if (use_gpu) renderSceneOnGPU(scene, trace, viewport->camera, viewport->canvas, mode);
    else         renderSceneOnCPU(scene, trace, viewport->camera, viewport->canvas, mode);
#else
    renderSceneOnCPU(scene, trace, viewport->camera, viewport->canvas, mode);
#endif
}
