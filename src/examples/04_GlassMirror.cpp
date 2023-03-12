#include "../slim/renderer/raytracer.h"
#include "../slim/draw/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/app.h"

// Or using the single-header file:
//#include "../slim.H"

struct ClassicShadersApp : SlimApp {
    // Viewport:
    Camera camera{
            {-4, 15, -17},
            {-25 * DEG_TO_RAD, 0, 0}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};

    // Scene:
    Light key_light{
            {20, 20, -5},
            {1.0f, 1.0f, 0.65f},
            1.1f * 150.0f};
    Light fill_light{
            {-20, 20, -5},
            {0.65f, 0.65f, 1.0f},
            1.2f * 150.0f
    };
    Light rim_light{
            {5, 5, 20},
            {1.0f, 0.25f, 0.25f},
            0.9f * 150.0f
    };
    Light *lights{&key_light};

    enum MATERIAL {
        MATERIAL_ROUGH,
        MATERIAL_MIRROR,
        MATERIAL_GLASS,
        MATERIAL_COUNT
    };

    Material rough_material{
        BRDF_CookTorrance,
        0.2f, 0.0f,
        MATERIAL_HAS_ALBEDO_MAP | MATERIAL_HAS_NORMAL_MAP,
        0,
        {0.8f, 1.0f, 0.8f}
    };
    Material mirror_material{
        BRDF_CookTorrance, 0.0f, 1.0f,
        MATERIAL_IS_REFLECTIVE | MATERIAL_HAS_NORMAL_MAP,
        0,
        0.1f, 0.9f
    };
    Material glass_material{
        BRDF_CookTorrance,
        0.1f, 0.0f,
        MATERIAL_IS_REFRACTIVE | MATERIAL_HAS_NORMAL_MAP,
        0,
        0.1f,
        F0_Glass_Low,
        0.0f,
        1.0f,
        0.1f,
        IOR_GLASS
    };

    Material *materials{&rough_material};

    char string_buffers[2][200];
    String texture_files[2]{
            String::getFilePath((char*)"floor_albedo.texture",string_buffers[0],(char*)__FILE__),
            String::getFilePath((char*)"floor_normal.texture",string_buffers[1],(char*)__FILE__)
    };

    Texture floor_albedo_map;
    Texture floor_normal_map;
    Texture *textures = &floor_albedo_map;



    Geometry floor{{{}, {}, {40, 1, 40}}, GeometryType_Quad, MATERIAL_ROUGH};
    Geometry wall{{{-15, 5, 5}, {0.0f, 0.0f, 90.0f * DEG_TO_RAD}, {4, 1, 8}}, GeometryType_Quad, MATERIAL_MIRROR};
    Geometry box{{{3, 4, 0}, {0.02f, 0.04f, 0.0f}, {2.5f}}, GeometryType_Box, MATERIAL_GLASS, GEOMETRY_IS_VISIBLE};
    Geometry tet{{{-3, 4, 12}, {0.02f, 0.04f, 0.06f}, {2.5f}}, GeometryType_Tet, MATERIAL_GLASS, GEOMETRY_IS_VISIBLE};
    Geometry sphere{{{-9, 5, 3}, {}, {2.5f}}, GeometryType_Sphere, MATERIAL_GLASS, GEOMETRY_IS_VISIBLE};
    Geometry *geometries{&floor};

    SceneCounts counts{5, 1, 3, MATERIAL_COUNT, 2};
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{scene, (u32)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;
    quat rotation{tet.transform.rotation};

    ClassicShadersApp() {
        updateSelectionInHUD();
        rough_material.texture_count = 2;
        rough_material.texture_ids[0] = 0;
        rough_material.texture_ids[1] = 1;

        mirror_material.texture_count = 2;
        mirror_material.texture_ids[0] = 0;
        mirror_material.texture_ids[1] = 1;
        mirror_material.uv_repeat = 0.25f;
        mirror_material.normal_magnitude = 0.1f;

        glass_material.texture_count = 2;
        glass_material.texture_ids[0] = 0;
        glass_material.texture_ids[1] = 1;
        glass_material.uv_repeat = 0.125f;
        glass_material.normal_magnitude = 0.1f;
    }

    void OnRender() override {
        canvas.clear();

        ray_tracer.render(viewport);
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);

        canvas.drawToWindow();
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
                geo.transform.rotation = (geo.transform.rotation * rot).normalized();
        }
    }

    // HUD:
    HUDLine shader_line{ (char*)"Shader : "};
    HUDLine bounces_line{(char*)"Bounces: "};
    HUD hud{{2}, &shader_line};

    void updateSelectionInHUD() {
        char* shader = (char*)"";
        if (selection.geometry) {
            char* shader = (char*)"";
            switch (selection.geometry->material_id) {
                case MATERIAL_MIRROR: shader = (char*)"Mirror";  break;
                case MATERIAL_GLASS : shader = (char*)"Glass";   break;
                case MATERIAL_ROUGH : shader = (char*)"Lambert"; break;
                default: break;
            }
            shader_line.value.string.copyFrom(shader, 0);
        }
        bounces_line.value = (i32)ray_tracer.max_depth;
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
            if (key == 'Z' || key == 'X') {
                char inc = key == 'X' ? 1 : -1;
                if (controls::is_pressed::ctrl) {
                    ray_tracer.max_depth += inc;
                    if (ray_tracer.max_depth == 0) ray_tracer.max_depth = 10;
                    if (ray_tracer.max_depth == 11) ray_tracer.max_depth = 1;
                } else if (selection.geometry) {
                    Geometry &geo = *selection.geometry;
                    geo.material_id = (geo.material_id + inc + MATERIAL_COUNT) % MATERIAL_COUNT;
                    geo.flags = geo.material_id == MATERIAL_GLASS ?
                            GEOMETRY_IS_VISIBLE : (
                            GEOMETRY_IS_VISIBLE | GEOMETRY_IS_SHADOWING
                            );
                }
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