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
    HUDLine Mode{"Mode: ", "Beauty"};
    HUDLine Shader{ "Shader : "};
    HUDLine Bounces{"Bounces: "};
    HUD hud{{7}, &FPS};

    enum MaterialID { Floor, Mirror, Glass, MaterialCount };

    void updateSelectionInHUD() {
        const char* shader = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case Floor : shader = "Floor";  break;
                case Mirror: shader = "Mirror"; break;
                case Glass : shader = "Glass";  break;
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
    Light key_light{ {1.0f, 1.0f, 0.65f}, {-9, 8, -2}, 100.0f};
    Light *lights{&key_light};

    u8 flags{MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material floor_material{0.8f, 0.2f, flags,
                            2, {Floor_Albedo, Floor_Normal}};
    Material mirror_material{
        0.0f, 0.02f,
        MATERIAL_IS_REFLECTIVE |
              MATERIAL_HAS_NORMAL_MAP,
        2, {0, 1},
        BRDF_CookTorrance, 0.02f, 0.2f,  0.0f,
        IOR_AIR, F0_Aluminium
    };
    Material glass_material {
        0.05f,0.25f,
        MATERIAL_IS_REFRACTIVE |
              MATERIAL_HAS_NORMAL_MAP,
        2,{0, 1},
        BRDF_CookTorrance, 0.08f, 0.05f, 0.0f,
        IOR_GLASS, F0_Glass_Low
    };
    Material *materials{&floor_material};

    OrientationUsingQuaternion wall_rot{75 * DEG_TO_RAD, 0, 90.0f * DEG_TO_RAD};
    Geometry floor {{{},{       },{40, 1, 40}}, GeometryType_Quad, Floor};
    Geometry wall  {{wall_rot, {-6, 8, 15}, {6, 1, 10}}, GeometryType_Quad, Mirror};
    Geometry box{{{},{-9, 8, -2},{2, 2, 3}}, GeometryType_Box, Glass, 0, GEOMETRY_IS_VISIBLE};
    Geometry tet{{{},{-3, 5, -3},{4, 3, 4}}, GeometryType_Tet, Glass, 0, GEOMETRY_IS_VISIBLE};
    Geometry sphere{{{},{ 3, 7, 2  },{4, 3, 3}}, GeometryType_Sphere, Glass, 0, GEOMETRY_IS_VISIBLE};
    Geometry *geometries{&floor};

    Scene scene{{5,1,1,MaterialCount,TextureCount},
                geometries, cameras, lights, materials, textures, texture_files};

    SceneTracer scene_tracer{scene.counts.geometries, scene.mesh_stack_size};
    Selection selection{scene, scene_tracer, projection};
    RayTracingRenderer renderer{scene, scene_tracer, projection, 5,
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

        static vec3 axis{OrientationUsingQuaternion{0.02f, 0.04f, 0.06f}.axis};
        quat rot = quat::AxisAngle(axis, delta_time * 10.0f);
        for (u32 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo = geometries[i];
            if (!(controls::is_pressed::alt && &geo == selection.geometry) &&
                geo.type != GeometryType_Quad && geo.type != GeometryType_Mesh)
                geo.transform.orientation = (geo.transform.orientation * rot).normalized();
        }
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

            if ((key == 'Z' || key == 'X')) {
                char inc = key == 'X' ? 1 : -1;
                if (controls::is_pressed::ctrl) {
                    renderer.settings.max_depth += inc;
                    if (renderer.settings.max_depth == 0) renderer.settings.max_depth = 10;
                    if (renderer.settings.max_depth == 11) renderer.settings.max_depth = 1;
                    Bounces.value = (i32)renderer.settings.max_depth;
                } else if (selection.geometry) {
                    Geometry &geo = *selection.geometry;
                    if (&geo == &floor)
                        geo.material_id = (geo.material_id + inc + MaterialCount) % MaterialCount;
                    else
                        geo.material_id = ((geo.material_id + inc + MaterialCount - 2) % (MaterialCount - 1)) + 1;
                    geo.flags = GEOMETRY_IS_VISIBLE;
                    if (geo.material_id != Glass) geo.flags |= GEOMETRY_IS_SHADOWING;
                    uploadMaterials(scene);
                    updateSelectionInHUD();
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