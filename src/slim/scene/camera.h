#pragma once

#include "../math/mat3.h"


struct Camera {
    OrientationUsing3x3Matrix orientation{};
    vec3 position{0};
    vec3 current_velocity{0};
    f32 focal_length{  CAMERA_DEFAULT__FOCAL_LENGTH};
    f32 zoom_amount{   CAMERA_DEFAULT__FOCAL_LENGTH};
    f32 target_distance{CAMERA_DEFAULT__TARGET_DISTANCE};
    f32 dolly_amount{0};

    void zoom(f32 amount) {
        f32 new_amount = zoom_amount + amount;
        focal_length = new_amount > 1 ? new_amount : (new_amount < -1.0f ? (-1.0f / new_amount) : 1.0f);
        zoom_amount = new_amount;
    }

    void dolly(f32 amount) {
        vec3 target_position = orientation.forward.scaleAdd(target_distance, position);

        // Compute new target distance:
        dolly_amount += amount;
        target_distance = powf(2.0f, dolly_amount / -200.0f) * CAMERA_DEFAULT__TARGET_DISTANCE;

        // Back-track from target position_x to new current position_x:
        position = target_position - (orientation.forward * target_distance);
    }

    void orbit(f32 azimuth, f32 altitude) {
        // Move the camera forward to the position_x of its target:
        position += orientation.forward * target_distance;

        // Reorient the camera while it is at the position_x of its target:
        orientation.rotate(altitude, azimuth);

        // Back the camera away from its target position_x using the updated forward direction:
        position -= orientation.forward * target_distance;
    }

    void pan(f32 right_amount, f32 up_amount) {
        position += orientation.up * up_amount + orientation.right * right_amount;
    }

    INLINE_XPU vec3 getTopLeftCornerOfProjectionPlane(f32 aspect_ratio) const {
        return orientation.forward.scaleAdd(focal_length, orientation.right.scaleAdd(-aspect_ratio, orientation.up));
    }

    INLINE_XPU vec3 getRayDirectionAt(i32 x, i32 y, f32 aspect_ratio, f32 normalization_factor, bool use_pixel_centers = true) const {
        f32 X = (f32)x * normalization_factor;
        f32 Y = (f32)y * normalization_factor;
        vec3 start = getTopLeftCornerOfProjectionPlane(aspect_ratio);

        if (use_pixel_centers) {
            normalization_factor *= 0.5f;
            start += orientation.right * normalization_factor;
            start -= orientation.up * normalization_factor;
        }

        return orientation.right.scaleAdd(X, orientation.up.scaleAdd(-Y, start)).normalized();
    }

    INLINE_XPU vec3 internPos(const vec3 &pos) const { return _unrotate(_untranslate(pos)); }
    INLINE_XPU vec3 internDir(const vec3 &dir) const { return _unrotate(dir); }
    INLINE_XPU vec3 externPos(const vec3 &pos) const { return _translate(_rotate(pos)); }
    INLINE_XPU vec3 externDir(const vec3 &dir) const { return _rotate(dir); }

private:
    INLINE_XPU vec3 _rotate(const vec3 &pos) const { return orientation * pos; }
    INLINE_XPU vec3 _unrotate(const vec3 &pos) const { return orientation.transposed() * pos; }
    INLINE_XPU vec3 _translate(const vec3 &pos) const { return pos + position; }
    INLINE_XPU vec3 _untranslate(const vec3 &pos) const { return pos - position; }
};