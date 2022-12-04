


__global__ void d_render(ProjectionPlane projection_plane, enum RenderMode mode, vec3 camera_position, quat camera_rotation, Trace trace,
                         u16 width,
                         u32 pixel_count,

                         Scene scene,
                         u32        *scene_bvh_leaf_ids,
                         BVHNode    *scene_bvh_nodes,
                         BVHNode    *mesh_bvh_nodes,
                         Mesh       *meshes,
                         Triangle   *mesh_triangles,
                         Light *lights,
                         AreaLight  *area_lights,
                         Material   *materials,
                         Texture *textures,
                         TextureMip *texture_mips,
                         TexelQuad *texel_quads,
                         Geometry  *geometries,

                         u32       *mesh_bvh_leaf_ids,
                         const u32 *mesh_bvh_node_counts,
                         const u32 *mesh_triangle_counts
) {
    u32 i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= pixel_count)
        return;

    Pixel *pixel = d_pixels + i;
    pixel.color = Color(Black);
    pixel.opacity = 1;
    pixel.depth = INFINITY;

    u16 x = i % width;
    u16 y = i / width;

    Ray ray;
    ray.origin = camera_position;
    ray.direction = normVec3(scaleAddVec3(projection_plane.down, y, scaleAddVec3(projection_plane.right, x, projection_plane.start)));

    scene.lights = lights;
    scene.area_lights  = area_lights;
    scene.materials    = materials;
    scene.textures     = textures;
    scene.geometries   = geometries;
    scene.meshes       = meshes;
    scene.bvh.nodes    = scene_bvh_nodes;
    scene.bvh.leaf_ids = scene_bvh_leaf_ids;

    u32 scene_stack[6], mesh_stack[5];
    trace.mesh_stack  = mesh_stack;
    trace.scene_stack = scene_stack;

    Mesh *mesh = meshes;
    u32 nodes_offset = 0;
    u32 triangles_offset = 0;
    for (u32 m = 0; m < scene.settings.meshes; m++, mesh++) {
        mesh.bvh.node_count = mesh_bvh_node_counts[m];
        mesh.triangles      = mesh_triangles + triangles_offset;
        mesh.bvh.leaf_ids   = mesh_bvh_leaf_ids + triangles_offset;
        mesh.bvh.nodes      = mesh_bvh_nodes + nodes_offset;

        nodes_offset        += mesh.bvh.node_count;
        triangles_offset    += mesh.triangle_count;
    }

    TextureMip *mip = texture_mips;
    TexelQuad *quads = texel_quads;
    Texture *texture = textures;
    for (u32 t = 0; t < scene.settings.textures; t++, texture++) {
        texture.mips = mip;
        for (u32 m = 0; m < texture.mip_count; m++, mip++) {
            mip.texel_quads = quads;
            quads += mip.width * mip.height;
        }
    }

    ray.direction_reciprocal = oneOverVec3(ray.direction);
    trace.closest_hit.distance = trace.closest_hit.distance_squared = INFINITY;
    trace.closest_hit.cone_angle = projection_plane.cone_angle;
    trace.closest_hit.cone_width = 0;

    rayTrace(&ray, &trace, &scene, mode, pixel, x, y, camera_position, camera_rotation);
}