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
    bool cutout = false;
    bool use_gpu = USE_GPU_BY_DEFAULT;

    // HUD:
    HUDLine FPS {"FPS : "};
    HUDLine GPU {"GPU : ", "On", "Off",&use_gpu};
    HUDLine AA  {"AA  : ", "On", "Off",&antialias};
    HUDLine BVH {"BVH : ", "On", "Off",&draw_BVH};
    HUDLine Cut {"Cut : ", "On", "Off",&cutout};
    HUDLine Mode{"Mode: ", "Beauty"};
    HUDLine Shader{   "Material : "};
    HUDLine Roughness{"Roughness: "};
    HUDLine Bounces{  "Bounces  : "};
    HUD hud{{9}, &FPS};

    // Viewport:
    Camera camera{{-25 * DEG_TO_RAD, 0, 0}, {0, 7, -11}}, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light key_light{ {1.0f, 1.0f, 0.65f}, {8, 10, -5}, 1.1f * 40.0f};
    Light fill_light{{0.65f, 0.65f, 1.0f}, {-8, 10, -5}, 1.2f * 40.0f };
    Light rim_light{ {1.0f, 0.25f, 0.25f}, {6, 5, 2}, 0.9f * 40.0f};
    Light aux_light{ {0.25f, 1.0f, 0.25f}, {-2, 4, 2}, 0.9f * 20.0f};
    Light *lights{&key_light};

    enum MATERIAL {
        MATERIAL_FLOOR,
        MATERIAL_DOG,
        MATERIAL_SHAPE,
        MATERIAL_PHONG,
        MATERIAL_BLINN,
        MATERIAL_MIRROR,
        MATERIAL_GLASS,
        MATERIAL_COUNT
    };

    Material floor_material{
        0.8f, 0.2f,
        MATERIAL_HAS_NORMAL_MAP |
              MATERIAL_HAS_ALBEDO_MAP,
        2, {0, 1}
    };
    Material dog_material{
        1.0f, 0.2f,
        MATERIAL_HAS_NORMAL_MAP |
              MATERIAL_HAS_ALBEDO_MAP,
        2, {2, 3}
    };
    Material shape_material{0.8f, 0.7f};
    Material Phong{1.0f,0.5f,0,0, {},
                   BRDF_Phong, 1.0f, 1.0, 0.0f, IOR_AIR,
                   {1.0f, 1.0f, 0.4f}};
    Material Blinn{{1.0f, 0.3f, 1.0f},0.5f,0,0, {},
                   BRDF_Blinn, 1.0f, 1.0, 0.0f, IOR_AIR,
                   {1.0f, 0.4f, 1.0f}};
    Material Mirror{
        0.0f,0.02f,
        MATERIAL_IS_REFLECTIVE,
        0, {},
        BRDF_CookTorrance, 1.0f, 1.0,  0.0f,
        IOR_AIR, F0_Aluminium
    };
    Material Glass {
        0.05f,0.25f,
        MATERIAL_IS_REFRACTIVE,
        0,{},
        BRDF_CookTorrance, 1.0f, 1.0f, 0.0f,
        IOR_GLASS, F0_Glass_Low
    };
    Material *materials{&floor_material};
    OrientationUsingQuaternion rot{0, -45 * DEG_TO_RAD, 0};
    Geometry floor {{{},{}, {40, 1, 40}},GeometryType_Quad};
    Geometry sphere1{{{},{4, 1, -4}},GeometryType_Sphere,MATERIAL_MIRROR};
    Geometry sphere2{{{},aux_light.position_or_direction},
                     GeometryType_Sphere,MATERIAL_PHONG, 0, GEOMETRY_IS_SHADOWING | GEOMETRY_IS_TRANSPARENT | GEOMETRY_IS_VISIBLE};
    Geometry monkey{{rot,{6, 4.5, 2}, 0.4f},    GeometryType_Mesh,MATERIAL_GLASS, 0, GEOMETRY_IS_VISIBLE};
    Geometry dragon{{{},{-2, 2, -3}},   GeometryType_Mesh,MATERIAL_PHONG, 1};
    Geometry dog   {{rot,{4, 2.1f, 3}, 0.8f},    GeometryType_Mesh,MATERIAL_DOG, 2};
    Geometry *geometries{&floor};

    Texture textures[4];
    char string_buffers[4][200]{};
    String texture_files[4]{
        String::getFilePath("floor_albedo.texture",string_buffers[0],__FILE__),
        String::getFilePath("floor_normal.texture",string_buffers[1],__FILE__),
        String::getFilePath("dog_albedo.texture"  ,string_buffers[2],__FILE__),
        String::getFilePath("dog_normal.texture"  ,string_buffers[3],__FILE__)
    };

    Mesh meshes[3];
    char strings[3][100]{};
    String mesh_files[3] = {
        String::getFilePath("monkey.mesh",strings[0],__FILE__),
        String::getFilePath("dragon.mesh",strings[1],__FILE__),
        String::getFilePath("dog.mesh"   ,strings[2],__FILE__)
    };

    Scene scene{{6,1,4,MATERIAL_COUNT,4,3},
                geometries, cameras, lights, materials, textures, texture_files, meshes, mesh_files};
    Selection selection;

    RayTracingRenderer renderer{scene};

    void OnUpdate(f32 delta_time) override {
        i32 fps = (i32)render_timer.average_frames_per_second;
        FPS.value = fps;
        FPS.value_color = fps >= 60 ? Green : (fps >= 24 ? Cyan : (fps < 12 ? Red : Yellow));
        Bounces.value = (i32)renderer.settings.max_depth;

        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (selection.changed) updateSelectionInHUD();
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        quat rot = quat::RotationAroundY(delta_time * 0.25f);
        for (u32 i = 1; i < scene.counts.geometries; i++) {
            Geometry &geo{geometries[i]};
            rot.amount = -rot.amount;
            if (!(controls::is_pressed::alt && selection.geometry == &geo))
                geo.transform.orientation *= rot;
        }
    }

    void updateSelectionInHUD() {
        const char* material = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            switch (geo.material_id) {
                case MATERIAL_FLOOR:  material = "Floor";  break;
                case MATERIAL_DOG:    material = "Dog";    break;
                case MATERIAL_SHAPE:  material = "Shape";  break;
                case MATERIAL_PHONG:  material = "Phong";  break;
                case MATERIAL_BLINN:  material = "Blinn";  break;
                case MATERIAL_MIRROR: material = "Mirror"; break;
                case MATERIAL_GLASS:  material = "Glass";  break;
                default: material = "";
            }
            Roughness.value = materials[geo.material_id].roughness;
        } else
            Roughness.value.string.copyFrom("", 0);

        Shader.value.string.copyFrom(material, 0);
        cutout = selection.geometry && (selection.geometry->flags & GEOMETRY_IS_TRANSPARENT);

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
            if (key == 'V') { antialias = !antialias; canvas.antialias = antialias ? SSAA : NoAA; }
            if (key == 'G') use_gpu = !use_gpu;
            if (key == 'B') draw_BVH = !draw_BVH;
            if (key == '1') renderer.settings.render_mode = RenderMode_Beauty;
            if (key == '2') renderer.settings.render_mode = RenderMode_Depth;
            if (key == '3') renderer.settings.render_mode = RenderMode_Normals;
            if (key == '4') renderer.settings.render_mode = RenderMode_NormalMap;
            if (key == '5') renderer.settings.render_mode = RenderMode_MipLevel;
            if (key == '6') renderer.settings.render_mode = RenderMode_UVs;
            if (selection.geometry &&
                selection.geometry != &floor &&
                (key == 'Z' || key == 'X' || key == 'C')) {
                Geometry &geo = *selection.geometry;
                if (key == 'C') {
                    if (geo.type != GeometryType_Mesh) {
                        if (geo.flags & GEOMETRY_IS_TRANSPARENT)
                            geo.flags &= ~GEOMETRY_IS_TRANSPARENT;
                        else
                            geo.flags |= GEOMETRY_IS_TRANSPARENT;

                        cutout = geo.flags & GEOMETRY_IS_TRANSPARENT;
                    }
                } else if (selection.geometry != &dog) {
                    char inc = key == 'X' ? 1 : -1;
                    if (controls::is_pressed::shift) {
                        renderer.settings.max_depth += inc;
                        if (renderer.settings.max_depth == 0) renderer.settings.max_depth = 10;
                        if (renderer.settings.max_depth == 11) renderer.settings.max_depth = 1;
                    } else if (controls::is_pressed::ctrl) {
                        Material &M = scene.materials[geo.material_id];
                        M.roughness = clampedValue(M.roughness + (f32)inc * 0.01f, 0.05f, 1.0f);
                    } else {
                        u32 material_count = MATERIAL_COUNT - 2;
                        geo.material_id = ((geo.material_id - 2 + inc + material_count) % material_count) + 2;
                        if (geo.material_id == MATERIAL_GLASS)
                            geo.flags &= ~GEOMETRY_IS_SHADOWING;
                        else
                            geo.flags |= GEOMETRY_IS_SHADOWING;
                    }
                    updateSelectionInHUD();
                    uploadMaterials(scene);
                }
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

            if (key == 'C' && selection.geometry && selection.geometry->type != GeometryType_Mesh) {
                if (selection.geometry->flags & GEOMETRY_IS_TRANSPARENT)
                    selection.geometry->flags &= ~GEOMETRY_IS_TRANSPARENT;
                else
                    selection.geometry->flags |= GEOMETRY_IS_TRANSPARENT;

                cutout = selection.geometry->flags & GEOMETRY_IS_TRANSPARENT;
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
    return (SlimApp*)new ExampleApp();
}