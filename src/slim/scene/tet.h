#pragma once

#include "../math/vec3.h"

union TetVertices {
    struct Corners {
        vec3 left_bottom_back;
        vec3 left_top_front;
        vec3 right_top_back;
        vec3 right_bottom_front;
    } corners;
    vec3 array[TET__VERTEX_COUNT];

    TetVertices(f32 size = TET_MAX) : corners{
        {-size, -size, -size},
        {-size, +size, +size},
        {+size, +size, -size},
        {+size, -size, +size}
    } {}
};

union TetEdges {
    struct Sides {
        Edge left, right, top, bottom, back, front;
    } sides;
    Edge array[TET__EDGE_COUNT];

    TetEdges(const TetVertices &vertices = TetVertices{}) { setFrom(vertices); }
    void setFrom(const TetVertices &vertices) {
        sides.left = {vertices.corners.left_bottom_back, vertices.corners.left_top_front};
        sides.right = {vertices.corners.right_bottom_front, vertices.corners.right_top_back};
        sides.top = {vertices.corners.left_top_front, vertices.corners.right_top_back};
        sides.bottom = {vertices.corners.left_bottom_back, vertices.corners.right_bottom_front};
        sides.back = {vertices.corners.left_bottom_back, vertices.corners.right_top_back};
        sides.front = {vertices.corners.right_bottom_front, vertices.corners.left_top_front};
    }
};

struct Tet {
    TetVertices vertices;
    TetEdges edges;

    Tet() : vertices{}, edges{vertices} {}
};
