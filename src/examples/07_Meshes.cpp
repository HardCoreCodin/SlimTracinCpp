#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/ssb.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct MeshesApp : SlimApp {
    // Viewport:
    Camera camera{
        .orientation = {-25 * DEG_TO_RAD, 0, 0},
        .position = {0, 7, -11}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};

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

    Material shapes_material{
        .albedo = {0.8f, 1.0f, 0.8f},
        .roughness = 0.7f
    };
    Material dog_material{
        .roughness = 0.2f,
        .normal_magnitude = 4.0f,
        .flags = MATERIAL_HAS_NORMAL_MAP |
                 MATERIAL_HAS_ALBEDO_MAP,
        .texture_count = 2,
        .texture_ids = {2, 3}
    };
    Material floor_material{
        .albedo = {0.8f, 0.8f, 0.8f},
        .roughness = 0.2f,
        .flags = MATERIAL_HAS_NORMAL_MAP |
                 MATERIAL_HAS_ALBEDO_MAP,
        .texture_count = 2,
        .texture_ids = {0, 1}
    };
    Material *materials{&shapes_material};

    char string_buffers[4][200];
    String texture_files[4]{
        String::getFilePath((char*)"floor_albedo.texture",string_buffers[0],(char*)__FILE__),
        String::getFilePath((char*)"floor_normal.texture",string_buffers[1],(char*)__FILE__),
        String::getFilePath((char*)"dog_albedo.texture",string_buffers[2],(char*)__FILE__),
        String::getFilePath((char*)"dog_normal.texture",string_buffers[3],(char*)__FILE__)
    };

    Texture floor_albedo_map;
    Texture floor_normal_map;
    Texture dog_albedo_map;
    Texture dog_normal_map;
    Texture *textures = &floor_albedo_map;

    Geometry dog{
        .transform = {
            .position = {0, 2, -1}
        },
        .type = GeometryType_Mesh,
        .material_id = 1
    };
    Geometry dragon{
        .transform = {
            .position = {4, 2, 8}
        },
        .type = GeometryType_Mesh,
        .id = 1
    };
    Geometry monkey{
        .transform = {
            .position = {-4, 1.2f, 8}
        },
        .type = GeometryType_Mesh,
        .id = 2
    };

    Geometry floor{
        .transform = {
            .scale = {40, 0.1f, 40}
        },
        .type = GeometryType_Quad,
        .material_id = 2
    };
    Geometry *geometries{&dog};

    char strings[3][100] = {};
    String mesh_files[3] = {
        String::getFilePath((char*)"dog.mesh" ,strings[0],(char*)__FILE__),
        String::getFilePath((char*)"dragon.mesh" ,strings[1],(char*)__FILE__),
        String::getFilePath((char*)"monkey.mesh",strings[2],(char*)__FILE__)
    };
    Mesh dog_mesh;
    Mesh dragon_mesh;
    Mesh monkey_mesh;
    Mesh *meshes{&dog_mesh};

    SceneCounts counts{
        .geometries = 4,
        .cameras = 1,
        .lights = 3,
        .materials = 3,
        .textures = 4,
        .meshes = 3
    };
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files,
                meshes, mesh_files};
    Selection selection;

    RayTracer ray_tracer{scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;

    bool draw_TLAS = false;
    bool antialias = false;
    bool transparent = false;

    // HUD:
    HUDLine FPS_hud_line{(char*)"FPS : "};
    HUDLine GPU_hud_line{(char*)"GPU : ",
                         (char*)"On",
                         (char*)"Off",
                         &ray_tracer.use_gpu,
                         true};
    HUDLine AA_hud_line{(char*)"SSAA: ",
                        (char*)"On",
                        (char*)"Off",
                        &antialias,
                        true};
    HUDLine Mode_hud_line{(char*)"Mode : ", (char*)"Beauty"};
    HUDLine TLAS_hud_line{(char*)"TLAS : ", (char*)"BVH"};
    HUDLine Draw_TLAS_hud_line{(char*)"Draw TLAS : ",
                               (char*)"On",
                               (char*)"Off",
                               &draw_TLAS,
                               true};
    HUDLine Transparent_hud_line{(char*)"Transparent : ",
                                 (char*)"On",
                                 (char*)"Off",
                                 &transparent,
                                 true};
    HUDSettings hud_settings{7};
    HUD hud{hud_settings, &FPS_hud_line};

    void OnRender() override {
        static Transform transform;
        FPS_hud_line.value = (i32)render_timer.average_frames_per_second;
        canvas.clear();

        ray_tracer.render(viewport);
        if (draw_TLAS) {
            if (ray_tracer.use_ssb)
                drawSSB(scene, canvas);
            else {
                for (u32 i = 0; i < scene.counts.geometries; i++)
                    if (geometries[i].type == GeometryType_Mesh)
                        drawBVH(scene.meshes[geometries[i].id].bvh, geometries[i].transform, viewport);
                drawBVH(scene.bvh, transform, viewport);
            }
        }

        if (controls::is_pressed::alt) drawSelection(selection, viewport, scene);
        if (hud.enabled) drawHUD(hud, canvas);

        canvas.drawToWindow();
    }

    void OnUpdate(f32 delta_time) override {
        static float elapsed = 0;
        elapsed += delta_time;

        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        f32 dog_scale    = sinf(elapsed * 1.5f) * 0.02f;
        f32 dragon_scale = sinf(elapsed * 2.5f) * 0.04f;
        f32 monkey_scale = sinf(elapsed * 2.5f + 1) * 0.05f;

        dog.transform.scale = 0.8f + dog_scale;
        dog.transform.scale.y += 2 * dog_scale;
        dragon.transform.scale = 0.4f - dragon_scale;
        dragon.transform.scale.y += 2 * dragon_scale;
        monkey.transform.scale = 0.3f + monkey_scale;
        monkey.transform.scale.y -= 2 * monkey_scale;

        quat rot = quat::RotationAroundY(delta_time * 0.15f);
        if (!(controls::is_pressed::alt && selection.geometry == &dragon))
            dragon.transform.orientation *= rot;

        rot.amount *= -0.5f;
        rot = rot.normalized();
        if (!(controls::is_pressed::alt && selection.geometry == &monkey))
            monkey.transform.orientation *= rot;

        rot.amount = -rot.amount;
        if (!(controls::is_pressed::alt && selection.geometry == &dog))
            dog.transform.orientation *= rot;
    }

    void OnKeyChanged(u8 key, bool is_pressed) override {
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
        if (!is_pressed) {
            if (key == controls::key_map::tab) hud.enabled = !hud.enabled;
            if (key == 'B') draw_TLAS = !draw_TLAS;
            if (key == 'G') ray_tracer.use_gpu = !ray_tracer.use_gpu;
            if (key == 'X') { ray_tracer.use_ssb = !ray_tracer.use_ssb; TLAS_hud_line.value.string = ray_tracer.use_ssb ? (char*)"SSB" : (char*)"BVH";}
            if (key == 'Z') { antialias = !antialias; canvas.antialias = antialias ? SSAA : NoAA; }
            if (key == '1') { ray_tracer.render_mode = RenderMode_Beauty; Mode_hud_line.value.string = (char*)"Beauty"; }
            if (key == '2') { ray_tracer.render_mode = RenderMode_Depth; Mode_hud_line.value.string = (char*)"Depth"; }
            if (key == '3') { ray_tracer.render_mode = RenderMode_Normals; Mode_hud_line.value.string = (char*)"Normals"; }
            if (key == '4') { ray_tracer.render_mode = RenderMode_NormalMap; Mode_hud_line.value.string = (char*)"Normal Maps"; }
            if (key == '5') { ray_tracer.render_mode = RenderMode_MipLevel; Mode_hud_line.value.string = (char*)"Mip Level"; }
            if (key == '6') { ray_tracer.render_mode = RenderMode_UVs; Mode_hud_line.value.string = (char*)"UVs"; }
            if (key == 'T' && selection.geometry) {
                u8 &flags = selection.geometry->flags;
                if (flags & GEOMETRY_IS_TRANSPARENT)
                    flags &= ~GEOMETRY_IS_TRANSPARENT;
                else
                    flags |= GEOMETRY_IS_TRANSPARENT;

                transparent = flags & GEOMETRY_IS_TRANSPARENT;
//                uploadMaterials(scene);
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
    return (SlimApp*)new MeshesApp();
}