#pragma once

#include "../trace.h"

INLINE_XPU vec3 shadeUV(vec2 uv) { return vec3{uv.x, uv.y, 1} / 2.0f; }
INLINE_XPU vec3 shadeDirection(vec3 direction) { return (direction + 1.0f) / 2.0f; }
INLINE_XPU vec3 shadeDepth(f32 distance) { return {4 / distance}; }
INLINE_XPU vec3 shadeDirectionAndDepth(vec3 direction, f32 distance) { return (direction + 1.0f) * (4 / distance); }