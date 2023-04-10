#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/ray_tracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct ExampleApp : SlimApp {
    bool draw_BVH = false;
    bool antialias = false;
    bool cutout = false;
    bool use_gpu = USE_GPU_BY_DEFAULT;

    // HUD:
    HUDLine FPS {"FPS : "};
    HUDLine GPU {"GPU : ", "On", "Off",&use_gpu};
    HUDLine AA  {"AA  : ", "On", "Off",&antialias};
    HUDLine BVH {"BVH : ", "On", "Off",&draw_BVH};
    HUDLine Cut {"Cut : ", "On", "Off",&cutout};
    HUDLine Mode{"Mode: ", "Beauty"};
    HUDLine Shader{ "Material : "};
    HUD hud{{7}, &FPS};

    // Viewport:
    Camera camera{{-25 * DEG_TO_RAD, 0, 0}, {-4, 15, -17}}, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light key_light{ {1.0f, 1.0f, 0.65f}, {10, 13, -7}, 130.0f};
    Light fill_light{{0.65f, 0.65f, 1.0f}, {-15, 15, -3}, 130.0f};
    Light rim_light{ {1.0f, 0.25f, 0.25f}, {8, 7, 15}, 130.0f};
    Light *lights{&key_light};

    enum MATERIAL {
        MATERIAL_FLOOR,
        MATERIAL_SHAPE,
        MATERIAL_EMISSIVE1,
        MATERIAL_EMISSIVE2,
        MATERIAL_COUNT
    };

    u8 flags{MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material floor_material{0.8f, 0.2f, flags, 2, {0, 1}};
    Material shape_material{0.8f, 0.7f};
    Material Emissive1{0.0f, 0.0f,
        MATERIAL_IS_EMISSIVE, 0, {},
        BRDF_CookTorrance, 1.0f, 1.0f,
        {0.9f, 0.5f, 0.5f}
    };
    Material Emissive2{0.0f, 0.0f,
        MATERIAL_IS_EMISSIVE, 0, {},
        BRDF_CookTorrance, 1.0f, 1.0f,
        {0.5f, 0.5f, 0.9f}
    };
    Material *materials{&floor_material};

    Geometry floor {{{},{       },{40, 1, 40}},GeometryType_Quad,  MATERIAL_FLOOR};
    Geometry wall1 {{{0.0f, 0.0f, -90.f * DEG_TO_RAD}, {-15, 5, 5}, {4, 1, 8}}, GeometryType_Quad, MATERIAL_EMISSIVE1};
    Geometry wall2 {{{0.0f, 0.0f, +90.f * DEG_TO_RAD}, {+15, 5, 5}, {4, 1, 8}}, GeometryType_Quad, MATERIAL_EMISSIVE2};
    Geometry sphere1{{{},{-3, 6, 14},{4, 3, 2}},GeometryType_Sphere, MATERIAL_SHAPE};
    Geometry sphere2{{{},{3, 6, 2},{2, 4, 3}},GeometryType_Sphere, MATERIAL_SHAPE};
    Geometry *geometries{&floor};

    Texture textures[2];
    char string_buffers[2][200]{};
    String texture_files[2]{
        String::getFilePath("floor_albedo.texture",string_buffers[0],__FILE__),
        String::getFilePath("floor_normal.texture",string_buffers[1],__FILE__),
    };

    Scene scene{{5,1,3,MATERIAL_COUNT,2}, nullptr,
                geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{scene};

    void OnUpdate(f32 delta_time) override {
        i32 fps = (i32)render_timer.average_frames_per_second;
        FPS.value = fps;
        FPS.value_color = fps >= 60 ? Green : (fps >= 24 ? Cyan : (fps < 12 ? Red : Yellow));

        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (selection.changed) updateSelectionInHUD();
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);
    }

    void updateSelectionInHUD() {
        const char* shader = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case MATERIAL_FLOOR: shader = "Floor";  break;
                case MATERIAL_SHAPE: shader = "Shape";   break;
                case MATERIAL_EMISSIVE1 : shader = "Emissive 1"; break;
                case MATERIAL_EMISSIVE2 : shader = "Emissive 2"; break;
                default: break;
            }
        }
        Shader.value.string.copyFrom(shader, 0);

        selection.changed = false;
    }

    void OnRender() override {
        ray_tracer.render(viewport, true, use_gpu);
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
        if (!is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'A' && controls::is_pressed::shift) { antialias = !antialias; canvas.antialias = antialias ? SSAA : NoAA; }
            if (key == 'G') use_gpu = !use_gpu;
            if (key == 'B') draw_BVH = !draw_BVH;
            if (key == '1') ray_tracer.settings.render_mode = RenderMode_Beauty;
            if (key == '2') ray_tracer.settings.render_mode = RenderMode_Depth;
            if (key == '3') ray_tracer.settings.render_mode = RenderMode_Normals;
            if (key == '4') ray_tracer.settings.render_mode = RenderMode_NormalMap;
            if (key == '5') ray_tracer.settings.render_mode = RenderMode_MipLevel;
            if (key == '6') ray_tracer.settings.render_mode = RenderMode_UVs;
            if (selection.geometry && selection.geometry != &floor && (key == 'Z' || key == 'X' || key == 'C')) {
                Geometry &geo = *selection.geometry;
                if (key == 'C') {
                    if (geo.flags & GEOMETRY_IS_TRANSPARENT)
                        geo.flags &= ~GEOMETRY_IS_TRANSPARENT;
                    else
                        geo.flags |= GEOMETRY_IS_TRANSPARENT;

                    cutout = geo.flags & GEOMETRY_IS_TRANSPARENT;
                } else if (selection.geometry->type == GeometryType_Quad){
                    char inc = key == 'X' ? 1 : -1;
                    geo.material_id = ((geo.material_id + inc + MATERIAL_COUNT - 2) % (MATERIAL_COUNT - 1)) + 1;
                    updateSelectionInHUD();
                    ray_tracer.uploadMaterials();
                }
            }
            const char* mode;
            switch ( ray_tracer.settings.render_mode) {
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