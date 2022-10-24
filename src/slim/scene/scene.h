#pragma once

#include "./mesh.h"
#include "./grid.h"
#include "./box.h"
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
    Camera *cameras{nullptr};
    Mesh *meshes{nullptr};
    Texture *textures{nullptr};
    Material *materials{nullptr};
    Light *lights{nullptr};

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
          Curve *curves = nullptr,
          memory::MonotonicAllocator *memory_allocator = nullptr
    ) : counts{counts},
        file_path{file_path},
        cameras{cameras},
        geometries{geometries},
        grids{grids},
        boxes{boxes},
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
        u32 capacity = sizeof(BVHBuilder) + sizeof(u32) * counts.geometries + sizeof(BVHNode) * bvh.node_count;

        if (counts.lights && !lights) capacity += sizeof(Material) * counts.lights;
        if (counts.materials && !materials) capacity += sizeof(Material) * counts.materials;
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

        updateBVH();
    }

    void updateBVH() {
        AABB aabb;
        BVHNode *node = bvh_builder->nodes;
        Geometry *geometry = geometries;
        for (u32 i = 0; i < counts.geometries; i++, node++, geometry++) {
            aabb = geometry->type == GeometryType_Mesh ? meshes[geometry->id].aabb : geometry->getAABB();
            node->aabb = geometry->transform.externAABB(aabb);
            node->first_index = bvh_builder->node_ids[i] = i;
        }

        bvh_builder->build(bvh, counts.geometries, 2);

        for (u32 i = 0; i < counts.geometries; i++)
            bvh_leaf_geometry_indices[i] = bvh_builder->leaf_ids[i];
    }

    INLINE bool castRay(Ray &ray) const {
        static Ray local_ray;
        static Transform xform;

        bool found{false};
        bool current_found{false};

        Geometry *geo = geometries;

        for (u32 i = 0; i < counts.geometries; i++, geo++) {
            xform = geo->transform;
            if (geo->type == GeometryType_Mesh)
                xform.scale *= meshes[geo->id].aabb.max;

            xform.internPosAndDir(ray.origin, ray.direction, local_ray.origin, local_ray.direction);

            current_found = local_ray.hitsCube();
            if (current_found) {
                local_ray.hit.position         = xform.externPos(local_ray.hit.position);
                local_ray.hit.distance_squared = (local_ray.hit.position - ray.origin).squaredLength();
                if (local_ray.hit.distance_squared < ray.hit.distance_squared) {
                    ray.hit = local_ray.hit;
                    ray.hit.geo_type = geo->type;
                    ray.hit.geo_id = i;
                    found = true;
                }
            }
        }

        if (found) {
            ray.hit.distance = sqrtf(ray.hit.distance_squared);
            ray.hit.normal = geometries[ray.hit.geo_id].transform.externDir(ray.hit.normal).normalized();
        }

        return found;
    }
};