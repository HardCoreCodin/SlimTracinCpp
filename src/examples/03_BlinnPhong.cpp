#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/renderer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct ExampleApp : SlimApp {
    bool draw_BVH = false;
    bool antialias = false;
    bool use_gpu = USE_GPU_BY_DEFAULT;

    // HUD:
    HUDLine FPS {"FPS : "};
    HUDLine GPU {"GPU : ", "On", "Off",&use_gpu};
    HUDLine AA  {"AA  : ", "On", "Off",&antialias};
    HUDLine BVH {"BVH : ", "On", "Off",&draw_BVH};
    HUDLine Mode{"Mode: ", "Beauty"};
    HUDLine Shader{   "Shader   : "};
    HUDLine Roughness{"Roughness: "};
    HUD hud{{7}, &FPS};

    // Viewport:
    Camera camera{{-25 * DEG_TO_RAD, 0, 0}, {-4, 15, -17}}, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light key_light{ {1.0f, 1.0f, 0.65f}, {20, 20, -5}, 1.1f * 200.0f};
    Light fill_light{{0.65f, 0.65f, 1.0f}, {-20, 20, -5}, 1.2f * 200.0f };
    Light rim_light{ {1.0f, 0.25f, 0.25f}, {5, 8, 15}, 0.9f * 200.0f};
    Light *lights{&key_light};

    enum MATERIAL {
        MATERIAL_FLOOR,
        MATERIAL_LAMBERT,
        MATERIAL_PHONG,
        MATERIAL_BLINN,
        MATERIAL_COUNT
    };

    u8 flags{MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material floor_material{0.8f, 0.2f, flags, 2, {0, 1}};
    Material Lambert{1.0f,0.7f,0,0, {},
                     BRDF_Lambert, 1.0f, 0.25f};
    Material Phong{1.0f,0.5f,MATERIAL_HAS_NORMAL_MAP,2, {0, 1},
                   BRDF_Phong, 0.75f, 0.25f, 0.0f, 1.0f, {1.0f, 1.0f, 0.4f}};
    Material Blinn{{1.0f, 0.3f, 1.0f},0.5f,MATERIAL_HAS_NORMAL_MAP,2, {0, 1},
                   BRDF_Blinn, 0.75f, 0.25f, 0.0f, 1.0f, {1.0f, 0.4f, 1.0f}};
    Material *materials{&floor_material};

    Geometry floor {{{},{       },{40, 1, 40}},GeometryType_Quad,  MATERIAL_FLOOR};
    Geometry box{   {{},{-9, 8, -2},{2, 2, 3}},GeometryType_Box,   MATERIAL_LAMBERT};
    Geometry tet{   {{},{-3, 6, 14},{4, 3, 4}},GeometryType_Tet,   MATERIAL_PHONG};
    Geometry sphere{{{},{3, 6, 2  },{4, 3, 3}},GeometryType_Sphere,MATERIAL_BLINN};
    Geometry *geometries{&floor};

    Texture textures[2];
    char string_buffers[2][200]{};
    String texture_files[2]{
        String::getFilePath("floor_albedo.texture",string_buffers[0],__FILE__),
        String::getFilePath("floor_normal.texture",string_buffers[1],__FILE__),
    };

    Scene scene{{4,1,3,MATERIAL_COUNT,2},
                geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracingRenderer renderer{scene};

    void OnUpdate(f32 delta_time) override {
        i32 fps = (i32)render_timer.average_frames_per_second;
        FPS.value = fps;
        FPS.value_color = fps >= 60 ? Green : (fps >= 24 ? Cyan : (fps < 12 ? Red : Yellow));

        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (selection.changed) updateSelectionInHUD();
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        static vec3 axis{OrientationUsingQuaternion{0.02f, 0.04f, 0.06f}.axis};
        quat rot = quat::AxisAngle(axis, delta_time * 10.0f);
        for (u32 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo = geometries[i];
            if (!(controls::is_pressed::alt && &geo == selection.geometry) &&
                geo.type != GeometryType_Quad && geo.type != GeometryType_Mesh)
                geo.transform.orientation = (geo.transform.orientation * rot).normalized();
        }
    }

    void updateSelectionInHUD() {
        const char* shader = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case MATERIAL_BLINN: shader = "Blinn"; break;
                case MATERIAL_PHONG: shader = "Phong"; break;
                default:             shader = "Lambert"; break;
            }
            Roughness.value = materials[geo.material_id].roughness;
        } else
            Roughness.value.string.copyFrom("", 0);

        Shader.value.string.copyFrom(shader, 0);

        selection.changed = false;
    }

    void OnRender() override {
        renderer.render(viewport, true, use_gpu);
        if (draw_BVH) {
            for (u32 i = 0; i < scene.counts.geometries; i++)
                if (geometries[i].type == GeometryType_Mesh)
                    drawBVH(scene.meshes[geometries[i].id].bvh, geometries[i].transform, viewport);
            drawBVH(scene.bvh, {}, viewport);
        }
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);
        canvas.drawToWindow();
    }

    void OnKeyChanged(u8 key, bool is_pressed) override {
        if (is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'A' && controls::is_pressed::shift) { antialias = !antialias; canvas.antialias = antialias ? SSAA : NoAA; }
            if (key == 'G') use_gpu = !use_gpu;
            if (key == 'B') draw_BVH = !draw_BVH;
            if (key == '1') renderer.settings.render_mode = RenderMode_Beauty;
            if (key == '2') renderer.settings.render_mode = RenderMode_Depth;
            if (key == '3') renderer.settings.render_mode = RenderMode_Normals;
            if (key == '4') renderer.settings.render_mode = RenderMode_NormalMap;
            if (key == '5') renderer.settings.render_mode = RenderMode_MipLevel;
            if (key == '6') renderer.settings.render_mode = RenderMode_UVs;
            if ((key == 'Z' || key == 'X') && selection.geometry) {
                Geometry &geo = *selection.geometry;
                char inc = key == 'X' ? 1 : -1;
                if (controls::is_pressed::ctrl) {
                    Material &M = scene.materials[geo.material_id];
                    M.roughness = clampedValue(M.roughness + (f32)inc * 0.05f, 0.05f, 1.0f);
                } else if (selection.geometry != &floor)
                    geo.material_id = ((geo.material_id + inc + MATERIAL_COUNT - 2) % (MATERIAL_COUNT - 1)) + 1;

                uploadMaterials(scene);
                updateSelectionInHUD();
            }
            const char* mode;
            switch ( renderer.settings.render_mode) {
                case RenderMode_Beauty:    mode = "Beauty"; break;
                case RenderMode_Depth:     mode = "Depth"; break;
                case RenderMode_Normals:   mode = "Normals"; break;
                case RenderMode_NormalMap: mode = "Normal Maps"; break;
                case RenderMode_MipLevel:  mode = "Mip Level"; break;
                case RenderMode_UVs:       mode = "UVs"; break;
            }
            Mode.value.string = mode;
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
    return (SlimApp*)new ExampleApp();
}