#pragma once

#include "../viewport/viewport.h"
#include "./tracers/ray_tracer.h"
#include "./shaders/surface_shader.h"

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

#define RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH 3
#define RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE RenderMode_Beauty


struct RayTracingRenderer {
    Scene &scene;
    RayTracer ray_tracer;
    RayTracerSettings settings;
    RayTracerProjection projection;

    explicit RayTracingRenderer(Scene &scene,
                                u32 max_depth = RAY_TRACER_DEFAULT_SETTINGS_MAX_DEPTH,
                                RenderMode render_mode = RAY_TRACER_DEFAULT_SETTINGS_RENDER_MODE,
                                memory::MonotonicAllocator *memory_allocator = nullptr) :
              scene{scene}, ray_tracer{scene.counts.geometries, scene.mesh_stack_size, memory_allocator} {

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

        ray_tracer.ray.origin = camera.position;
        projection.reset(camera, canvas);

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
        if (use_GPU) renderOnGPU(canvas, &settings, &projection);
        else         renderOnCPU(canvas);
#else
        renderOnCPU(canvas);
#endif
    }

    void renderOnCPU(const Canvas &canvas) {
        const Dimensions &dim{canvas.dimensions};

        f32 &Cx = ray_tracer.screen_center_to_pixel_center.x;
        f32 &Cy = ray_tracer.screen_center_to_pixel_center.y;
        i32 &x = ray_tracer.ray.pixel_coords.x;
        i32 &y = ray_tracer.ray.pixel_coords.y;
        i32 width = dim.width * (canvas.antialias == SSAA ? 2 : 1);
        i32 height = dim.height * (canvas.antialias == SSAA ? 2 : 1);

        vec3 direction;
        Color color;

        for (Cy = projection.Cy_start, y = 0; y < height;
             Cy -= projection.sample_size, y++, projection.start += projection.down) {
            for (direction = projection.start, Cx = projection.Cx_start, x = 0;  x < width;
                 direction += projection.right, Cx += projection.sample_size, x++) {
                ray_tracer.renderPixel(scene, settings, projection, direction, color);
                canvas.setPixel(x, y, color, -1, ray_tracer.z_depth);
            }
        }
    }
};