#pragma once

#include "../viewport/viewport.h"
#include "./scene_tracer.h"
#include "./surface_shader.h"
//#include "./SSB.h"



struct RayTracerSettings {
    mat3 inverted_camera_rotation;
    vec3 start, right, down, camera_position;
    f32 squared_distance_to_projection_plane;
    u32 max_depth = 5;
    RenderMode render_mode = RenderMode_Beauty;

    INLINE_XPU f32 getDepthAt(vec3 &position) const {
        return (inverted_camera_rotation * (position - camera_position)).z;
    }

    void reset(const Camera &camera, const Canvas &canvas) {
        const Dimensions &dim = canvas.dimensions;

        f32 left_of_center = 1.0f + (canvas.antialias == SSAA ? -(dim.f_width) : -dim.h_width);
        f32 up_of_center = -1.0f + (canvas.antialias == SSAA ? (dim.f_height) : dim.h_height);

        inverted_camera_rotation = camera.orientation.inverted();
        camera_position = camera.position;
        down = -camera.orientation.up;
        right = camera.orientation.right;
        start = camera.orientation.forward.scaleAdd(camera.focal_length * dim.h_height, right.scaleAdd(left_of_center, camera.orientation.up * up_of_center));

        squared_distance_to_projection_plane = dim.h_height * camera.focal_length;
        squared_distance_to_projection_plane *= squared_distance_to_projection_plane;
    }
};

struct Trace {
    SceneTracer scene_tracer;
    LightsShader lights_shader;
    SurfaceShader surface;
    Ray ray;
    RayHit hit;
    vec2 screen_center_to_pixel_center;
    f32 z_depth;

    INLINE_XPU Trace(u32 *stack, u32 *mesh_stack) : scene_tracer{stack, mesh_stack} {}

    explicit Trace(u32 stack_size, u32 mesh_stack_size, memory::MonotonicAllocator *memory_allocator = nullptr) :
        scene_tracer{stack_size, mesh_stack_size, memory_allocator}  {}

    INLINE_XPU void renderPixel(const Scene &scene, const RayTracerSettings &settings, vec3 &direction, Color &color) {
        color = Black;
        z_depth = INFINITY;

        f32 scaling_factor = 1.0f / direction.length();
        ray.reset(settings.camera_position, direction * scaling_factor);
        surface.geometry = scene_tracer.trace(ray, hit, scene);

        bool hit_found = surface.geometry != nullptr;
        if (hit_found) {
            hit.scaling_factor = scaling_factor / sqrtf(
                screen_center_to_pixel_center.squaredLength() +
                settings.squared_distance_to_projection_plane
            );
            scene_tracer.finalizeHit(surface.geometry, scene.materials, ray, hit);
            surface.prepareForShading(ray, hit, scene.materials, scene.textures);
            z_depth = settings.getDepthAt(hit.position);

            switch (settings.render_mode) {
                case RenderMode_UVs      : color = getColorByUV(hit.uv); break;
                case RenderMode_Depth    : color = getColorByDistance(hit.distance); break;
//                case RenderMode_MipLevel : color = surface.material->isTextured() ? MIP_LEVEL_COLORS[scene.textures[0].mipLevel(hit.uv_coverage)] : Grey;  break;
                case RenderMode_Normals  : color = directionToColor(hit.normal);  break;
                case RenderMode_NormalMap: color = directionToColor(sampleNormal(*surface.material, hit, scene.textures));  break;
                default: {
                    if (!hit.from_behind && surface.material->isEmissive())
                        color = surface.material->emission;
                    else if (scene.lights)
                        shadePixel(scene, settings, color);
                }
            }
        } else {
            hit_found = lights_shader.shadeLights(scene.lights, scene.counts.lights, settings.camera_position, ray.direction, INFINITY, color);
            if (hit_found) z_depth = INFINITY;
        }
        if (hit_found && settings.render_mode == RenderMode_Beauty) color.applyToneMapping();
    }

