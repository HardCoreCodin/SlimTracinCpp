#pragma once

#include "../math/vec3.h"

union QuadVertices {
    struct Corners {
        vec3 left_back;
        vec3 left_front;
        vec3 right_back;
        vec3 right_front;
    } corners;
    vec3 array[QUAD__VERTEX_COUNT];

    QuadVertices(f32 size = 1.0f) : corners{
        {-size, 0, -size},
        {-size, 0, +size},
        {+size, 0, -size},
        {+size, 0, +size}
    } {}
};

union QuadEdges {
    struct Sides {
        Edge left, right, back, front;
    } sides;
    Edge array[QUAD__EDGE_COUNT];

    QuadEdges(const QuadVertices &vertices = QuadVertices{}) { setFrom(vertices); }
    void setFrom(const QuadVertices &vertices) {
        sides.left = {vertices.corners.left_back, vertices.corners.left_front};
        sides.right = {vertices.corners.right_front, vertices.corners.right_back};
        sides.back = {vertices.corners.left_back, vertices.corners.right_back};
        sides.front = {vertices.corners.right_front, vertices.corners.left_front};
    }
};

struct Quad {
    QuadVertices vertices;
    QuadEdges edges;

    Quad() : vertices{}, edges{vertices} {}
};
