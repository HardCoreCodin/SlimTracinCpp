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
    HUDLine GPU {"GPU : ","Off","On",&use_gpu};
    HUDLine AA  {"AA  : ","Off","On",&antialias};
    HUDLine BVH {"BVH : ","Off","On",&draw_BVH};
    HUDLine Cut {"Cut : ","Off","On",&cutout};
    HUDLine Mode{"Mode: ","Beauty"};
    HUDLine Shader{   "Shader   : "};
    HUDLine Roughness{"Roughness: "};
    HUDLine Bounces{  "Bounces  : "};
    HUD hud{{9}, &FPS};

    // Viewport:
    Camera camera{{-25 * DEG_TO_RAD, 0, 0}, {0, 7, -11}}, *cameras{&camera};
    CameraRayProjection projection;
    Canvas canvas;
    Viewport viewport{canvas, &camera};

    // Scene:
    Light rim_light{ {1.0f, 0.25f, 0.25f}, {6, 5, 2}, 0.9f * 40.0f};
    Light glass_light1{ {0.25f, 1.0f, 0.25f}, {-3.3f, 0.6f, -3.0f}, 20.0f};
    Light glass_light2{ {0.25f, 0.25f, 1.0f}, {-0.6f, 1.75f, -3.15f}, 20.0f};
    Light *lights{&rim_light};

    enum MaterialID { Floor, DogMaterial, Rough, Phong, Blinn, Mirror, Glass, MaterialCount };

    u8 flags{MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material floor_material{0.8f, 0.2f, flags,
                            2, {Floor_Albedo, Floor_Normal}};
    Material dog_material{1.0f, 0.2f, flags,
        2, {Dog_Albedo, Dog_Normal}};
    Material rough_material{0.8f, 0.7f};
    Material phong_material{1.0f,0.5f,0,0, {},
                   BRDF_Phong, 1.0f, 1.0, 0.0f, IOR_AIR,
                   {1.0f, 1.0f, 0.4f}};
    Material blinn_material{{1.0f, 0.3f, 1.0f},0.5f,0,0, {},
                   BRDF_Blinn, 1.0f, 1.0, 0.0f, IOR_AIR,
                   {1.0f, 0.4f, 1.0f}};
    Material mirror_material{
        0.0f,0.02f,
        MATERIAL_IS_REFLECTIVE,
        0, {},
        BRDF_CookTorrance, 1.0f, 1.0,  0.0f,
        IOR_AIR, F0_Aluminium
    };
    Material glass_material {
        0.05f,0.25f,
        MATERIAL_IS_REFRACTIVE,
        0,{},
        BRDF_CookTorrance, 1.0f, 1.0f, 0.0f,
        IOR_GLASS, F0_Glass_Low
    };
    Material *materials{&floor_material};


    enum MesheID { Monkey, Dragon, Dog, MeshCount };

    Mesh meshes[MeshCount];
    char strings[MeshCount][100]{};
    String mesh_files[MeshCount] = {
        String::getFilePath("monkey.mesh",strings[Monkey],__FILE__),
        String::getFilePath("dragon.mesh",strings[Dragon],__FILE__),
        String::getFilePath("dog.mesh"   ,strings[Dog   ],__FILE__)
    };

    OrientationUsingQuaternion rot{0, -45 * DEG_TO_RAD, 0};
    Geometry floor {{{},{}, {40, 1, 40}},
                    GeometryType_Quad, Floor};
    Geometry sphere{{{},{4, 1, -4}},
                    GeometryType_Sphere, Mirror};
    Geometry monkey{{rot,{6, 4.5, 2}, 0.4f},
                    GeometryType_Mesh, Glass,      Monkey};
    Geometry dragon{{{},{-2, 2, -3}},
                    GeometryType_Mesh, Glass,      Dragon};
    Geometry dog   {{rot,{4, 2.1f, 3}, 0.8f},
                    GeometryType_Mesh, DogMaterial,   Dog};
    Geometry *geometries{&floor};

    Scene scene{{5,1,3,
                 MaterialCount,TextureCount, MeshCount},
                geometries, cameras, lights, materials, textures, texture_files, meshes, mesh_files};

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

        Bounces.value = (i32)renderer.settings.max_depth;

        quat rot = quat::RotationAroundY(delta_time * 0.25f);
        for (u32 i = 1; i < scene.counts.geometries; i++) {
            Geometry &geo{geometries[i]};
            if (geo.material_id == Glass)
                geo.flags &= ~GEOMETRY_IS_SHADOWING;
            else
                geo.flags |= GEOMETRY_IS_SHADOWING;

            if (&geo != &dragon) {
                rot.amount = -rot.amount;
                if (!(controls::is_pressed::alt && selection.geometry == &geo))
                    geo.transform.orientation *= rot;
            }
        }
    }

    void OnRender() override {
        renderer.render(viewport, true, use_gpu);
        if (draw_BVH) drawSceneBVH();
        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);
        canvas.drawToWindow();
    }

    void updateSelectionInHUD() {
        const char* shader = "";
        if (selection.geometry) {
            Geometry &geo = *selection.geometry;
            cutout = geo.flags & GEOMETRY_IS_TRANSPARENT;
            switch (geo.material_id) {
                case Floor      : shader = "Floor";  break;
                case DogMaterial: shader = "Dog";    break;
                case Rough      : shader = "Rough";  break;
                case Phong      : shader = "Phong";  break;
                case Blinn      : shader = "Blinn";  break;
                case Mirror     : shader = "Mirror"; break;
                case Glass      : shader = "Glass";  break;
                default: break;
            }
            Roughness.value = materials[geo.material_id].roughness;
        } else
            Roughness.value.string.copyFrom("", 0);

        Shader.value.string.copyFrom(shader, 0);

        selection.changed = false;
    }

    void OnKeyChanged(u8 key, bool is_pressed) override {
        if (!is_pressed) {
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
        } else {
            Geometry &geo = *selection.geometry;
            if (key == 'X' || key == 'Z') {
                char inc = key == 'X' ? 1 : -1;
                if (controls::is_pressed::alt) {
                    if (!(inc == -1 && min_bvh_depth == 1)) {
                        min_bvh_depth += inc;
                        max_bvh_depth += inc;
                    }
                } else if (controls::is_pressed::shift) {
                    renderer.settings.max_depth += inc;
                    if (renderer.settings.max_depth == 0) renderer.settings.max_depth = 10;
                    if (renderer.settings.max_depth == 11) renderer.settings.max_depth = 1;
                    Bounces.value = (i32)renderer.settings.max_depth;
                } else if (selection.geometry && &geo != &dog) {
                    if (controls::is_pressed::ctrl) {
                        Material &M = scene.materials[geo.material_id];
                        M.roughness = clampedValue(M.roughness + (f32) inc * 0.1f, 0.05f, 1.0f);
                        uploadMaterials(scene);
                    } else {
                        u32 material_count = MaterialCount - 2;
                        geo.material_id = ((geo.material_id - 2 + inc + material_count) % material_count) + 2;
                    }
                    updateSelectionInHUD();
                }
            } else if (key == 'C' && selection.geometry && geo.type != GeometryType_Mesh) {
                if (geo.flags & GEOMETRY_IS_TRANSPARENT)
                    geo.flags &= ~GEOMETRY_IS_TRANSPARENT;
                else
                    geo.flags |= GEOMETRY_IS_TRANSPARENT;

                cutout = geo.flags & GEOMETRY_IS_TRANSPARENT;
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

    u16 min_bvh_depth = 0;
    u16 max_bvh_depth = 3;

    void drawSceneBVH() {
        static Box box;
        static AABB aabb;
        for (u32 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo{scene.geometries[i]};
            if (geo.type == GeometryType_Mesh) {
                drawBVH(meshes[geo.id].bvh, geo.transform, viewport,min_bvh_depth, max_bvh_depth);
            } else {
                aabb.max = geo.type == GeometryType_Tet ? TET_MAX : 1.0f;
                aabb.min = -aabb.max.x;
                if (geo.type == GeometryType_Quad) {
                    aabb.min.y = -EPS;
                    aabb.max.y = EPS;
                }
                box.vertices = aabb;
                drawBox(box, geo.transform, viewport, BrightYellow, 0.5f);
            }
        }
        drawBVH(scene.bvh, {}, viewport, 0, scene.bvh.height);
    }

};

SlimApp* createApp() {
    return (SlimApp*)new ExampleApp();
}