#pragma once

#include "./edge.h"
#include "../core/transform.h"

void drawSphere(const Transform &transform, const Viewport &viewport,
                const Color &color = White, f32 opacity = 1.0f, u8 line_width = 0, u32 step_count = CURVE_STEPS) {
    const Camera &cam = *viewport.camera;
    vec3 center_to_orbit{1, 0, 0};
    mat3 rotation{mat3::RotationAroundY(TAU / (f32)step_count)};

    // Transform vertices positions of edges from view-space to screen-space (w/ culling and clipping):
    vec3 local_position, local_previous_position, current_position, previous_position;
    Edge edge;

    for (u32 i = 0; i < step_count; i++) {
        local_position = center_to_orbit = rotation * center_to_orbit;
        current_position = cam.internPos(transform.externPos(local_position));

        if (i) {
            edge.from = previous_position;
            edge.to   = current_position;
            drawEdge(edge, viewport, color, opacity, line_width);

            edge.from.x = local_previous_position.x;
            edge.from.y = local_previous_position.z;
            edge.from.z = 0;

            edge.to.x = local_position.x;
            edge.to.y = local_position.z;
            edge.to.z = 0;

            edge.from = cam.internPos(transform.externPos(edge.from));
            edge.to   = cam.internPos(transform.externPos(edge.to));
            drawEdge(edge, viewport, color, opacity, line_width);

            edge.from.x = 0;
            edge.from.y = local_previous_position.x;
            edge.from.z = local_previous_position.z;

            edge.to.x = 0;
            edge.to.y = local_position.x;
            edge.to.z = local_position.z;

            edge.from = cam.internPos(transform.externPos(edge.from));
            edge.to   = cam.internPos(transform.externPos(edge.to));
            drawEdge(edge, viewport, color, opacity, line_width);
        }

        previous_position = current_position;
        local_previous_position = local_position;
    }
}