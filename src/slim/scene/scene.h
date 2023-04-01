#pragma once

#include "./mesh.h"
#include "./grid.h"
#include "./box.h"
#include "./tet.h"
#include "./quad.h"
#include "./camera.h"
#include "./material.h"
#include "./bvh_builder.h"
#include "../core/texture.h"
#include "../core/ray.h"
#include "../core/transform.h"
#include "../serialization/texture.h"
#include "../serialization/mesh.h"

struct SceneCounts {
    u32 geometries{0};
    u32 cameras{1};
    u32 lights{0};
    u32 materials{0};
    u32 textures{0};
    u32 meshes{0};
    u32 grids{0};
    u32 boxes{0};
    u32 tets{0};
    u32 quads{0};
    u32 curves{0};
};

struct Scene {
    SceneCounts counts;
    String file_path;
    BVH bvh;
    BVHBuilder *bvh_builder{nullptr};
    u32 *bvh_leaf_geometry_indices{nullptr};

    AmbientLight ambient_light;

    Geometry *geometries{nullptr};
    Curve *curves{nullptr};
    Grid *grids{nullptr};
    Box *boxes{nullptr};
    Tet *tets{nullptr};
    Quad *quads{nullptr};
    Camera *cameras{nullptr};
    Mesh *meshes{nullptr};
    Texture *textures{nullptr};
    Material *materials{nullptr};
    Light *lights{nullptr};
    AABB *aabbs{nullptr};
    RectI *screen_bounds{nullptr};

    void updateAABB(AABB &aabb, const Geometry &geo, u8 sphere_steps = 255) {
        static QuadVertices quad_vertices;
        static TetVertices tet_vertices;
        static BoxVertices box_vertices;

        BoxVertices mesh_vertices;

        vec3 pos, *vertices;
        u8 vertex_count = 0;
        aabb.min = INFINITY;
        aabb.max = -INFINITY;

        switch (geo.type) {
            case GeometryType_Box   : vertex_count = BOX__VERTEX_COUNT;  vertices = box_vertices.array;  break;
            case GeometryType_Tet   : vertex_count = TET__VERTEX_COUNT;  vertices = tet_vertices.array;  break;
            case GeometryType_Quad  : vertex_count = QUAD__VERTEX_COUNT; vertices = quad_vertices.array; break;
            case GeometryType_Mesh  : vertex_count = BOX__VERTEX_COUNT;  vertices = mesh_vertices.array; mesh_vertices = BoxVertices{meshes[geo.id].aabb}; break;
            case GeometryType_Sphere: {
                if (geo.transform.scale.x == geo.transform.scale.y && geo.transform.scale.x == geo.transform.scale.z) {
                    aabb.min = geo.transform.position - geo.transform.scale;
                    aabb.max = geo.transform.position + geo.transform.scale;
                } else {
                    vec3 center_to_orbit{1.0f, 0.0f, 0.0f};
                    mat3 rotation{mat3::RotationAroundY(TAU / (f32)sphere_steps)};

                    // Transform vertices positions from local-space to world-space:
                    for (u8 i = 0; i < sphere_steps; i++) {
                        center_to_orbit = rotation * center_to_orbit;

                        pos = geo.transform.externPos(center_to_orbit);
                        aabb.min = minimum(aabb.min, pos);
                        aabb.max = maximum(aabb.max, pos);

                        pos = geo.transform.externPos({center_to_orbit.x, center_to_orbit.z, 0.0f});
                        aabb.min = minimum(aabb.min, pos);
                        aabb.max = maximum(aabb.max, pos);

                        pos = geo.transform.externPos({0.0f, center_to_orbit.x, center_to_orbit.z});
                        aabb.min = minimum(aabb.min, pos);
                        aabb.max = maximum(aabb.max, pos);
                    }
                }
            } break;
            default: return;
        }

        if (vertex_count) {
            // Transform vertices positions from local-space to world-space:
            for (u8 i = 0; i < vertex_count; i++) {
                pos = geo.transform.externPos(vertices[i]);
                aabb.min = minimum(aabb.min, pos);
                aabb.max = maximum(aabb.max, pos);
            }
        }
    }

    INLINE_XPU void updateAABBs() {
        for (u32 i = 0; i < counts.geometries; i++)
            updateAABB(aabbs[i], geometries[i]);
    }

    u64 last_io_ticks = 0;
    bool last_io_is_save{false};
    u8 max_bvh_height = 0;
    u32 max_triangle_count = 0;
    u32 max_vertex_positions = 0;
    u32 max_vertex_normals = 0;
    u8 mesh_stack_size = 0;

