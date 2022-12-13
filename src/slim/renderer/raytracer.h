#pragma once

#include "../math/vec3.h"
#include "../math/quat.h"
#include "../scene/box.h"
#include "../core/hud.h"
#include "../scene/selection.h"
#include "./common.h"
#include "./closest_hit/debug.h"
#include "./closest_hit/surface.h"
#include "./closest_hit/lights.h"
#include "./SSB.h"
#include "../viewport/viewport.h"

#ifdef __CUDACC__
#include "./raytracer_kernel.h"
#endif

#include "../draw/rectangle.h"
void drawSSB(const Scene &scene, Canvas &canvas) {
    ColorID color;
    for (u32 i = 0; i < scene.counts.geometries; i++) {
        Geometry &geometry = scene.geometries[i];
        if (geometry.flags & GEOMETRY_IS_VISIBLE) {
            switch (geometry.type) {
                case GeometryType_Box        : color = Cyan;    break;
                case GeometryType_Quad       : color = White;   break;
                case GeometryType_Sphere     : color = Yellow;  break;
                case GeometryType_Tetrahedron: color = Magenta; break;
                case GeometryType_Mesh       : color = Red;     break;
                default:
                    continue;
            }
            canvas.drawRect(geometry.screen_bounds, color);
        }
    }
}

//void setBoxGeometryFromAABB(Geometry *box_geometry, AABB *aabb) {
//    box_geometry.transform.rotation = {};
//    box_geometry.transform.position = (aabb.max + aabb.min) * 0.5f;
//    box_geometry.transform.scale    = (aabb.max - aabb.min) * 0.5f;
//    box_geometry.flags = 0;
//}
//
//void setBoxGeometryFromGeometryAndAABB(Geometry *box_geometry, Geometry *geometry, AABB *aabb) {
//    setBoxGeometryFromAABB(box_geometry, aabb);
//    box_geometry.transform.scale = box_geometry.transform.scale * geometry.transform.scale;
//    box_geometry.transform.rotation = geometry.transform.rotation;
//    box_geometry.transform.position = geometry.transform.externPos(box_geometry.transform.position);
//}
//
//void drawMeshAccelerationStructure(Viewport *viewport, Mesh *mesh, Geometry *geometry, Geometry *box_geometry) {
//    BVHNode *node = mesh.bvh.nodes;
//    vec3 color;
//    u32 node_count = mesh.bvh.node_count;
//    for (u32 node_id = 0; node_id < node_count; node_id++, node++) {
//        if (node.depth > 5) continue;
//        color = Color(node.leaf_count ? Magenta : (node_id ? Green : Blue));
//        setBoxGeometryFromGeometryAndAABB(box_geometry, geometry, &node.aabb);
//
//        drawBox(&viewport.default_box, BOX__ALL_SIDES, box_geometry, color, 0.125f, 0, viewport);
//    }
//}
//
//void drawBVH(const Scene &scene, const Viewport &viewport) {
//    static Geometry box_geometry;
//    box_geometry.transform.rotation = {};
//    enum ColorID color;
//    u32 *geometry_id;
//
//    for (u8 node_id = 0; node_id < scene.bvh.node_count; node_id++, node++) {
//        BVHNode &node = scene.bvh.nodes[node_id];
//        if (node.leaf_count) {
//            geometry_id = scene.bvh_leaf_geometry_indices + node.first_index;
//            for (u32 i = 0; i < node.leaf_count; i++, geometry_id++) {
//                Geometry &geometry = scene.geometries[geometry_id];
//                switch (geometry.type) {
//                    case GeometryType_Box        : color = Cyan;    break;
//                    case GeometryType_Quad       : color = White;   break;
//                    case GeometryType_Sphere     : color = Yellow;  break;
//                    case GeometryType_Tetrahedron: color = Magenta; break;
//                    case GeometryType_Mesh       :
//                        drawMeshAccelerationStructure(viewport, scene.meshes + geometry.id, geometry, &box_geometry);
//                        continue;
//                    default:
//                        continue;
//                }
//                drawBox(&viewport.default_box, BOX__ALL_SIDES, geometry, Color(color), 1, 1, viewport);
//            }
//            color = White;
//        } else
//            color = node_id ? Grey : Blue;
//
//        setBoxGeometryFromAABB(&box_geometry, &node.aabb);
//        drawBox(&viewport.default_box, BOX__ALL_SIDES, &box_geometry, Color(color), 1, 1, viewport);
//    }
//}

struct RayTracer {
    const Canvas &canvas;
    SceneTracer scene_tracer;
    LightsShader lights_shader;
    Shaded shaded;

    enum RenderMode mode = RenderMode_Beauty;
    bool use_gpu = false;
    u32 max_depth = 2;

