#pragma once

#include "./scene.h"
#include "../core/ray.h"
#include "../core/transform.h"
#include "../viewport/viewport.h"

struct Selection {
    Transform xform;
    quat object_rotation;
    vec3 transformation_plane_origin,
         transformation_plane_normal,
         transformation_plane_center,
         object_scale,
         world_offset,
         *world_position{nullptr};
    Geometry *geometry{nullptr};
    Light *light{nullptr};
    f32 object_distance = 0;

    BoxSide box_side = BoxSide_None;
    bool changed = false;
    bool left_mouse_button_was_pressed = false;

    void manipulate(const Viewport &viewport, const Scene &scene) {
        static Ray ray, local_ray;
        static RayHit hit, local_ray_hit;

        const Dimensions &dimensions = viewport.dimensions;
        Camera &camera = *viewport.camera;
        i32 x = mouse::pos_x - viewport.bounds.left;
        i32 y = mouse::pos_y - viewport.bounds.top;
        f32 normalization_factor = 2.0f / dimensions.f_height;

        ray.origin = camera.position;
        ray.direction = camera.getRayDirectionAt(x, y, dimensions.width_over_height, normalization_factor);
        hit.distance = local_ray_hit.distance = INFINITY;
        i32 hit_geo_id = -1;
        i32 hit_light_id = -1;

        if (mouse::left_button.is_pressed && !left_mouse_button_was_pressed && !controls::is_pressed::alt) {
            // This is the first frame after the left mouse button went down:
            // Cast a ray onto the scene to find the closest object behind the hovered pixel:

            if (scene.castRay(ray, hit, hit_geo_id, hit_light_id)) {
                // Track the object that is now selected and Detect if object scene->selection has changed:
                if (hit_light_id >= 0) {
                    changed = light != (scene.lights + hit_light_id);
                    geometry = nullptr;
                    light = scene.lights + hit_light_id;
                    world_position = &light->position_or_direction;
                } else {
                    changed = geometry != (scene.geometries + hit_geo_id);
                    light = nullptr;
                    geometry = scene.geometries + hit_geo_id;
                    world_position = &geometry->transform.position;
                }

                // Capture a pointer to the selected object's position for later use in transformations:
                transformation_plane_origin = hit.position;
                world_offset = hit.position - *world_position;

                // Track how far away the hit position is from the camera along the depth axis:
                object_distance = (camera.orientation.transposed() * (hit.position - ray.origin)).z;
            } else {
                if (geometry || light) changed = true;
                geometry = nullptr;
                light = nullptr;
            }
        }
        left_mouse_button_was_pressed = mouse::left_button.is_pressed;
        if (geometry || light) {
            if (controls::is_pressed::alt) {
                bool any_mouse_button_is_pressed = (
                        mouse::left_button.is_pressed ||
                        mouse::middle_button.is_pressed ||
                        mouse::right_button.is_pressed);
                if (!any_mouse_button_is_pressed) {
                    // Cast a ray onto the bounding box of the currently selected object:
                    if (geometry) {
                        xform = geometry->transform;
                        if (geometry->type == GeometryType_Mesh)
                            xform.scale *= scene.meshes[geometry->id].aabb.max;
                    } else {
                        xform.position = light->position_or_direction;
                        xform.scale = light->intensity / 64;
                        xform.orientation.reset();
                    }

                    local_ray.localize(ray, xform);
                    box_side = local_ray.hitsDefaultBox(local_ray_hit);
                    if (box_side) {
                        transformation_plane_center = xform.externPos(local_ray_hit.normal);
                        transformation_plane_origin = xform.externPos(local_ray_hit.position);
                        transformation_plane_normal = xform.externDir(local_ray_hit.normal);
                        transformation_plane_normal = transformation_plane_normal.normalized();
                        world_offset = transformation_plane_origin - *world_position;
                        object_scale    = xform.scale;
                        object_rotation = xform.orientation;
                    }
                }

                if (any_mouse_button_is_pressed && box_side) {
                    ray.reset(ray.origin, ray.direction);
                    if (ray.hitsPlane(transformation_plane_origin, transformation_plane_normal, hit)) {
                        if (geometry) {
                            xform = geometry->transform;
                            if (geometry->type == GeometryType_Mesh)
                                xform.scale *= scene.meshes[geometry->id].aabb.max;
                        } else {
                            xform.position = light->position_or_direction;
                            xform.scale = light->intensity / 64;
                            xform.orientation.reset();
                        }

                        if (mouse::left_button.is_pressed) {
                            *world_position = hit.position - world_offset;
                        } else if (mouse::middle_button.is_pressed) {
                            hit.position = xform.internPos(hit.position) / xform.internPos(transformation_plane_origin);

                            if (geometry)
                                geometry->transform.scale = object_scale * hit.position;
                            else {
                                if (box_side == BoxSide_Top || box_side == BoxSide_Bottom)
                                    hit.position.x = hit.position.z > 0 ? hit.position.z : -hit.position.z;
                                else
                                    hit.position.x = hit.position.y > 0 ? hit.position.y : -hit.position.y;
                                light->intensity = (object_scale.x * 64) * hit.position.x;
                            }
                        } else if (mouse::right_button.is_pressed && geometry) {
                            vec3 v1{ hit.position - transformation_plane_center };
                            vec3 v2{ transformation_plane_origin - transformation_plane_center };
                            quat rotation = quat{v2.cross(v1), (v1.dot(v2)) + sqrtf(v1.squaredLength() * v2.squaredLength())};
                            geometry->transform.orientation = (rotation.normalized() * object_rotation).normalized();
                        }
                    }
                }
            } else {
                box_side = BoxSide_None;
                if (mouse::left_button.is_pressed && mouse::moved) {
                    // BoxSide_Back-project the new mouse position onto a quad at a distance of the selected-object away from the camera

                    // Screen -> NDC:
                    f32 X = ((f32)x + 0.5f) / dimensions.h_width  - 1;
                    f32 Y = ((f32)y + 0.5f) / dimensions.h_height - 1;

                    // NDC -> View:
                    X *= object_distance / (camera.focal_length * dimensions.height_over_width);
                    Y *= object_distance / camera.focal_length;

                    // View -> World (BoxSide_Back-track by the world offset from the hit position back to the selected-object's center):
                    *world_position = camera.orientation * vec3{X, -Y, object_distance} + camera.position - world_offset;
                }
            }
        }
    }
};