    u32 *mesh_bvh_node_counts = nullptr;
    u32 *mesh_triangle_counts = nullptr;
    u32 *mesh_vertex_counts = nullptr;

    Scene(SceneCounts counts,
          char *file_path = nullptr,
          Geometry *geometries = nullptr,
          Camera *cameras = nullptr,
          Light *lights = nullptr,
          Material *materials = nullptr,
          Texture *textures = nullptr,
          String *texture_files = nullptr,
          Mesh *meshes = nullptr,
          String *mesh_files = nullptr,
          Grid *grids = nullptr,
          Box *boxes = nullptr,
          Tet *tets = nullptr,
          Quad *quads = nullptr,
          Curve *curves = nullptr,
          memory::MonotonicAllocator *memory_allocator = nullptr
    ) : counts{counts},
        file_path{file_path},
        cameras{cameras},
        geometries{geometries},
        grids{grids},
        boxes{boxes},
        tets{tets},
        quads{quads},
        curves{curves},
        meshes{meshes},
        textures{textures},
        materials{materials},
        lights{lights}
    {
        mesh_stack_size = 0;
        max_triangle_count = 0;
        bvh.node_count = counts.geometries * 2;
        bvh.height = (u8)counts.geometries;

        memory::MonotonicAllocator temp_allocator;
        u32 capacity = sizeof(BVHBuilder) + (sizeof(u32) + sizeof(AABB) + sizeof(RectI)) * counts.geometries + sizeof(BVHNode) * bvh.node_count;

        if (counts.lights && !lights) capacity += sizeof(Material) * counts.lights;
        if (counts.materials && !materials) capacity += sizeof(Material) * counts.materials;
        if (counts.geometries && !geometries) capacity += sizeof(Geometry) * counts.geometries;
        if (counts.boxes && !boxes) capacity += sizeof(Box) * counts.boxes;
        if (counts.tets && !tets) capacity += sizeof(Tet) * counts.tets;
        if (counts.quads && !quads) capacity += sizeof(Quad) * counts.quads;
        if (counts.curves && !curves) capacity += sizeof(Curve) * counts.curves;

        if (counts.textures) {
            if (!textures) capacity += sizeof(Texture) * counts.textures;
            capacity += getTotalMemoryForTextures(texture_files, counts.textures);
        }
        if (counts.meshes) {
            if (!meshes) capacity += sizeof(Mesh) * counts.meshes;
            capacity += getTotalMemoryForMeshes(mesh_files, counts.meshes ,&max_bvh_height, &max_triangle_count);
            capacity += sizeof(u32) * (3 * counts.meshes);
        }
        u32 max_leaf_node_count = max_triangle_count > counts.geometries ? max_triangle_count : counts.geometries;
        capacity += BVHBuilder::getSizeInBytes(max_leaf_node_count);

        if (!memory_allocator) {
            temp_allocator = memory::MonotonicAllocator{capacity};
            memory_allocator = &temp_allocator;
        }

        bvh.nodes = (BVHNode*)memory_allocator->allocate(sizeof(BVHNode) * bvh.node_count);
        bvh_leaf_geometry_indices = (u32*)memory_allocator->allocate(sizeof(u32) * counts.geometries);
        bvh_builder = (BVHBuilder*)memory_allocator->allocate(sizeof(BVHBuilder));
        *bvh_builder = BVHBuilder{max_leaf_node_count, memory_allocator};

        aabbs = (AABB*)memory_allocator->allocate(sizeof(AABB) * counts.geometries);
        screen_bounds = (RectI*)memory_allocator->allocate(sizeof(RectI) * counts.geometries);

        if (counts.geometries && !geometries) {
            geometries = (Geometry*)memory_allocator->allocate(sizeof(Geometry) * counts.geometries);
            for (u32 i = 0; i < counts.geometries; i++) geometries[i] = Geometry{};
        }
        if (counts.boxes && !boxes) {
            boxes = (Box*)memory_allocator->allocate(sizeof(Box) * counts.boxes);
            for (u32 i = 0; i < counts.boxes; i++) boxes[i] = Box{};
        }
        if (counts.tets && !tets) {
            tets = (Tet*)memory_allocator->allocate(sizeof(Tet) * counts.tets);
            for (u32 i = 0; i < counts.tets; i++) tets[i] = Tet{};
        }
        if (counts.quads && !quads) {
            quads = (Quad*)memory_allocator->allocate(sizeof(Quad) * counts.quads);
            for (u32 i = 0; i < counts.quads; i++) quads[i] = Quad{};
        }
        if (counts.curves && !curves) {
            curves = (Curve*)memory_allocator->allocate(sizeof(Curve) * counts.curves);
            for (u32 i = 0; i < counts.curves; i++) curves[i] = Curve{};
        }
        if (counts.lights && !lights) {
            lights = (Light*)memory_allocator->allocate(sizeof(Light) * counts.lights);
            for (u32 i = 0; i < counts.lights; i++) lights[i] = Light{};
        }
        if (counts.materials && !materials) {
            materials = (Material*)memory_allocator->allocate(sizeof(Material) * counts.materials);
            for (u32 i = 0; i < counts.materials; i++) materials[i] = Material{};
        }
        if (counts.textures && texture_files) {
            if (!textures) textures = (Texture*)memory_allocator->allocate(sizeof(Texture) * counts.textures);
            for (u32 i = 0; i < counts.textures; i++)
                load(textures[i], texture_files[i].char_ptr, memory_allocator);
        }
        if (counts.meshes && mesh_files) {
            if (!meshes) meshes = (Mesh*)memory_allocator->allocate(sizeof(Mesh) * counts.meshes);
            for (u32 i = 0; i < counts.meshes; i++) meshes[i] = Mesh{};

            mesh_bvh_node_counts = (u32*)memory_allocator->allocate(sizeof(u32) * counts.meshes);
            mesh_triangle_counts = (u32*)memory_allocator->allocate(sizeof(u32) * counts.meshes);
            mesh_vertex_counts   = (u32*)memory_allocator->allocate(sizeof(u32) * counts.meshes);

            max_vertex_positions = 0;
            max_vertex_normals = 0;
            for (u32 i = 0; i < counts.meshes; i++) {
                load(meshes[i], mesh_files[i].char_ptr, memory_allocator);
                mesh_bvh_node_counts[i] = meshes[i].bvh.node_count;
                mesh_triangle_counts[i] = meshes[i].triangle_count;
                if (meshes[i].vertex_count  > max_vertex_positions) max_vertex_positions = meshes[i].vertex_count;
                if (meshes[i].normals_count > max_vertex_normals)   max_vertex_normals   = meshes[i].normals_count;
                if (meshes[i].bvh.height > mesh_stack_size)         mesh_stack_size      = meshes[i].bvh.height;
            }
            mesh_stack_size += 2;
        }

        updateAABBs();
        updateBVH();
    }

