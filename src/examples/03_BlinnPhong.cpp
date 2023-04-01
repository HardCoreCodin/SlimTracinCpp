#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.H"

struct ClassicShadersApp : SlimApp {
    // Viewport:
    Camera camera{
        .orientation = {-25 * DEG_TO_RAD, 0, 0},
        .position = {0, 7, -11}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light key_light{
        .color = {1.0f, 1.0f, 0.65f},
        .position_or_direction = {10, 10, -5},
        .intensity = 1.1f * 40.0f
    };
    Light fill_light{
        .color = {0.65f, 0.65f, 1.0f},
        .position_or_direction = {-10, 10, -5},
        .intensity = 1.2f * 40.0f
    };
    Light rim_light{
        .color = {1.0f, 0.25f, 0.25f},
        .position_or_direction = {2, 5, 12},
        .intensity = 0.9f * 40.0f
    };
    Light *lights{&key_light};

    enum MATERIAL {
        MATERIAL_LAMBERT,
        MATERIAL_PHONG,
        MATERIAL_BLINN,
        MATERIAL_COUNT
    };

    Material lambert_material{
        .albedo = {0.8f, 1.0f, 0.8f},
        .brdf = BRDF_Lambert,
    };
    Material phong_material{
        .albedo = {0.2f, 0.2f, 0.3f},
        .reflectivity = {1.0f, 1.0f, 0.4f},
        .roughness = 0.5f,
        .brdf = BRDF_Phong,
    };
    Material blinn_material{
        .albedo = {1.0f, 0.3f, 1.0f},
        .reflectivity = {1.0f, 0.4f, 1.0f},
        .roughness = 0.5f,
        .brdf = BRDF_Blinn,
    };
    Material *materials{&lambert_material};

    Geometry floor{
        .transform = {
            .scale = {40, 1, 40}
        },
        .type = GeometryType_Quad,
        .material_id = MATERIAL_LAMBERT
    };
    Geometry box{
        .transform = {
            .orientation = {0.02f, 0.04f, 0.0f},
            .position = {-9, 5, 3},
            .scale = 2.5f
        },
        .type = GeometryType_Box,
        .material_id = MATERIAL_PHONG
    };
    Geometry tet{
        .transform = {
            .orientation = {0.02f, 0.04f, 0.06f},
            .position = {-3, 4, 12},
            .scale = 2.5f
        },
        .type = GeometryType_Tet,
        .material_id = MATERIAL_PHONG
    };
    Geometry sphere{
        .transform = {
            .position = {3, 4, 0},
            .scale = 2.5f
        },
        .type = GeometryType_Sphere,
        .material_id = MATERIAL_BLINN
    };
    Geometry *geometries{&floor};

    SceneCounts counts{
        .geometries = 4,
        .cameras = 1,
        .lights = 3,
        .materials = MATERIAL_COUNT
    };
    Scene scene{counts, nullptr, geometries, cameras, lights, materials};
    Selection selection;

    RayTracer ray_tracer{scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;
    quat rotation{tet.transform.orientation};

    // HUD:
    HUDLine shader_line{   (char*)"Surface   : "};
    HUDLine roughness_line{(char*)"Roughness : "};
    HUD hud{{2}, &shader_line};

    void updateSelectionInHUD() {
        char* shader = (char*)"";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case MATERIAL_BLINN: shader = (char*)"Blinn"; break;
                case MATERIAL_PHONG: shader = (char*)"Phong"; break;
                default:             shader = (char*)"Lambert"; break;
            }
            roughness_line.value = materials[geo.material_id].roughness;
        } else
            roughness_line.value.string.copyFrom((char*)"", 0);

        shader_line.value.string.copyFrom(shader, 0);
    }

    ClassicShadersApp() {
        updateSelectionInHUD();
    }

    void OnUpdate(f32 delta_time) override {
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);
        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (selection.changed) {
            selection.changed = false;
            updateSelectionInHUD();
        }

        quat rot = quat::AxisAngle(rotation.axis, delta_time * 10.0f);
        for (u8 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo = geometries[i];
            if (!(controls::is_pressed::alt && &geo == selection.geometry) &&
                geo.type != GeometryType_Quad)
                geo.transform.orientation = (geo.transform.orientation * rot).normalized();
        }
    }

    void OnRender() override {
        static Transform transform;
        canvas.clear();

        ray_tracer.render(viewport);
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);

        canvas.drawToWindow();
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

        if (is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if ((key == 'Z' || key == 'X') && selection.geometry) {
                Geometry &geo = *selection.geometry;
                char inc = key == 'X' ? 1 : -1;
                if (controls::is_pressed::ctrl) {
                    Material &M = scene.materials[geo.material_id];
                    M.roughness = clampedValue(M.roughness + (f32)inc * 0.05f, 0.05f, 1.0f);
                } else
                    geo.material_id = (geo.material_id + inc + MATERIAL_COUNT) % MATERIAL_COUNT;

                updateSelectionInHUD();
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
    return (SlimApp*)new ClassicShadersApp();
}