#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct GeometryApp : SlimApp {
    // Viewport:
    Camera camera{
        .orientation = {-25 * DEG_TO_RAD, 0, 0},
        .position = {-4, 15, -17}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};

    // Scene:
    Light key_light{
        .color = {1.0f, 1.0f, 0.65f},
        .position_or_direction = {20, 20, -5},
        .intensity = 1.1f * 150.0f
    };
    Light fill_light{
        .color = {0.65f, 0.65f, 1.0f},
        .position_or_direction = {-20, 20, -5},
        .intensity = 1.2f * 150.0f
    };
    Light rim_light{
        .color = {1.0f, 0.25f, 0.25f},
        .position_or_direction = {2, 5, 10},
        .intensity = 0.9f * 150.0f
    };
    Light *lights{&key_light};

    Material shapes_material{
        .albedo = {0.8f, 1.0f, 0.8f},
        .brdf = BRDF_Lambert
    };
    Material floor_material{
        .roughness = 0.2f,
        .flags = MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP,
        .texture_count = 2,
        .texture_ids = {0, 1}
    };
    Material *materials{&shapes_material};

    char string_buffers[2][200];
    String texture_files[2]{
        String::getFilePath((char*)"floor_albedo.texture",string_buffers[0],(char*)__FILE__),
        String::getFilePath((char*)"floor_normal.texture",string_buffers[1],(char*)__FILE__)
    };

    Texture floor_albedo_map;
    Texture floor_normal_map;
    Texture *textures = &floor_albedo_map;

    Geometry box{
        .transform = {
            .orientation = {0.02f, 0.04f, 0.0f},
            .position = {-11, 8, 5},
            .scale = {3, 4, 5}
        },
        .type = GeometryType_Box
    };
    Geometry tet{
        .transform = {
            .orientation = {0.02f, 0.04f, 0.06f},
            .position = {-3, 6, 14},
            .scale = {4, 3, 5}
        },
        .type = GeometryType_Tet
    };
    Geometry sphere{
        .transform = {
            .position = {3, 6, 2},
            .scale = {5, 4, 3}
        },
        .type = GeometryType_Sphere
    };
    Geometry floor{
        .transform = {
            .scale = {40, 1, 40}
        },
        .type = GeometryType_Quad,
        .material_id = 1
    };
    Geometry *geometries{&box};

    SceneCounts counts{
        .geometries = 4,
        .cameras = 1,
        .lights = 3,
        .materials = 2,
        .textures = 2
    };
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;
    quat rotation{tet.transform.orientation};

    void OnUpdate(f32 delta_time) override {
        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        quat rot = quat::AxisAngle(rotation.axis, delta_time * 10.0f);
        for (u8 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo = geometries[i];
            if (!(controls::is_pressed::alt && &geo == selection.geometry) &&
                geo.type != GeometryType_Quad)
                geo.transform.orientation = (geo.transform.orientation * rot).normalized();
        }
    }

    void OnRender() override {
        canvas.clear();

        ray_tracer.render(viewport);
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);

        canvas.drawToWindow();
    }

    // HUD:
    HUDLine Transparent_hud_line{(char*)"Transparent : ",
                                 (char*)"On",
                                 (char*)"Off",
                                 &transparent,
                                 true};
    HUD hud{{1}, &Transparent_hud_line};

    bool transparent = false;

    void OnKeyChanged(u8 key, bool is_pressed) override {
        if (!is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'T' && selection.geometry) {
                u8 &flags = selection.geometry->flags;
                if (flags & GEOMETRY_IS_TRANSPARENT)
                    flags &= ~GEOMETRY_IS_TRANSPARENT;
                else
                    flags |= GEOMETRY_IS_TRANSPARENT;

                transparent = flags & GEOMETRY_IS_TRANSPARENT;
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
};

SlimApp* createApp() {
    return (SlimApp*)new GeometryApp();
}