    void updateBVH(u16 max_leaf_size = 1) {
        for (u32 i = 0; i < counts.geometries; i++) {
            bvh_builder->nodes[i].aabb = aabbs[i];
            bvh_builder->nodes[i].first_index = bvh_builder->node_ids[i] = i;
        }

        bvh_builder->build(bvh, counts.geometries, max_leaf_size);

        for (u32 i = 0; i < counts.geometries; i++)
            bvh_leaf_geometry_indices[i] = bvh_builder->leaf_ids[i];
    }

    INLINE bool castRay(Ray &ray, RayHit &hit, Geometry **hit_geo, Light **hit_light) const {
        static Ray local_ray;
        static RayHit local_hit;
        static Transform xform;
        bool found = false;
        *hit_geo = nullptr;
        *hit_light = nullptr;
        hit.distance = local_hit.distance = INFINITY;

        for (u32 i = 0; i < counts.geometries; i++) {
            Geometry &geo = geometries[i];
            xform = geo.transform;
            if (geo.type == GeometryType_Mesh)
                xform.scale *= meshes[geo.id].aabb.max;

            local_ray.localize(ray, xform);
            if (local_ray.hitsDefaultBox(local_hit)) {
                hit = local_hit;
                *hit_geo = geometries + i;
                found = true;
            }
        }

        if (lights) {
            for (u32 i = 0; i < counts.lights; i++) {
                Light &light = lights[i];
                if (light.is_directional)
                    continue;

                f32 light_radius = light.intensity * (1.0f / (8.0f * 16.0f));
                xform.position = light.position_or_direction;
                xform.scale = light_radius;
                xform.orientation.reset();

                local_ray.localize(ray, xform);
                if (local_ray.hitsDefaultSphere(local_hit)) {
                    hit = local_hit;
                    *hit_geo = nullptr;
                    *hit_light = lights + i;
                    found = true;
                }
            }
        }

        if (found) {
            hit.position = ray[hit.distance];
            if (*hit_geo)
            hit.normal = (*hit_geo)->transform.externDir(hit.normal);
        }

        return found;
    }
};