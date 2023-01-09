#pragma once

#include "../scene/scene.h"
#include "../viewport/viewport.h"

bool computeSSB(RectI &bounds, const vec3 &pos, f32 r, f32 focal_length, const Dimensions &dimensions) {
/*
 H = y - t
 HH = zz + tt

 R/z = H/H
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
 t/z = -1/(rr - zz) * -(-yz +/- R*sqrt(zz + yy - rr)
 t/z = 1/(zz - rr) * (yz -/+ R*sqrt(yy + zz - rr)

  t/z = 1/(zz - rr) * (yz -/+ R*sqrt(yy + zz - rr)
  den = zz - rr
  sqr = R * sqrt(yy + den)

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
        // H : Half of the height of a projection-plane placed at 'pos' and facing the camera:
        // fl / 1 = abs(z) / H
        // fl * H = abs(z)
        // H = abs(z) / fl
        f32 h = fabsf(z) / focal_length;
        top    = y + r;
        bottom = y - r;

        if (h < bottom || top < -h) // The geometry is out of view vertically (either above or below)
            return false;

        // w : Half of the width of a projection-plane placed at 'pos' and facing the camera:
        // w / H = width / height
        // w = H * width / height
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

void updateScreenBoundsRangeFromCoords(i32 x, i32 y, i32 X, i32 Y, RectI &bounds) {
    bounds.left = Max(Min(bounds.left, x), 0);
    bounds.top  = Max(Min(bounds.top,  y), 0);
    bounds.right  = Min(Max(bounds.right,  x), X);
    bounds.bottom = Min(Max(bounds.bottom, y), Y);
}

bool updateScreenBoundsRangeFromEdge(Edge edge, i32 X, i32 Y, const Geometry &geo, const Viewport &viewport, RectI &bounds) {
    edge.from = viewport.camera->internPos(geo.transform.externPos(edge.from));
    edge.to   = viewport.camera->internPos(geo.transform.externPos(edge.to));
    Sides from_sides, to_sides;
    viewport.checkEdge(edge, from_sides, to_sides);
    if (viewport.cullAndClipEdge(edge)) {
        viewport.projectEdge(edge);
        updateScreenBoundsRangeFromCoords((i32)edge.from.x, (i32)edge.from.y, X, Y, bounds);
        updateScreenBoundsRangeFromCoords((i32)edge.to.x, (i32)edge.to.y, X, Y, bounds);
        return true;
    } else {
        if (from_sides.right || to_sides.right) bounds.right = X;
        if (from_sides.left || to_sides.left) bounds.left = 0;
        if (from_sides.top || to_sides.top) bounds.top = 0;
        if (from_sides.bottom || to_sides.bottom) bounds.bottom = Y;

        return !((from_sides.back && to_sides.back));
    }
}

void updateScreenBounds(const Scene &scene, const Viewport &viewport) {
    static QuadVertices quad_vertices;
    static TetVertices tet_vertices;
    static BoxVertices box_vertices;
    static QuadEdges quad_edges;
    static TetEdges tet_edges;
    static BoxEdges box_edges, mesh_box_edges;

    Camera &camera = *viewport.camera;
    const Dimensions &dim = viewport.dimensions;
    Edge edge, *edges;
    u8 edge_count;
    bool visible;
    i32 X = (i32)(dim.width  - 1);
    i32 Y = (i32)(dim.height - 1);

    for (u32 g = 0; g < scene.counts.geometries; g++) {
        RectI &bounds = scene.screen_bounds[g];
        Geometry &geo = scene.geometries[g];
        Transform &xform = geo.transform;

        bounds.right = bounds.bottom = 0;
        bounds.left = X;
        bounds.top  = Y;
        visible = edge_count = 0;
        switch (geo.type) {
            case GeometryType_Quad  : edge_count = QUAD__EDGE_COUNT; edges = quad_edges.array; break;
            case GeometryType_Tet   : edge_count = TET__EDGE_COUNT; edges = tet_edges.array; break;
            case GeometryType_Box   : edge_count = BOX__EDGE_COUNT;  edges = box_edges.array; break;
            case GeometryType_Mesh  : edge_count = BOX__EDGE_COUNT;  edges = mesh_box_edges.array; mesh_box_edges = BoxEdges{scene.meshes[geo.id].aabb}; break;
            case GeometryType_Sphere: visible = computeSSB(bounds, camera.internPos(xform.position), xform.scale.maximum(), camera.focal_length, dim); break;
            default: continue;
        }
        for (u8 i = 0; i < edge_count; i++) visible |= updateScreenBoundsRangeFromEdge(edges[i], X, Y, geo, viewport, bounds);

        visible &= !(bounds.right <= bounds.left || bounds.bottom <= bounds.top);
        if (visible)
            geo.flags |= GEOMETRY_IS_VISIBLE;
        else
            geo.flags &= ~GEOMETRY_IS_VISIBLE;
    }
}