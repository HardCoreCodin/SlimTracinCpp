#pragma once

#include "./edge.h"
#include "../core/transform.h"
#include "../scene/tet.h"

void drawTet(const Tet &tet, const Transform &transform, const Viewport &viewport,
             const Color &color = White, f32 opacity = 1.0f, u8 line_width = 1) {

    static Tet view_space_tet;

    // Transform vertices positions from local-space to world-space and then to view-space:
    for (u8 i = 0; i < TET__VERTEX_COUNT; i++)
        view_space_tet.vertices.array[i] = viewport.camera->internPos(transform.externPos(tet.vertices.array[i]));

    // Distribute transformed vertices positions to edges:
    view_space_tet.edges.setFrom(view_space_tet.vertices);

    for (const auto &edge : view_space_tet.edges.array)
        drawEdge(edge, viewport, color, opacity, line_width);
}