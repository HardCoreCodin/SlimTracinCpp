#pragma once

#include "./ray_tracer.h"
#include "./surface_shader.h"


#define USE_GPU_BY_DEFAULT true
#define MESH_BVH_STACK_SIZE 16
#define SCENE_BVH_STACK_SIZE 6
#define SLIM_THREADS_PER_BLOCK 64

__constant__ SceneData d_scene;
__constant__ CanvasData d_canvas;

SceneData t_scene;
CanvasData t_canvas;
BVHNode *d_mesh_bvh_nodes;
Triangle *d_triangles;
TextureMip *d_texture_mips;
TexelQuad *d_texel_quads;

__global__ void d_render(const RayTracerSettings settings, const CameraRayProjection projection) {
    u32 s = d_canvas.antialias == SSAA ? 2 : 1;
    u32 i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= (d_canvas.dimensions.width_times_height * s * s))
        return;

    u32 scene_stack[SCENE_BVH_STACK_SIZE], mesh_stack[MESH_BVH_STACK_SIZE];
    SceneTracer scene_tracer{scene_stack, mesh_stack};
    SurfaceShader surface;
    Ray ray;
    RayHit hit;
    Color color;
    f32 depth;

    ray.pixel_coords.x = (i32)(i % ((u32)d_canvas.dimensions.width * s));
    ray.pixel_coords.y = (i32)(i / ((u32)d_canvas.dimensions.width * s));
    hit.scaling_factor = 1.0f / sqrtf(projection.squared_distance_to_projection_plane +
        vec2{ray.pixel_coords.x,
            -ray.pixel_coords.y}.scaleAdd(projection.sample_size,projection.C_start).squaredLength());

    renderPixel(settings, projection, *((Scene*)(&d_scene)), scene_tracer, surface, ray, hit,
                projection.getRayDirectionAt(ray.pixel_coords.x, ray.pixel_coords.y), color, depth);

    ((Canvas*)(&d_canvas))->setPixel(ray.pixel_coords.x, ray.pixel_coords.y, color, -1, depth);
}

void renderOnGPU(const Canvas &canvas, const CameraRayProjection &projection, const RayTracerSettings &settings) {
    t_canvas.dimensions = canvas.dimensions;
    t_canvas.antialias = canvas.antialias;
    uploadConstant(&t_canvas, d_canvas)

    u32 pixel_count = canvas.dimensions.width_times_height * (canvas.antialias == SSAA ? 4 : 1);
    u32 depths_count = canvas.dimensions.width_times_height * (canvas.antialias == NoAA ? 1 : 4);
    u32 threads = SLIM_THREADS_PER_BLOCK;
    u32 blocks  = pixel_count / threads;
    if (pixel_count < threads) {
        threads = pixel_count;
        blocks = 1;
    } else if (pixel_count % threads)
        blocks++;

    d_render<<<blocks, threads>>>(settings, projection);

    checkErrors()
    downloadN(t_canvas.pixels, canvas.pixels, pixel_count)
    downloadN(t_canvas.depths, canvas.depths, depths_count)
}

void uploadGeometries(const Scene &scene) { if (scene.counts.geometries) uploadN(scene.geometries, t_scene.geometries, scene.counts.geometries) }
void uploadMaterials(const Scene &scene)  { if (scene.counts.materials)  uploadN(scene.materials,  t_scene.materials,  scene.counts.materials) }
void uploadCameras(const Scene &scene)    { if (scene.counts.cameras)    uploadN(scene.cameras,    t_scene.cameras,    scene.counts.cameras) }
void uploadLights(const Scene &scene)     { if (scene.counts.lights)     uploadN(scene.lights,     t_scene.lights,     scene.counts.lights) }

void uploadSceneBVH(const Scene &scene)   {
    if (scene.bvh.node_count   ) uploadN(scene.bvh.nodes,                 t_scene.bvh.nodes,                 scene.bvh.node_count)
    if (scene.counts.geometries) uploadN(scene.bvh_leaf_geometry_indices, t_scene.bvh_leaf_geometry_indices, scene.counts.geometries)
}

