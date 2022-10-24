#pragma once

#include "../scene/scene.h"
#include "../viewport/viewport.h"

INLINE_XPU bool computeSSB(RectI &bounds, vec3 &pos, f32 r, f32 focal_length, Dimensions &dimensions) {
/*
 h = y - t
 HH = zz + tt

 r/z = h/H
 rH = zh
 rrHH = zzhh
 rr(zz + tt) = zz(y -t)(y - t)
 rrzz + rrtt = zz(yy -2ty + tt)
 rrzz + rrtt = zzyy -2tyzz + ttzz
 rrtt - zztt + (2yzz)t + rrzz - zzyy = 0
 (rr - zz)tt + (2yzz)t + zz(rr - yy) = 0

 a = rr - zz
 b = 2yzz
 c = zz(rr - yy)

 t = -b/2a +/- sqrt(bb - 4ac)/2a
 t = -2yzz/2(rr - zz) +/- sqrt(2yzz2yzz - 4(rr - zz)zz(rr - yy))/2(rr - zz)
 t = -yzz/(rr - zz) +/- sqrt(4yzzyzz - 4(rr - zz)zz(rr - yy))/2(rr - zz)
 t = -yzz/(rr - zz) +/- sqrt(yyzzzz - zz(rr - zz)(rr - yy))/(rr - zz)
 t = -yzz/(rr - zz) +/- sqrt(zz(yyzz - (rr - zz)(rr - yy)))/(rr - zz)
 t = -yzz/(rr - zz) +/- z*sqrt(yyzz - (rr - zz)(rr - yy))/(rr - zz)
 t = -yzz/(rr - zz) +/- z*sqrt(yyzz - (rr - zz)(rr - yy))/(rr - zz)

 t/z = 1/(rr - zz) * (-yz +/- sqrt(yyzz - (rr - zz)(rr - yy)))
 t/z = 1/(rr - zz) * (-yz +/- sqrt(yyzz - rr*rr + zz*rr + rr*yy - zz*yy))
 t/z = 1/(rr - zz) * (-yz +/- sqrt(yyzz - zz*yy - rr*rr + zz*rr + rr*yy))
 t/z = 1/(rr - zz) * (-yz +/- sqrt(0 - rr*rr + zz*rr + rr*yy))
 t/z = 1/(rr - zz) * (-yz +/- sqrt(rr(yy - rr + zz))
 t/z = -1/(rr - zz) * -(-yz +/- r*sqrt(zz + yy - rr)
 t/z = 1/(zz - rr) * (yz -/+ r*sqrt(yy + zz - rr)

  t/z = 1/(zz - rr) * (yz -/+ r*sqrt(yy + zz - rr)
  den = zz - rr
  sqr = r * sqrt(yy + den)

  t/z = 1/den * (yz -/+ sqr)
  s/fl = t/z
  s = fl*t/z
  s = fl/den * (yz -/+ sqr)

  f = fl/den
  s = f(yx -/+ sqr)

  s1 = f(yz - sqr)
  s2 = f(yz + sqr)
*/
    bounds.left = dimensions.width + 1;
    bounds.right = dimensions.width + 1;
    bounds.top = dimensions.height + 1;
    bounds.bottom = dimensions.height + 1;

    if (pos.z <= -r)
        return false;

    f32 x = pos.x;
    f32 y = pos.y;
    f32 z = pos.z;
    f32 left, right, top, bottom;
    f32 factor;

    if (z <= r) { // Camera is within the bounds of the shape - fallback to doing weak-projection:
        // The focal length ('fl') is defined with respect to a 'conceptual' projection-plane of height 2 (spanning in [-1, +1])
        // which is situated at distance 'fl' away from the camera, along its view-space's positive Z axis.
        // So half of its height is 1 while half of its width is the aspect ratio (width / height).

        // Compute the dimensions of a proportionally-scaled image plane situated at distance 'z' instead of 'fl'
        // h : Half of the height of a projection-plane placed at 'pos' and facing the camera:
        // fl / 1 = abs(z) / h
        // fl * h = abs(z)
        // h = abs(z) / fl
        f32 h = fabsf(z) / focal_length;
        top    = y + r;
        bottom = y - r;

        if (h < bottom || top < -h) // The geometry is out of view vertically (either above or below)
            return false;

        // w : Half of the width of a projection-plane placed at 'pos' and facing the camera:
        // w / h = width / height
        // w = h * width / height
        f32 w = h * dimensions.width_over_height;
        right  = x + r;
        left   = x - r;
        if (w < left || right < -w) // The geometry is out of view horizontally (either on the left or the right)
            return false;

        bounds.left = -w < left   ? (i32)(dimensions.f_width  * (left   + w) / (2 * w)) : 0;
        bounds.right = right < w   ? (i32)(dimensions.f_width  * (right  + w) / (2 * w)) : dimensions.width;
        bounds.top = top < h     ? (i32)(dimensions.f_height * (h    - top) / (2 * h)) : 0;
        bounds.bottom = -h < bottom ? (i32)(dimensions.f_height * (h - bottom) / (2 * h)) : dimensions.height;

        return true;
    }

    f32 den = z*z - r*r;
    factor = focal_length / den;

    f32 yz = y * z;
    f32 sqr = y*y + den;
    sqr = r * sqrtf(sqr);

    bottom = factor*(yz - sqr);
    top = factor*(yz + sqr);
    if (bottom < 1 && top > -1) {
        factor *= dimensions.height_over_width;
        f32 xz = x * z;
        sqr = r * sqrtf(x*x + den);
        left = factor*(xz - sqr);
        right    = factor*(xz + sqr);
        if (left < 1 && right > -1) {
            bottom = bottom > -1 ? bottom : -1; bottom += 1;
            top    = top < 1 ? top : 1; top    += 1;
            left   = left > -1 ? left : -1; left   += 1;
            right  = right < 1 ? right : 1; right  += 1;

            top    = 2 - top;
            bottom = 2 - bottom;

            bounds.left = (i32)(dimensions.h_width * left);
            bounds.right = (i32)(dimensions.h_width * right);
            bounds.bottom = (i32)(dimensions.h_height * bottom);
            bounds.top = (i32)(dimensions.h_height * top);
            return true;
        }
    }

    return false;
}

