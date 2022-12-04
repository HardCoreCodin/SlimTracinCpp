#pragma once

#include "../core/ray.h"
#include "../core/transform.h"
#include "../scene/material.h"

INLINE_XPU vec3 getAreaLightVector(Transform &transform, vec3 P, vec3 *v) {
    const f32 sx = transform.scale.x;
    const f32 sz = transform.scale.z;
    if (sx == 0 || sz == 0)
        return 0.0f;

    vec3 U{transform.rotation * vec3{sx < 0 ? -sx : sx, 0.0f, 0.0f}};
    vec3 V{transform.rotation * vec3{0.0f, 0.0f ,sz < 0 ? -sz : sz}};
    v[0] = transform.position - U - V;
    v[1] = transform.position + U - V;
    v[2] = transform.position + U + V;
    v[3] = transform.position - U + V;

    vec3 u1n{(v[0] - P).normalized()};
    vec3 u2n{(v[1] - P).normalized()};
    vec3 u3n{(v[2] - P).normalized()};
    vec3 u4n{(v[3] - P).normalized()};

    return {
            u1n.cross(u2n) * (acosf(u1n.dot(u2n)) * 0.5f) +
            u2n.cross(u3n) * (acosf(u2n.dot(u3n)) * 0.5f) +
            u3n.cross(u4n) * (acosf(u3n.dot(u4n)) * 0.5f) +
            u4n.cross(u1n) * (acosf(u4n.dot(u1n)) * 0.5f)
    };
}