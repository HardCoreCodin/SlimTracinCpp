#include "../slim/scene/selection.h"
#include "../slim/draw/hud.h"
#include "../slim/draw/bvh.h"
#include "../slim/draw/selection.h"
#include "../slim/renderer/raytracer.h"
#include "../slim/app.h"


// Or using the single-header file:
//#include "../slim.h"

struct GeometryApp : SlimApp {
    // Viewport:
    Camera camera{
            {0, 7, -11},
            {-25 * DEG_TO_RAD, 0, 0}
    }, *cameras{&camera};
    Canvas canvas;
    Viewport viewport{canvas,&camera};
    bool is_transparent = false;
    bool draw_bvh = false;

    // HUD:
    HUDLine transparent_line{(char*)"Transparent : ",
               (char*)"On",
               (char*)"Off",
               &is_transparent,
               true};
    HUDLine draw_bvh_line{(char*)"Draw BVH : ",
                             (char*)"On",
                             (char*)"Off",
                             &draw_bvh,
                             true};
    HUDSettings hud_settings{2};
    HUD hud{hud_settings, &transparent_line};

    // Scene:
    Light key_light{ {10, 10, -5}, {1.0f, 1.0f, 0.65f}, 1.1f * 40.0f};
    Light fill_light{ {-10, 10, -5}, {0.65f, 0.65f, 1.0f}, 1.2f * 40.0f};
    Light rim_light{ {2, 5, 12}, {1.0f, 0.25f, 0.25f}, 0.9f * 40.0f};
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

    Geometry floor{{{}, {}, {40, 1, 40}}, GeometryType_Quad};
    Geometry box{{{-9, 4, 3}, {0.02f, 0.04f, 0.0f}, {3,4,5}}, GeometryType_Box};
    Geometry tet{{{-3, 4, 12}, {0.02f, 0.04f, 0.06f}, {3,4,5}}, GeometryType_Tetrahedron};
    Geometry sphere{{{3, 4, 0}, {}, {3,4,5}}, GeometryType_Sphere};
    Geometry *geometries{&floor};

    SceneCounts counts{4, 1, 3, 2, 2};
    Scene scene{counts, nullptr, geometries, cameras, lights, materials, textures, texture_files};
    Selection selection;

    RayTracer ray_tracer{canvas, scene, (u8)counts.geometries, scene.mesh_stack_size};

    // Drawing:
    f32 opacity = 0.2f;
    quat rotation{tet.transform.rotation};

    GeometryApp() {
        floor.material_id = &floor_material - materials;
        floor_material.texture_count = 2;
        floor_material.texture_ids[0] = 0;
        floor_material.texture_ids[1] = 1;
    }

    void OnRender() override {
        static Transform transform;

        canvas.clear();

        ray_tracer.render(camera);

        if (draw_bvh)
            drawBVH(scene.bvh, transform, viewport);

        if (controls::is_pressed::alt)
            drawSelection(selection, viewport, scene);

        if (hud.enabled)
            drawHUD(hud, canvas);

        canvas.drawToWindow();
    }

    void OnUpdate(f32 delta_time) override {
        if (!mouse::is_captured) selection.manipulate(viewport, scene);
        if (!controls::is_pressed::alt) viewport.updateNavigation(delta_time);

        quat rot = quat{rotation.axis, rotation.amount};
        for (u8 i = &box - geometries; i < 4; i++) {
            Geometry &geo = geometries[i];

            if (!(controls::is_pressed::alt && &geo == selection.geometry))
                geo.transform.rotation = (geo.transform.rotation * rot).normalized();
        }
        scene.updateAABBs();
        scene.updateBVH();

//        uploadPrimitives(scene);
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
            if (key == 'B')
                draw_bvh = !draw_bvh;
            else if (key == 'T' && selection.geometry) {
                u8 &flags = selection.geometry->flags;
                if (flags & GEOMETRY_IS_TRANSPARENT)
                    flags &= ~GEOMETRY_IS_TRANSPARENT;
                else
                    flags |= GEOMETRY_IS_TRANSPARENT;

                is_transparent = flags & GEOMETRY_IS_TRANSPARENT;
//                uploadMaterials(scene);
            }
        } else if (key == controls::key_map::tab)
            hud.enabled = !hud.enabled;
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