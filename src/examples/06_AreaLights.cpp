#include "./textures.h"

#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/renderer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

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
    HUDLine Cut {"Cut : ", "Off","On", &cutout};
    HUDLine Mode{"Mode: ", "Beauty"};
    HUDLine Shader{ "Shader: "};
    HUD hud{{7}, &FPS};

    enum MaterialID { Floor, Rough, Mirror, Emissive1, Emissive2, MaterialCount };

    void updateSelectionInHUD() {
        const char* shader = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case     Floor: shader = "Floor";  break;
                case     Rough: shader = "Rough";   break;
                case    Mirror: shader = "Mirror";   break;
                case Emissive1: shader = "Emissive 1"; break;
                case Emissive2: shader = "Emissive 2"; break;
                default: break;
            }
        }
        Shader.value.string.copyFrom(shader, 0);

        selection.changed = false;
    }

    // Viewport:
    Camera camera{{-25 * DEG_TO_RAD, 0, 0}, {-4, 15, -17}}, *cameras{&camera};
    CameraRayProjection projection;
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:

    u8 flags{MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material floor_material{0.8f, 0.2f, flags,
                            2, {Floor_Albedo, Floor_Normal}};
    Material rough_material{0.8f,0.2f,MATERIAL_HAS_NORMAL_MAP,2, {0, Floor_Normal},
                   BRDF_CookTorrance, 0.25f, 0.25f};
    Material mirror_material{
        0.1f, 0.02f,
        MATERIAL_IS_REFLECTIVE |
        MATERIAL_HAS_NORMAL_MAP,
        2, {0, 1},
        BRDF_CookTorrance, 0.1f, 0.3f,  0.0f,
        IOR_AIR, F0_Aluminium
    };
    Material emissive1_material{0.0f, 0.0f,
        MATERIAL_IS_EMISSIVE, 0, {},
        BRDF_CookTorrance, 1.0f, 1.0f,
        {0.9f, 0.5f, 0.5f}
    };
    Material emissive2_material{0.0f, 0.0f,
        MATERIAL_IS_EMISSIVE, 0, {},
        BRDF_CookTorrance, 1.0f, 1.0f,
        {0.5f, 0.5f, 0.9f}
    };
    Material *materials{&floor_material};

    Geometry floor {{{},{       },{40, 1, 40}}, GeometryType_Quad, Floor};
    Geometry wall1 {{{0.0f, 0.0f, -90.0f * DEG_TO_RAD}, {-12, 10, 5}, {4, 1, 8}}, GeometryType_Quad, Emissive1};
    Geometry wall2 {{{0.0f, 0.0f, +90.0f * DEG_TO_RAD}, {+15, 5, 5}, {4, 1, 8}}, GeometryType_Quad, Emissive2};
    Geometry sphere{{{0.0f, 0.0f, -15.0f * DEG_TO_RAD},{-7.5f, 4.5f, 2},{2, 4, 3}}, GeometryType_Sphere, Rough};
    Geometry wall3 {{{0, 0,90*DEG_TO_RAD},{6, 6, 1},{5, 1, 5}}, GeometryType_Quad, Mirror};
    Geometry *geometries{&floor};

    Scene scene{{5,1,0,MaterialCount,TextureCount},
                geometries, cameras, nullptr, materials, textures, texture_files};

    SceneTracer scene_tracer{scene.counts.geometries, scene.mesh_stack_size};
    Selection selection{scene, scene_tracer, projection};
    RayTracingRenderer renderer{scene, scene_tracer, projection, 3,
                                Cathedral_SkyboxColor,
                                Cathedral_SkyboxRadiance,
                                Cathedral_SkyboxIrradiance};

    void OnUpdate(f32 delta_time) override {
        projection.reset(camera, canvas.dimensions, canvas.antialias == SSAA);

        i32 fps = (i32)render_timer.average_frames_per_second;
        FPS.value = fps;
        FPS.value_color = fps >= 60 ? Green : (fps >= 24 ? Cyan : (fps < 12 ? Red : Yellow));

        if (!mouse::is_captured) selection.manipulate(viewport);
        if (selection.changed) updateSelectionInHUD();
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
            if (key == '1') renderer.settings.render_mode = RenderMode_Beauty;
            if (key == '2') renderer.settings.render_mode = RenderMode_Depth;
            if (key == '3') renderer.settings.render_mode = RenderMode_Normals;
            if (key == '4') renderer.settings.render_mode = RenderMode_NormalMap;
            if (key == '5') renderer.settings.render_mode = RenderMode_MipLevel;
            if (key == '6') renderer.settings.render_mode = RenderMode_UVs;
            const char* mode;
            switch (renderer.settings.render_mode) {
                case RenderMode_Beauty:    mode = "Beauty"; break;
                case RenderMode_Depth:     mode = "Depth"; break;
                case RenderMode_Normals:   mode = "Normals"; break;
                case RenderMode_NormalMap: mode = "Normal Maps"; break;
                case RenderMode_MipLevel:  mode = "Mip Level"; break;
                case RenderMode_UVs:       mode = "UVs"; break;
            }
            Mode.value.string = mode;

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
                    geo.material_id = ((geo.material_id + inc + MaterialCount - 2) % (MaterialCount - 1)) + 1;
                    updateSelectionInHUD();
                    uploadMaterials(scene);
                }
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