INLINE void updateGeometrySSB(Scene *scene, Viewport *viewport, Geometry *geometry) {
    f32 radius, min_r, max_r;
    AABB *aabb;

    vec3 view_space_position = geometry->transform.position - viewport->camera->position;
    view_space_position = viewport->camera->rotation.inverted() * view_space_position;
    vec3 &scale{geometry->transform.scale};
    switch (geometry->type) {
        case GeometryType_Quad       : radius = scale.length();         break;
        case GeometryType_Box        : radius = scale.length();         break;
        case GeometryType_Tetrahedron: radius = scale.maximum() * SQRT2; break;
        case GeometryType_Sphere     : radius = scale.maximum();         break;
        case GeometryType_Mesh       :
            aabb   = &scene->meshes[geometry->id].aabb;
            min_r  = (scale * aabb->min).length();
            max_r  = (scale * aabb->max).length();
            radius = max_r > min_r ? max_r : min_r;
            break;
        default:
            return;
    }

    geometry->flags &= ~GEOMETRY_IS_VISIBLE;
    if (computeSSB(geometry->screen_bounds,
                   view_space_position, radius,
                   viewport->camera->focal_length,
                   viewport->canvas.dimensions))
        geometry->flags |= GEOMETRY_IS_VISIBLE;
}

void updateSceneSSB(Scene *scene, Viewport *viewport) {
    for (u32 i = 0; i < scene->counts.geometries; i++)
        updateGeometrySSB(scene, viewport, scene->geometries + i);

//    uploadGeometries(scene);
}