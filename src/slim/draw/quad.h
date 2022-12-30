#pragma once

#include "./edge.h"
#include "../core/transform.h"
#include "../scene/quad.h"

void drawQuad(const Quad &quad, const Transform &transform, const Viewport &viewport,
             const Color &color = White, f32 opacity = 1.0f, u8 line_width = 1) {

    static Quad view_space_quad;

    // Transform vertices positions from local-space to world-space and then to view-space:
    for (u8 i = 0; i < QUAD__VERTEX_COUNT; i++)
        view_space_quad.vertices.array[i] = viewport.camera->internPos(transform.externPos(quad.vertices.array[i]));

    // Distribute transformed vertices positions to edges:
    view_space_quad.edges.setFrom(view_space_quad.vertices);

    for (const auto &edge : view_space_quad.edges.array)
        drawEdge(edge, viewport, color, opacity, line_width);
}