void initDataOnGPU(const Scene &scene) {
    t_scene = scene;
    gpuErrchk(cudaMalloc(&t_canvas.pixels, sizeof(Pixel) * MAX_WINDOW_SIZE * 4))
    gpuErrchk(cudaMalloc(&t_canvas.depths, sizeof(f32) * MAX_WINDOW_SIZE * 4))
    gpuErrchk(cudaMalloc(&t_scene.bvh_leaf_geometry_indices, sizeof(u32) * scene.counts.geometries))
    gpuErrchk(cudaMalloc(&t_scene.bvh.nodes,sizeof(BVHNode)  * scene.counts.geometries * 2))

    uploadSceneBVH(scene);

    u32 total_triangles = 0;

    if (scene.counts.geometries) {
        gpuErrchk(cudaMalloc(&t_scene.geometries,sizeof(Geometry) * scene.counts.geometries))
        uploadGeometries(scene);
    }

    if (scene.counts.materials) {
        gpuErrchk(cudaMalloc(&t_scene.materials,sizeof(Material) * scene.counts.materials))
        uploadMaterials(scene);
    }

    if (scene.counts.lights) {
        gpuErrchk(cudaMalloc(&t_scene.lights,    sizeof(Light)    * scene.counts.lights))
        uploadLights(scene);
    }

    if (scene.counts.cameras) {
        gpuErrchk(cudaMalloc(&t_scene.cameras,    sizeof(Camera)    * scene.counts.cameras))
        uploadCameras(scene);
    }

    if (scene.counts.meshes) {
        u32 total_bvh_nodes = 0;
        for (u32 i = 0; i < scene.counts.meshes; i++) {
            total_triangles += scene.meshes[i].triangle_count;
            total_bvh_nodes += scene.meshes[i].bvh.node_count;
        }

        gpuErrchk(cudaMalloc(&t_scene.meshes,   sizeof(Mesh)     * scene.counts.meshes))
        gpuErrchk(cudaMalloc(&d_triangles,      sizeof(Triangle) * total_triangles))
        gpuErrchk(cudaMalloc(&d_mesh_bvh_nodes, sizeof(BVHNode)  * total_bvh_nodes))

        Mesh d_mesh;
        Mesh *mesh = scene.meshes;
        Mesh *d_mehses = t_scene.meshes;
        Triangle *triangles = d_triangles;
        BVHNode *nodes = d_mesh_bvh_nodes;
        for (u32 i = 0; i < scene.counts.meshes; i++, mesh++) {
            uploadN(mesh->bvh.nodes, nodes, mesh->bvh.node_count)
            uploadN(mesh->triangles, triangles, mesh->triangle_count)

            d_mesh = *mesh;
            d_mesh.triangles = triangles;
            d_mesh.bvh.nodes = nodes;
            uploadN(&d_mesh, d_mehses, 1)
            d_mehses++;

            nodes     += mesh->bvh.node_count;
            triangles += mesh->triangle_count;
        }
    }

    if (scene.counts.textures) {
        u32 total_mip_count = 0;
        u32 total_texel_quads_count = 0;
        Texture *texture = scene.textures;
        for (u32 i = 0; i < scene.counts.textures; i++, texture++) {
            total_mip_count += texture->mip_count;
            TextureMip *mip = texture->mips;
            for (u32 m = 0; m < texture->mip_count; m++, mip++)
                total_texel_quads_count += (mip->width + 1) * (mip->height + 1);
        }
        gpuErrchk(cudaMalloc(&t_scene.textures, sizeof(Texture)    * scene.counts.textures))
        gpuErrchk(cudaMalloc(&d_texture_mips,   sizeof(TextureMip) * total_mip_count))
        gpuErrchk(cudaMalloc(&d_texel_quads,    sizeof(TexelQuad)  * total_texel_quads_count))

        TexelQuad *d_quads = d_texel_quads;
        TextureMip *d_mips = d_texture_mips;
        Texture *d_textures = t_scene.textures;
        Texture d_texture;
        texture = scene.textures;
        for (u32 i = 0; i < scene.counts.textures; i++, texture++) {
            d_texture = *texture;
            d_texture.mips = d_mips;
            uploadN(&d_texture, d_textures, 1)
            d_textures++;

            for (u32 m = 0; m < texture->mip_count; m++) {
                TextureMip mip = texture->mips[m];
                u32 quad_count = (mip.width + 1) * (mip.height + 1);
                uploadN( mip.texel_quads, d_quads, quad_count)

                mip.texel_quads = d_quads;
                uploadN(&mip, d_mips, 1)
                d_quads += quad_count;
                d_mips++;
            }
        }
    }

    uploadConstant(&t_scene, d_scene)
}
