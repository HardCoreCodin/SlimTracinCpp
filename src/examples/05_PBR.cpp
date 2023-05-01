#include "./textures.h"

#include "../slim/scene/selection.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/renderer.h"
#include "../slim/app.h"

// Or using the single-header file:
//#include "../slim.H"

#define GRID_SPACING 2.5f
#define GRID_SIZE 7
#define GRID_SIZE_F ((float)GRID_SIZE)
#define OBJECT_COUNT (GRID_SIZE * GRID_SIZE)


struct ExampleApp : SlimApp {
    bool use_gpu = USE_GPU_BY_DEFAULT;
    bool antialias = false;
    bool skybox_swapped = false;
    bool draw_BVH = false;
    bool cutout = false;

    // HUD:
    HUDLine FPS {"FPS : "};
    HUDLine GPU {"GPU : ", "Off","On", &use_gpu};
    HUDLine AA  {"AA  : ", "Off","On", &antialias};
    HUDLine BVH {"BVH : ", "Off","On", &draw_BVH};
    HUD hud{{4}, &FPS};

    // Viewport:
    Camera camera{{}, {-2.0f, -1.0f, -20.0f}};
    CameraRayProjection projection;
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light light1{White,{+10, +10, -10}, 200};
    Light light2{White,{-10, +10, -10}, 200};
    Light light3{White,{+10, -20, -10}, 200};
    Light light4{White,{-10, -20, -10}, 200};
    Light *lights{&light1};

    Material materials[GRID_SIZE][GRID_SIZE];
    Geometry geometries[GRID_SIZE][GRID_SIZE];
    Scene scene{{OBJECT_COUNT, 1, 4,
                          OBJECT_COUNT, TextureCount},
                &geometries[0][0], &camera, lights,
                &materials[0][0], textures, texture_files};

    ExampleApp() {
        Color plastic{0.04f};
        for (u8 row = 0; row < GRID_SIZE; row++) {
            for (u8 col = 0; col < GRID_SIZE; col++) {
                Geometry &geo{geometries[row][col]};
                Material &mat{materials[row][col]};
                geo.type = GeometryType_Sphere;
                geo.transform.position.x = ((f32)col - (GRID_SIZE_F / 2.0f)) * GRID_SPACING;
                geo.transform.position.y = ((f32)row - (GRID_SIZE_F / 2.0f)) * GRID_SPACING;
                geo.material_id = GRID_SIZE * row + col;
                mat.brdf = BRDF_CookTorrance;
                mat.metalness = (f32)row / GRID_SIZE_F;
                mat.roughness = (f32)col / GRID_SIZE_F;
                mat.roughness = clampedValue(mat.roughness, 0.05f, 1.0f);
                mat.albedo = Color{0.5f, 0.0f, 0.0f};
                mat.reflectivity = plastic.lerpTo(mat.albedo, mat.metalness);
            }
        }
        uploadMaterials(scene);
    }

    SceneTracer scene_tracer{scene.counts.geometries, scene.mesh_stack_size};
    Selection selection{scene, scene_tracer, projection};
    RayTracingRenderer renderer{scene, scene_tracer, projection, 1,
                                Cathedral_SkyboxColor,
                                Cathedral_SkyboxRadiance,
                                Cathedral_SkyboxIrradiance};

    void OnUpdate(f32 delta_time) override {
        projection.reset(camera, canvas.dimensions, canvas.antialias == SSAA);

        i32 fps = (i32)render_timer.average_frames_per_second;
        FPS.value = fps;
        FPS.value_color = fps >= 60 ? Green : (fps >= 24 ? Cyan : (fps < 12 ? Red : Yellow));

        if (!mouse::is_captured) selection.manipulate(viewport);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);
    }

    void OnRender() override {
        renderer.render(viewport, true, use_gpu);
        if (draw_BVH) drawSceneBVH();
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);
        canvas.drawToWindow();
    }

    void OnKeyChanged(u8 key, bool is_pressed) override {
        if (!is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'G' && USE_GPU_BY_DEFAULT) use_gpu = !use_gpu;
            if (key == 'B') draw_BVH = !draw_BVH;
            if (key == 'V') {
                antialias = !antialias;
                canvas.antialias = antialias ? SSAA : NoAA;
            }
            if (key == 'M') {
                skybox_swapped = !skybox_swapped;
                char inc = skybox_swapped ? 3 : -3;
                renderer.settings.skybox_color_texture_id += inc;
                renderer.settings.skybox_radiance_texture_id += inc;
                renderer.settings.skybox_irradiance_texture_id += inc;
            }
        }

        Move &move = viewport.navigation.move;
        Turn &turn = viewport.navigation.turn;
        if (key == 'Q') turn.left     = is_pressed;
        if (key == 'E') turn.right    = is_pressed;
        if (key == 'R') move.up       = is_pressed;
        if (key == 'F') move.down     = is_pressed;
        if (key == 'W') move.forward  = is_pressed;
        if (key == 'S') move.backward = is_pressed;
        if (key == 'A') move.left     = is_pressed;
        if (key == 'D') move.right    = is_pressed;
    }
    void OnWindowResize(u16 width, u16 height) override {
        viewport.updateDimensions(width, height);
        canvas.dimensions.update(width, height);
    }
    void OnMouseButtonDown(mouse::Button &mouse_button) override {
        mouse::pos_raw_diff_x = mouse::pos_raw_diff_y = 0;
    }
    void OnMouseButtonDoubleClicked(mouse::Button &mouse_button) override {
        if (&mouse_button == &mouse::left_button) {
            mouse::is_captured = !mouse::is_captured;
            os::setCursorVisibility(!mouse::is_captured);
            os::setWindowCapture(    mouse::is_captured);
            OnMouseButtonDown(mouse_button);
        }
    }

    void drawSceneBVH() {
        static Box box;
        static AABB aabb;
        for (u32 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo{scene.geometries[i]};
            aabb.max = geo.type == GeometryType_Tet ? TET_MAX : 1.0f;
            aabb.min = -aabb.max.x;
            if (geo.type == GeometryType_Quad) {
                aabb.min.y = -EPS;
                aabb.max.y = EPS;
            }
            box.vertices = aabb;
            drawBox(box, geo.transform, viewport, BrightYellow, 0.5f);
        }
        drawBVH(scene.bvh, {}, viewport, 1, scene.bvh.height);
    }
};

SlimApp* createApp() {
    return (SlimApp*)new ExampleApp();
}