    INLINE_XPU void shadePixel(const Scene &scene, const RayTracerSettings &settings, Color &color) {
        f32 max_distance = hit.distance;

        Color current_color;
        Color throughput = 1.0f;
        u32 depth_left = settings.max_depth;
        ray.depth = scene_tracer.shadow_ray.depth = scene_tracer.aux_ray.depth = 1;
        while (depth_left) {
            current_color = Black;

            for (u32 i = 0; i < scene.counts.lights; i++)
                surface.shadeFromLight(scene.lights[i], scene, scene_tracer, current_color);

            if (scene.flags & SCENE_HAD_EMISSIVE_QUADS &&
                surface.geometry != (scene.geometries + 4) &&
                surface.shadeFromEmissiveQuads(scene, scene_tracer.shadow_ray, scene_tracer.shadow_hit, current_color))
                max_distance = hit.distance;

            color = current_color.mulAdd(throughput, color);
            if (scene.lights)
                lights_shader.shadeLights(scene.lights, scene.counts.lights, ray.origin, ray.direction, max_distance, color);

            if ((surface.material->isReflective() ||
                 surface.material->isRefractive()) &&
                --depth_left) {
                ray.depth++;
                scene_tracer.aux_ray.depth++;
                scene_tracer.shadow_ray.depth++;
                vec3 &next_ray_direction{surface.material->isRefractive() ? surface.RF : surface.R};

                Color next_ray_throughput{White};
                surface.refracted = surface.refracted && surface.material->isRefractive();
                if (surface.material->brdf == BRDF_CookTorrance) {
                    surface.F = schlickFresnel(clampedValue(surface.N.dot(surface.R)), surface.material->reflectivity);

                    if (surface.refracted)
                        next_ray_throughput -= surface.F;
                    else
                        next_ray_throughput = surface.F;
                } else {
                    if (surface.refracted)
                        next_ray_throughput = surface.material->reflectivity;
                    else
                        next_ray_throughput -= surface.material->reflectivity;
                }
                throughput *= next_ray_throughput;

                ray.origin = hit.position;
                ray.direction = next_ray_direction;
                surface.geometry = scene_tracer.findClosestGeometry(ray, hit, scene);
                if (surface.geometry) {
                    surface.prepareForShading(ray, hit, scene.materials, scene.textures);
                    if (surface.geometry->type == GeometryType_Quad && surface.material->isEmissive()) {
                        color = hit.from_behind ? Black : surface.material->emission;
                        break;
                    }

                    continue;
                }
            }

            break;
        }
    }
};


#ifdef __CUDACC__

#define USE_GPU_BY_DEFAULT true
#define MESH_BVH_STACK_SIZE 16
#define SCENE_BVH_STACK_SIZE 6
#define SLIM_THREADS_PER_BLOCK 32

Pixel *d_pixels;
f32 *d_depths;
__constant__ Dimensions d_dimensions;
Scene d_scene, *d_scene_ptr;
BVHNode *d_mesh_bvh_nodes;
Triangle *d_triangles;
TextureMip *d_texture_mips;
TexelQuad *d_texel_quads;

__global__ void d_render(RayTracerSettings settings, Scene *scene, Pixel *pixels, f32 *depths) {
    u32 i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= d_dimensions.width_times_height)
        return;

    u32 scene_stack[SCENE_BVH_STACK_SIZE], mesh_stack[MESH_BVH_STACK_SIZE];
    Trace trace{scene_stack, mesh_stack};

    trace.ray.pixel_coords.x = (i32)(i % (u32)d_dimensions.width);
    trace.ray.pixel_coords.y = (i32)(i / (u32)d_dimensions.width);
    trace.screen_center_to_pixel_center.x = 0.5f - d_dimensions.h_width  + (f32)trace.ray.pixel_coords.x;
    trace.screen_center_to_pixel_center.y = d_dimensions.h_height - 0.5f - (f32)trace.ray.pixel_coords.y;

    Color color{};
    vec3 direction{
        settings.start +
        settings.down  * trace.ray.pixel_coords.y +
        settings.right * trace.ray.pixel_coords.x
    };
    trace.renderPixel(*scene, settings, direction,  color);

    Canvas canvas{pixels, depths, d_dimensions};
    canvas.setPixel(trace.ray.pixel_coords.x, trace.ray.pixel_coords.y, color, -1, trace.z_depth);
}
#else
#define USE_GPU_BY_DEFAULT false
#endif

