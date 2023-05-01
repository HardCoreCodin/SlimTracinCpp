#pragma once

#include "../viewport/viewport.h"
#include "ray_tracer.h"
#include "surface_shader.h"

#ifdef __CUDACC__
#include "./renderer_GPU.h"
#else
#define USE_GPU_BY_DEFAULT false
void initDataOnGPU(const Scene &scene) {}
void uploadLights(const Scene &scene) {}
void uploadCameras(const Scene &scene) {}
void uploadGeometries(const Scene &scene) {}
void uploadMaterials(const Scene &scene) {}
void uploadSceneBVH(const Scene &scene) {}
#endif

#define RAY_TRACER_DEFAULT_SETTINGS_SKYBOX_TEXTURE_ID 1
#define RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH 3
#define RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE RenderMode_Beauty


struct RayTracingRenderer {
    Scene &scene;
    SceneTracer &scene_tracer;
    CameraRayProjection &projection;

    RayTracerSettings settings;
    SurfaceShader surface;
    Ray ray;
    RayHit hit;
    Color color;
    f32 depth;

    explicit RayTracingRenderer(Scene &scene,
                                SceneTracer &scene_tracer,
                                CameraRayProjection &projection,
                                u8 max_depth = RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH,
                                char skybox_color_texture_id = -1,
                                char skybox_radiance_texture_id = -1,
                                char skybox_irradiance_texture_id = -1,
                                RenderMode render_mode = RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE) :
                                scene{scene}, scene_tracer{scene_tracer}, projection{projection} {
        settings.skybox_color_texture_id = skybox_color_texture_id;
        settings.skybox_radiance_texture_id = skybox_radiance_texture_id;
        settings.skybox_irradiance_texture_id = skybox_irradiance_texture_id;
        settings.max_depth = max_depth;
        settings.render_mode = render_mode;
        settings.mip_level_colors[0] = BrightRed;
        settings.mip_level_colors[1] = BrightYellow;
        settings.mip_level_colors[2] = BrightGreen;
        settings.mip_level_colors[3] = BrightMagenta;
        settings.mip_level_colors[4] = BrightCyan;
        settings.mip_level_colors[5] = BrightBlue;
        settings.mip_level_colors[6] = BrightGrey;
        settings.mip_level_colors[7] = Grey;
        settings.mip_level_colors[8] = DarkGrey;

        initDataOnGPU(scene);
    }

    void render(const Viewport &viewport, bool update_scene = true, bool use_GPU = false) {
        const Camera &camera = *viewport.camera;
        const Canvas &canvas = viewport.canvas;

        ray.origin = camera.position;

        if (update_scene) {
            scene.updateAABBs();
            scene.updateBVH();
            if (use_GPU) {
                uploadLights(scene);
                uploadCameras(scene);
                uploadGeometries(scene);
                uploadSceneBVH(scene);
            }
        }
#ifdef __CUDACC__
        if (use_GPU) renderOnGPU(canvas, projection, settings);
        else         renderOnCPU(canvas);
#else
        renderOnCPU(canvas);
#endif
    }

    void renderOnCPU(const Canvas &canvas) {
        i32 width  = canvas.dimensions.width  * (canvas.antialias == SSAA ? 2 : 1);
        i32 height = canvas.dimensions.height * (canvas.antialias == SSAA ? 2 : 1);
        vec3 C, direction;
        for (C.y = projection.C_start.y, ray.pixel_coords.y = 0; ray.pixel_coords.y < height;
             C.y -= projection.sample_size, ray.pixel_coords.y++, projection.start += projection.down) {
            for (direction = projection.start, C.x = projection.C_start.x, ray.pixel_coords.x = 0;  ray.pixel_coords.x < width;
                 direction += projection.right, C.x += projection.sample_size, ray.pixel_coords.x++) {
                hit.scaling_factor = 1.0f / sqrtf(C.squaredLength() + projection.squared_distance_to_projection_plane);
                renderPixel(settings, projection, scene, scene_tracer, surface, ray, hit, direction, color, depth);
                canvas.setPixel(ray.pixel_coords.x, ray.pixel_coords.y, color, -1, depth);
            }
        }
    }
};