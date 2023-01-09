#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"

// Or using the single-header file:
//#include "../slim.H"

struct LightsApp : SlimApp {
    // Viewport:
    Camera camera{
            {-4, 15, -17},
            {-25 * DEG_TO_RAD, 0, 0}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};

    // Scene:
    Light key_light{ {20, 20, -5}, {1.0f, 1.0f, 0.65f}, 1.1f * 150.0f};
    Light fill_light{ {-20, 20, -5}, {0.65f, 0.65f, 1.0f}, 1.2f * 150.0f};
    Light rim_light{ {2, 5, 10}, {1.0f, 0.25f, 0.25f}, 0.9f * 150.0f};
    Light *lights{&key_light};

    Material floor_material{BRDF_CookTorrance, 0.5f, 0.0f, MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material *materials{&floor_material};

    char string_buffers[2][200];
    String texture_files[2]{
        String::getFilePath((char*)"floor_albedo.texture",string_buffers[0],(char*)__FILE__),
        String::getFilePath((char*)"floor_normal.texture",string_buffers[1],(char*)__FILE__)
    };

    Texture floor_albedo_map;
    Texture floor_normal_map;
    Texture *textures = &floor_albedo_map;

    Geometry plane{{{}, {}, {40, 1, 40}}, GeometryType_Quad};
    Geometry *geometries{&plane};

    SceneCounts counts{1, 1, 3, 1, 2};
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;

    bool draw_bvh = false;
    bool draw_ssb = false;
    bool antialias = false;

    // HUD:
    HUDLine FPS_hud_line{(char*)"FPS : "};
    HUDLine GPU_hud_line{(char*)"GPU : ",
                         (char*)"On",
                         (char*)"Off",
                         &ray_tracer.use_gpu,
                         true};
    HUDLine AA_hud_line{(char*)"SSAA: ",
                        (char*)"On",
                        (char*)"Off",
                        &antialias,
                        true};
    HUDLine Mode_hud_line{(char*)"Mode : ", (char*)"Beauty"};
    HUDLine BVH_hud_line{(char*)"BVH : ",
                         (char*)"On",
                         (char*)"Off",
                         &draw_bvh,
                         true};
    HUDLine SSB_hud_line{(char*)"SSB : ",
                         (char*)"On",
                         (char*)"Off",
                         &draw_ssb,
                         true};
    HUDSettings hud_settings{6};
    HUD hud{hud_settings, &FPS_hud_line};

    LightsApp() {
        floor_material.texture_count = 2;
        floor_material.texture_ids[0] = 0;
        floor_material.texture_ids[1] = 1;
    }

    void OnRender() override {
        canvas.clear();

        ray_tracer.render(viewport);

        if (controls::is_pressed::alt)
            drawSelection(selection, viewport, scene);

        if (hud.enabled)
            drawHUD(hud, canvas);

        canvas.drawToWindow();
    }

    void OnUpdate(f32 delta_time) override {
        FPS_hud_line.value = (i32)render_timer.average_frames_per_second;
        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);
    }

    void OnKeyChanged(u8 key, bool is_pressed) override {
        Move &move = viewport.navigation.move;
        Turn &turn = viewport.navigation.turn;
        if (key == 'X') turn.left     = is_pressed;
        if (key == 'C') turn.right    = is_pressed;
        if (key == 'R') move.up       = is_pressed;
        if (key == 'F') move.down     = is_pressed;
        if (key == 'W') move.forward  = is_pressed;
        if (key == 'S') move.backward = is_pressed;
        if (key == 'A') move.left     = is_pressed;
        if (key == 'D') move.right    = is_pressed;
        if (!is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'H') draw_bvh = !draw_bvh;
            if (key == 'B') draw_ssb = !draw_ssb;
            if (key == 'G') ray_tracer.use_gpu = !ray_tracer.use_gpu;
            if (key == 'Z') { antialias = !antialias; canvas.antialias = antialias ? SSAA : NoAA; }
            if (key == '1') { ray_tracer.render_mode = RenderMode_Beauty; Mode_hud_line.value.string = (char*)"Beauty"; }
            if (key == '2') { ray_tracer.render_mode = RenderMode_Depth; Mode_hud_line.value.string = (char*)"Depth"; }
            if (key == '3') { ray_tracer.render_mode = RenderMode_Normals; Mode_hud_line.value.string = (char*)"Normals"; }
            if (key == '4') { ray_tracer.render_mode = RenderMode_NormalMap; Mode_hud_line.value.string = (char*)"Normal Maps"; }
            if (key == '5') { ray_tracer.render_mode = RenderMode_MipLevel; Mode_hud_line.value.string = (char*)"Mip Level"; }
            if (key == '6') { ray_tracer.render_mode = RenderMode_UVs; Mode_hud_line.value.string = (char*)"UVs"; }
        }
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
};

SlimApp* createApp() {
    return (SlimApp*)new LightsApp();
}