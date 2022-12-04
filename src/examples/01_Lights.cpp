#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"

// Or using the single-header file:
//#include "../slim.h"

struct LightsApp : SlimApp {
    // Viewport:
    Camera camera{
            {0, 7, -11},
            {-25 * DEG_TO_RAD, 0, 0}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};
    bool antialias = false;

    // HUD:
    HUDLine AA{(char*)"AA : ",
               (char*)"On",
               (char*)"Off",
               &antialias,
               true};
    HUDSettings hud_settings{1};
    HUD hud{hud_settings, &AA};

    // Scene:
    Light key_light{ {10, 10, -5}, {1.0f, 1.0f, 0.65f}, 3.3f * 40.0f};
    Light fill_light{ {-10, 10, -5}, {0.65f, 0.65f, 1.0f}, 3.1f * 40.0f};
    Light rim_light{ {2, 5, 12}, {1.0f, 0.25f, 0.25f}, 3.5f * 40.0f};
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

    RayTracer ray_tracer{canvas, scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;

    LightsApp() {
        floor_material.texture_count = 2;
        floor_material.texture_ids[0] = 0;
        floor_material.texture_ids[1] = 1;
    }

    void OnRender() override {
        canvas.clear();

        ray_tracer.render(camera);

        if (controls::is_pressed::alt)
            drawSelection(selection, viewport, scene);

        if (hud.enabled)
            drawHUD(hud, canvas);

        canvas.drawToWindow();
    }

    void OnUpdate(f32 delta_time) override {
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
            if (key == controls::key_map::tab)
                hud.enabled = !hud.enabled;
            else if (key == 'Q') {
                canvas.antialias = canvas.antialias == NoAA ? SSAA : NoAA;
                antialias = canvas.antialias == SSAA;
            }
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