struct RayTracer {
    Scene &scene;
    Trace trace;
    RayTracerSettings settings;

    explicit RayTracer(Scene &scene, memory::MonotonicAllocator *memory_allocator = nullptr) :
              scene{scene}, trace{scene.counts.geometries, scene.mesh_stack_size, memory_allocator} {
        initDataOnGPU();
    }

    void render(const Viewport &viewport, bool update_scene = true, bool use_GPU = false) {
        const Camera &camera = *viewport.camera;
        const Canvas &canvas = viewport.canvas;

        trace.ray.origin = camera.position;
        settings.reset(camera, canvas);

        if (update_scene) {
            scene.updateAABBs();
            scene.updateBVH();
            if (use_GPU) {
                uploadLights();
                uploadCameras();
                uploadGeometries();
                uploadSceneBVH();
            }
        }
        if (use_GPU) renderOnGPU(canvas);
        else         renderOnCPU(canvas);
    }

    void renderOnCPU(const Canvas &canvas) {
        const Dimensions &dim{canvas.dimensions};
        u16 width = dim.width * (canvas.antialias == SSAA ? 2 : 1);
        u16 height = dim.height * (canvas.antialias == SSAA ? 2 : 1);

        f32 &Cx = trace.screen_center_to_pixel_center.x;
        f32 &Cy = trace.screen_center_to_pixel_center.y;
        i32 &x = trace.ray.pixel_coords.x;
        i32 &y = trace.ray.pixel_coords.y;
        f32 Cx_start = 0.5f - dim.h_width;

        vec3 direction;
        Color color;

        for (Cy = dim.h_height - 0.5f, y = 0; y < height;
             Cy -= 1.0f, y++, settings.start += settings.down) {
            for (direction = settings.start, Cx = Cx_start, x = 0;  x < width;
                 direction += settings.right, Cx += 1.0f, x++) {
                trace.renderPixel(scene, settings, direction,  color);
                canvas.setPixel(x, y, color, -1, trace.z_depth);
            }
        }
    }

#ifdef __CUDACC__
    void renderOnGPU(const Canvas &canvas) {
        uploadConstant(&canvas.dimensions, d_dimensions)

        u32 pixel_count = canvas.dimensions.width_times_height;
        u32 threads = SLIM_THREADS_PER_BLOCK;
        u32 blocks  = pixel_count / threads;
        if (pixel_count < threads) {
            threads = pixel_count;
            blocks = 1;
        } else if (pixel_count % threads)
            blocks++;

        d_render<<<blocks, threads>>>(settings, d_scene_ptr, d_pixels, d_depths);

        checkErrors()
        downloadN(d_pixels, canvas.pixels, pixel_count)
        downloadN(d_depths, canvas.depths, pixel_count)
    }