    INLINE_XPU RayTracer(const Canvas &canvas, const Scene &scene, u32 *stack, u32 stack_size, u32 *mesh_stack, u32 mesh_stack_size, RenderMode mode = RenderMode_Beauty) :
            canvas{canvas}, scene_tracer{scene, stack, stack_size, mesh_stack, mesh_stack_size}, lights_shader{scene.lights, scene.counts.lights}, mode{mode} {}

    RayTracer(const Canvas &canvas, const Scene &scene, u32 stack_size, u32 mesh_stack_size = 0, RenderMode mode = RenderMode_Beauty, memory::MonotonicAllocator *memory_allocator = nullptr) :
            canvas{canvas}, scene_tracer{scene, stack_size, mesh_stack_size, memory_allocator}, lights_shader{scene.lights, scene.counts.lights}, mode{mode}  {}

    INLINE_XPU Color shade(Ray &ray) {
        shaded.reset(ray,
                     scene_tracer.scene.geometries,
                     scene_tracer.scene.materials,
                     mode == RenderMode_Beauty ?
                     scene_tracer.scene.textures : nullptr,
                     scene_tracer.pixel_area_over_focal_length_squared);
        switch (mode) {
            case RenderMode_UVs     : return shaded.getColorByUV();
            case RenderMode_Depth   : return shaded.getColorByDistance();
            case RenderMode_MipLevel: return shaded.getColorByMipLevel(scene_tracer.scene.textures[0]);
            case RenderMode_Normals : return directionToColor(shaded.sampleNormal(scene_tracer.scene.textures));
            default:
                return shaded.material->isEmissive() ? (shaded.from_behind ? Black : shaded.material->emission) :
                       shadeSurface(ray, scene_tracer, lights_shader, max_depth, shaded);
        }
    }

    void renderPixel(Ray &ray, const vec3 &camera_position, const mat3 &inverted_camera_rotation, u16 x, u16 y, Color &color) {
        f32 depth = shaded.distance = shaded.distance_squared = INFINITY;
        color = Black;
        vec3 Ro = ray.origin;
        vec3 Rd = ray.direction;
        bool hit_light_only = false;
        bool hit_found = scene_tracer.trace(ray, shaded);
        if (hit_found) {
            depth = (inverted_camera_rotation * (shaded.position - camera_position)).z;
            color = shade(ray);
        } else if (mode == RenderMode_Beauty && scene_tracer.scene.lights) {
            hit_light_only = lights_shader.shadeLights(Ro, Rd, shaded.distance, color);
            if (hit_light_only)
                depth = INFINITY;
        }
        if (mode == RenderMode_Beauty)
            color.applyToneMapping();

        if (hit_found || hit_light_only)
            canvas.setPixel(x, y, color, 1, depth);
    }

    void renderOnCPU(const Camera &camera) {
        mat3 inverted_camera_rotation = camera.rotation.inverted();

        f32 normalization_factor = 2.0f / canvas.dimensions.f_height;

        vec3 start = camera.getTopLeftCornerOfProjectionPlane(canvas.dimensions.width_over_height);
        vec3 right = camera.right * normalization_factor;
        vec3 down  = camera.up * -normalization_factor;
        vec3 current;

        Color color{Black};
        Ray ray;
        ray.origin = camera.position;

        Pixel *pixel = canvas.pixels;
        for (u16 y = 0; y < canvas.dimensions.height; y++) {
            current = start;
            for (u16 x = 0; x < canvas.dimensions.width; x++, pixel++) {
                ray.origin = camera.position;
                ray.direction = current.normalized();

                renderPixel(ray, camera.position, inverted_camera_rotation, x, y, color);

                current += right;
            }
            start += down;
        }
    }

#ifdef __CUDACC__
    void render(const Camera &camera) {
        if (use_gpu) renderOnGPU(camera);
         else        renderOnCPU(camera);
    }

    void renderSceneOnGPU(Scene *scene, Trace *trace, Camera *camera, Canvas &canvas, RenderMode mode) {
        Dimensions *dim = &viewport.frame_buffer.dimensions;
        u32 pixel_count = dim.width_times_height;
        u32 threads = 256;
        u32 blocks  = pixel_count / threads;
        if (pixel_count < threads) {
            threads = pixel_count;
            blocks = 1;
        } else if (pixel_count % threads)
            blocks++;

        d_render<<<blocks, threads>>>(
                viewport.projection_plane,
                viewport.settings.render_mode,
                viewport.camera.transform.position,
                viewport.camera.transform.rotation_inverted,
                viewport.trace,

                dim.width,
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
        downloadN(d_pixels, viewport.frame_buffer.float_pixels, dim.width_times_height)
    }

#else
    void render(const Camera &camera) {
        scene_tracer.pixel_area_over_focal_length_squared = 1.0f / (
                canvas.dimensions.h_width *
                canvas.dimensions.h_width *
                canvas.dimensions.h_width *
                camera.focal_length *
                camera.focal_length);
        renderOnCPU(camera);
    }
#endif
};