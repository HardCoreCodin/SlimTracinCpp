#pragma once

#include "./edge.h"
#include "../scene/scene.h"
#include "../draw/rectangle.h"

void drawSSB(const Scene &scene, Canvas &canvas) {
    ColorID color;
    for (u32 i = 0; i < scene.counts.geometries; i++) {
        Geometry &geometry = scene.geometries[i];
        if (geometry.flags & GEOMETRY_IS_VISIBLE) {
            switch (geometry.type) {
                case GeometryType_Box   : color = Cyan;    break;
                case GeometryType_Quad  : color = White;   break;
                case GeometryType_Sphere: color = Yellow;  break;
                case GeometryType_Tet   : color = Magenta; break;
                case GeometryType_Mesh  : color = Red;     break;
                default:
                    continue;
            }
            canvas.drawRect(scene.screen_bounds[i], color);
        }
    }
}