#pragma once

#include "./box.h"
#include "./tet.h"
#include "./quad.h"
#include "./curve.h"
#include "./sphere.h"
#include "./mesh.h"
#include "../core/transform.h"
#include "../scene/scene.h"

void drawScene(const Scene &scene, const Viewport &viewport, float opacity = 0.5f) {
    static Box box{};
    static Tet tet{};
    static Quad quad{};

    for (u32 i = 0; i < scene.counts.geometries; i++) {
        Geometry &geo{scene.geometries[i]};
        Transform &transform{geo.transform};
        Mesh &mesh{scene.meshes[geo.id]};
        Color color{geo.color};
        switch (geo.type) {
            case GeometryType_Quad  : drawQuad(quad, transform, viewport, color, opacity); break;
            case GeometryType_Box   : drawBox(box,   transform, viewport, color, opacity); break;
            case GeometryType_Tet   : drawTet(tet,   transform, viewport, color, opacity); break;
            case GeometryType_Sphere: drawSphere(    transform, viewport, color, opacity); break;
            case GeometryType_Mesh  : drawMesh(mesh, transform, false, viewport, color, opacity); break;
            default: break;
        }
    }
}