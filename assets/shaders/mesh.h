
#ifndef HELIX_GLSL_MESH_H
#define HELIX_GLSL_MESH_H

uint DrawFlags_AlphaMask    = 1 << 0;
uint DrawFlags_DoubleSided  = 1 << 1;
uint DrawFlags_Transparent  = 1 << 2;
uint DrawFlags_Phong        = 1 << 3;
uint DrawFlags_HasNormals   = 1 << 4;
uint DrawFlags_TexCoords    = 1 << 5;
uint DrawFlags_HasTangents  = 1 << 6;
uint DrawFlags_HasJoints    = 1 << 7;
uint DrawFlags_HasWeights   = 1 << 8;
uint DrawFlags_AlphaDither  = 1 << 9;

struct MeshInstanceDraw {
    mat4        model;
    mat4        model_inverse;

    uint        mesh_draw_index;
    uint        pad000;
    uint        pad001;
    uint        pad002;
};

struct MaterialData{
    uvec4       textures; // base_color , roughness, normal, occlusion

    vec4        base_color_factor;

    vec4        roughness_metallic_occlusion_factor;

    uint        flags;
    float       alpha_cutoff;
    uint        padding[2];
};

struct MeshData{
    uint        vertex_offset;
    uint        meshlet_offset;
    uint        meshlet_count;
    uint        padding0_;
};

#if NVIDIA
struct MeshDrawCommand
{
    uint        drawId;

    // VkDrawIndexedIndirectCommand
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    uint        vertexOffset;
    uint        firstInstance;
    // VkDrawMeshTasksIndirectCommandNV
    uint        taskCount;
    uint        firstTask;
};
#else
struct MeshDrawCommand
{
    uint        drawId;
    uint        firstTask;      

    // VkDrawIndexedIndirectCommand
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    uint        vertexOffset;
    uint        firstInstance;
        // VkDrawMeshTasksIndirectCommandEXT
    uint        x;
    uint        y;
    uint        z;
    uint        padding[2];
};

#endif // NVIDIA

//layout ( std430, set = MATERIAL_SET, binding = 2 ) readonly buffer MeshDraws {
//
//    MeshDraw    mesh_draws[];
//};

layout ( std430, set = MATERIAL_SET, binding = 2 ) readonly buffer MaterialDataBuffer {

    MaterialData    material_data[];
};

layout ( std430, set = MATERIAL_SET, binding = 3 ) readonly buffer MeshDataBuffer {

    MeshData    mesh_data[];
};

layout ( std430, set = MATERIAL_SET, binding = 10 ) readonly buffer MeshInstanceDraws {

    MeshInstanceDraw mesh_instance_draws[];
};

layout ( std430, set = MATERIAL_SET, binding = 12 ) readonly buffer MeshBounds {

    vec4        mesh_bounds[];
};

#endif // HELIX_GLSL_MESH_H