    void initDataOnGPU() {
        d_scene = scene;
        gpuErrchk(cudaMalloc(&d_pixels, sizeof(Pixel) * MAX_WINDOW_SIZE))
        gpuErrchk(cudaMalloc(&d_depths, sizeof(f32) * MAX_WINDOW_SIZE))
        gpuErrchk(cudaMalloc(&d_scene_ptr, sizeof(Scene)))
        gpuErrchk(cudaMalloc(&d_scene.bvh_leaf_geometry_indices, sizeof(u32) * scene.counts.geometries))
        gpuErrchk(cudaMalloc(&d_scene.bvh.nodes,sizeof(BVHNode)  * scene.counts.geometries * 2))

        uploadSceneBVH();

        u32 total_triangles = 0;

        if (scene.counts.geometries) {
            gpuErrchk(cudaMalloc(&d_scene.geometries,sizeof(Geometry) * scene.counts.geometries))
            uploadGeometries();
        }

        if (scene.counts.materials) {
            gpuErrchk(cudaMalloc(&d_scene.materials,sizeof(Material) * scene.counts.materials))
            uploadMaterials();
        }

        if (scene.counts.lights) {
            gpuErrchk(cudaMalloc(&d_scene.lights,    sizeof(Light)    * scene.counts.lights))
            uploadLights();
        }

        if (scene.counts.cameras) {
            gpuErrchk(cudaMalloc(&d_scene.cameras,    sizeof(Camera)    * scene.counts.cameras))
            uploadCameras();
        }

        if (scene.counts.meshes) {
            u32 total_bvh_nodes = 0;
            for (u32 i = 0; i < scene.counts.meshes; i++) {
                total_triangles += scene.meshes[i].triangle_count;
                total_bvh_nodes += scene.meshes[i].bvh.node_count;
            }

            gpuErrchk(cudaMalloc(&d_scene.meshes,   sizeof(Mesh)     * scene.counts.meshes))
            gpuErrchk(cudaMalloc(&d_triangles,      sizeof(Triangle) * total_triangles))
            gpuErrchk(cudaMalloc(&d_mesh_bvh_nodes, sizeof(BVHNode)  * total_bvh_nodes))

            Mesh d_mesh;
            Mesh *mesh = scene.meshes;
            Mesh *d_mehses = d_scene.meshes;
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
                    total_texel_quads_count += texture->width * mip->height;
            }
            gpuErrchk(cudaMalloc(&d_scene.textures, sizeof(Texture)    * scene.counts.textures))
            gpuErrchk(cudaMalloc(&d_texture_mips,   sizeof(TextureMip) * total_mip_count))
            gpuErrchk(cudaMalloc(&d_texel_quads,    sizeof(TexelQuad)  * total_texel_quads_count))

            TexelQuad *d_quads = d_texel_quads;
            TextureMip *d_mips = d_texture_mips;
            Texture *d_textures = d_scene.textures;
            Texture d_texture;
            texture = scene.textures;
            for (u32 i = 0; i < scene.counts.textures; i++, texture++) {
                d_texture = *texture;
                d_texture.mips = d_mips;
                uploadN(&d_texture, d_textures, 1)
                d_textures++;

                for (u32 m = 0; m < texture->mip_count; m++) {
                    TextureMip mip = texture->mips[m];
                    u32 quad_count = mip.width * mip.height;
                    uploadN( mip.texel_quads, d_quads, quad_count)

                    mip.texel_quads = d_quads;
                    uploadN(&mip, d_mips, 1)
                    d_quads += quad_count;
                    d_mips++;
                }
            }
        }

        uploadN(&d_scene, d_scene_ptr, 1)
    }

    void uploadGeometries() { if (scene.counts.geometries) uploadN(scene.geometries, d_scene.geometries, scene.counts.geometries) }
    void uploadMaterials()  { if (scene.counts.materials)  uploadN(scene.materials,  d_scene.materials,  scene.counts.materials) }
    void uploadCameras()    { if (scene.counts.cameras)    uploadN(scene.cameras,    d_scene.cameras,    scene.counts.cameras) }
    void uploadLights()     { if (scene.counts.lights)     uploadN(scene.lights,     d_scene.lights,     scene.counts.lights) }
    void uploadSceneBVH()   {
        if (scene.bvh.node_count   ) uploadN(scene.bvh.nodes,                 d_scene.bvh.nodes,                 scene.bvh.node_count)
        if (scene.counts.geometries) uploadN(scene.bvh_leaf_geometry_indices, d_scene.bvh_leaf_geometry_indices, scene.counts.geometries)
    }
#else
    void initDataOnGPU() {}
    void uploadLights() {}
    void uploadCameras() {}
    void uploadGeometries() {}
    void uploadMaterials() {}
    void uploadSceneBVH() {}
    void renderOnGPU(const Canvas &canvas) { renderOnCPU(canvas); }
#endif
};