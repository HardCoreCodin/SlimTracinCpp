#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/scene.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct GeometryApp : SlimApp {
    // Viewport:
    Camera camera{
            {-4, 15, -17},
            {-25 * DEG_TO_RAD, 0, 0}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};

    // Scene:
    Light key_light{ {20, 20, -5}, {1.0f, 1.0f, 0.65f}, 1.1f * 150.0f};
    Light fill_light{ {-20, 20, -5}, {0.65f, 0.65f, 1.0f}, 1.2f * 150.0f};
    Light rim_light{ {2, 5, 10}, {1.0f, 0.25f, 0.25f}, 0.9f * 150.0f};
    Light *lights{&key_light};

    Material shapes_material{BRDF_CookTorrance, 0.8f, 0.0f, 0, 0, 0.7f, 0.1f};
    Material floor_material{BRDF_CookTorrance, 0.5f, 0.0f, MATERIAL_HAS_NORMAL_MAP | MATERIAL_HAS_ALBEDO_MAP};
    Material *materials{&shapes_material};

    char string_buffers[2][200];
    String texture_files[2]{
            String::getFilePath((char*)"floor_albedo.texture",string_buffers[0],(char*)__FILE__),
            String::getFilePath((char*)"floor_normal.texture",string_buffers[1],(char*)__FILE__)
    };

    Texture floor_albedo_map;
    Texture floor_normal_map;
    Texture *textures = &floor_albedo_map;

    Geometry box{{{-11, 8, 5}, {0.02f, 0.04f, 0.0f}, {3,4,5}}, GeometryType_Box};
    Geometry tet{{{-3, 6, 14}, {0.02f, 0.04f, 0.06f}, {4,3, 5}}, GeometryType_Tet};
    Geometry sphere{{{3, 6, 2}, {}, {5,4,3}}, GeometryType_Sphere};
    Geometry floor{{{}, {}, {40, 1, 40}}, GeometryType_Quad};
    Geometry *geometries{&box};

    SceneCounts counts{4, 1, 3, 2, 2};
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;
    quat rotation{tet.transform.rotation};

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

    GeometryApp() {
        floor.material_id = (u32)(&floor_material - materials);
        floor_material.texture_count = 2;
        floor_material.texture_ids[0] = 0;
        floor_material.texture_ids[1] = 1;
    }

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
        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        quat rot = quat::AxisAngle(rotation.axis, delta_time * 10.0f);
        for (u8 i = 0; i < scene.counts.geometries; i++) {
            Geometry &geo = geometries[i];

            if (geo.type != GeometryType_Quad && !(controls::is_pressed::alt && &geo == selection.geometry))
                geo.transform.rotation = (geo.transform.rotation * rot).normalized();
        }

//        uploadPrimitives(scene);
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
            if (key == '1') { ray_tracer.mode = RenderMode_Beauty; Mode_hud_line.value.string = (char*)"Beauty"; }
            if (key == '2') { ray_tracer.mode = RenderMode_Depth; Mode_hud_line.value.string = (char*)"Depth"; }
            if (key == '3') { ray_tracer.mode = RenderMode_Normals; Mode_hud_line.value.string = (char*)"Normals"; }
            if (key == '4') { ray_tracer.mode = RenderMode_NormalMap; Mode_hud_line.value.string = (char*)"Normal Maps"; }
            if (key == '5') { ray_tracer.mode = RenderMode_MipLevel; Mode_hud_line.value.string = (char*)"Mip Level"; }
            if (key == '6') { ray_tracer.mode = RenderMode_UVs; Mode_hud_line.value.string = (char*)"UVs"; }
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
    return (SlimApp*)new GeometryApp();
}