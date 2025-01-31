#pragma once

#include "Core/Gltf.hpp"
#include "Renderer/AsynchronousLoader.hpp"
#include "Renderer/FrameGraph.hpp"
#include "Renderer/GPUProfiler.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/HelixImgui.hpp"
#include "Renderer/Node.hpp"
#include "Renderer/Renderer.hpp"
#include "vendor/enkiTS/TaskScheduler.h"

namespace Helix {
static const u16 INVALID_TEXTURE_INDEX = 0xffff;

static const u32 k_material_descriptor_set_index = 1;
static const u32 k_max_depth_pyramid_levels = 16;

struct glTFScene;

struct LightUniform {
  glm::mat4 model;
  glm::mat4 view_projection;
  glm::vec4 camera_position_texture_index;
};

enum DrawFlags {
  DrawFlags_AlphaMask = 1 << 0,
  DrawFlags_DoubleSided = 1 << 1,
  DrawFlags_Transparent = 1 << 2,
  DrawFlags_Phong = 1 << 3,
  DrawFlags_HasNormals = 1 << 4,
  DrawFlags_HasTexCoords = 1 << 5,
  DrawFlags_HasTangents = 1 << 6,
  DrawFlags_HasJoints = 1 << 7,
  DrawFlags_HasWeights = 1 << 8,
  DrawFlags_AlphaDither = 1 << 9,
  DrawFlags_Cloth = 1 << 10,
};  // enum DrawFlags

struct PBRMaterial {
  Material* material;

  BufferHandle material_buffer;
  DescriptorSetHandle descriptor_set;

  u16 diffuse_texture_index;
  u16 roughness_texture_index;
  u16 normal_texture_index;
  u16 occlusion_texture_index;

  glm::vec4 base_color_factor;
  glm::vec4 roughness_metallic_occlusion_factor;

  f32 alpha_cutoff;
  u32 flags;
};

struct Mesh {
  PBRMaterial pbr_material;

  BufferHandle index_buffer;
  VkIndexType index_type;
  u32 index_offset;

  BufferHandle position_buffer;
  BufferHandle tangent_buffer;
  BufferHandle normal_buffer;
  BufferHandle texcoord_buffer;

  u32 position_offset;
  u32 tangent_offset;
  u32 normal_offset;
  u32 texcoord_offset;

  u32 primitive_count;
  u32 node_index;

  u32 meshlet_offset;
  u32 meshlet_count;

  // u32                 gpu_mesh_index = u32_max;

  glm::vec4 bounding_sphere;

  bool is_transparent() const {
    return (pbr_material.flags &
            (DrawFlags_AlphaMask | DrawFlags_Transparent)) != 0;
  }
  bool is_double_sided() const {
    return (pbr_material.flags & DrawFlags_DoubleSided) ==
           DrawFlags_DoubleSided;
  }
};  // struct Mesh

struct MeshInstance {
  Mesh* mesh;
  u32 material_pass_index;
};  // struct MeshInstance

struct MeshData {
  glm::mat4 model;

  glm::mat4 inverse_model;

  u32 textures[4];  // diffuse, roughness, normal, occlusion

  glm::vec4 base_color_factor;

  glm::vec4 roughness_metallic_occlusion_factor;

  f32 alpha_cutoff;
  u32 flags;
  f32 padding_[2];
};  // struct MeshData

// Gpu Data Structs
// /////////////////////////////////////////////////////////////////////////
struct alignas(16) GPUMeshDrawCounts {
  u32 opaque_mesh_visible_count;
  u32 opaque_mesh_culled_count;
  u32 transparent_mesh_visible_count;
  u32 transparent_mesh_culled_count;

  u32 total_count;
  u32 total_opaque_mesh_count;
  u32 depth_pyramid_texture_index;
  u32 late_flag;
};  // struct GPUMeshDrawCounts Draw count buffer used in indirect draw calls
#if NVIDIA
struct alignas(16) GPUMeshDrawCommand {
  u32 mesh_index;
  VkDrawIndexedIndirectCommand indirect;        // 5 uint32_t
  VkDrawMeshTasksIndirectCommandNV indirectMS;  // 2 uint32_t
};
#else
struct alignas(16) GPUMeshDrawCommand {
  u32 mesh_index;
  u32 firstTask;
  VkDrawIndexedIndirectCommand indirect;         // 5 uint32_t
  VkDrawMeshTasksIndirectCommandEXT indirectMS;  // 3 uint32_t
  u32 padding[2];
};

#endif  // NVIDIA

struct GPUDebugIcon {
  glm::vec4
      position_texture_index[5];  // x,y,z for position, w for texture index
  u32 count;  // TODO: Maybe store the texture index as a u16 and the count as a
              // u16
};

// Contains the data for an instance of a single GPUMeshData
struct alignas(16) GPUMeshInstanceData {
  glm::mat4 world;
  glm::mat4 inverse_world;

  u32 mesh_index;
  u32 pad000;
  u32 pad001;
  u32 pad002;
};  // struct GpuMeshInstanceData

struct alignas(16) GPULight {
  glm::vec4 position;  // w contains the light index

  f32 range = 100.f;
  f32 intensity = 100.f;
  f32 padding_[2];
};
// Data used by the fragment shader to colour the geometry
struct alignas(16) GPUMaterialData {
  u32 textures[4];  // base_color , roughness, normal, occlusion
  // PBR
  glm::vec4 base_color_factor;

  glm::vec4
      roughness_metallic_occlusion_factor;  // metallic, roughness, occlusion

  u32 flags;
  f32 alpha_cutoff;
  u32 padding[2];
};  // struct GPUMaterialData

// Data used by the mesh/vertex shader to draw the geometry
struct alignas(16) GPUMeshData {
  u32 vertex_offset;
  u32 meshlet_offset;
  u32 meshlet_count;
  u32 padding0_;
};  // struct GPUMeshData

struct alignas(16) GPUMeshlet {
  glm::vec3 center;
  f32 radius;

  i8 cone_axis[3];
  i8 cone_cutoff;

  u32 data_offset;
  u32 mesh_index;
  u8 vertex_count;
  u8 triangle_count;
};  // struct GPUMeshlet

struct GPUMeshletVertexPosition {
  f32 position[3];
  f32 padding;
};  // struct GPUMeshletVertexPosition

struct GPUMeshletVertexData {
  u8 normal[4];
  u8 tangent[4];
  u16 uv_coords[2];
  f32 padding;
};  // struct GPUMeshletVertexData

struct GPUSceneData {
  glm::mat4 view_projection;
  glm::mat4 view_projection_debug;
  glm::mat4 inverse_view_projection;
  glm::mat4 view_matrix;
  glm::mat4 view_matrix_debug;
  glm::mat4 previous_view_projection;

  glm::vec4 camera_position;
  glm::vec4 camera_position_debug;
  glm::vec4 light_position;

  f32 current_light_count;
  f32 light_intensity;
  u32 dither_texture_index;
  f32 z_near;

  f32 z_far;
  f32 projection_00;
  f32 projection_11;
  u32 frustum_cull_meshes;

  u32 frustum_cull_meshlets;
  u32 occlusion_cull_meshes;
  u32 occlusion_cull_meshlets;
  u32 freeze_occlusion_camera;

  f32 resolution_x;
  f32 resolution_y;
  f32 aspect_ratio;
  f32 pad0001;

  glm::vec4 frustum_planes[6];

};  // struct GPUSceneData

// Gpu Data Structs
// /////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////
struct Scene {
  virtual void init(Renderer* renderer, Allocator* resident_allocator,
                    FrameGraph* frame_graph, StackAllocator* stack_allocator,
                    AsynchronousLoader* async_loader) {};
  virtual void load(cstring filename, cstring path,
                    Allocator* resident_allocator,
                    StackAllocator* temp_allocator,
                    AsynchronousLoader* async_loader) {};
  virtual void free_gpu_resources(Renderer* renderer) {};
  virtual void unload(Renderer* renderer) {};

  virtual void register_render_passes(FrameGraph* frame_graph) {};
  virtual void prepare_draws(Renderer* renderer,
                             StackAllocator* stack_allocator) {};

  virtual void fill_gpu_data_buffers(f32 model_scale) {};
  virtual void submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler,
                                enki::TaskScheduler* task_scheduler) {};

  Array<GPUMeshlet> meshlets;
  Array<GPUMeshletVertexPosition> meshlets_vertex_positions;
  Array<GPUMeshletVertexData> meshlets_vertex_data;
  Array<u32> meshlet_vertex_and_index_indices;
  Array<GPULight> lights;

  // Gpu buffers
  BufferHandle material_data_buffer =
      k_invalid_buffer;  // Contains the material data for opaque meshes and
                         // transparent meshes
  BufferHandle mesh_data_buffer = k_invalid_buffer;
  BufferHandle mesh_instances_buffer = k_invalid_buffer;
  BufferHandle mesh_bounds_buffer = k_invalid_buffer;
  BufferHandle scene_constant_buffer = k_invalid_buffer;
  BufferHandle meshlets_buffer = k_invalid_buffer;
  BufferHandle meshlet_vertex_and_index_indices_buffer = k_invalid_buffer;
  BufferHandle meshlets_vertex_pos_buffer = k_invalid_buffer;
  BufferHandle meshlets_vertex_data_buffer = k_invalid_buffer;
  BufferHandle light_data_buffer = k_invalid_buffer;

  // Indirect data
  BufferHandle mesh_draw_count_buffers[k_max_frames];
  BufferHandle mesh_indirect_draw_early_command_buffers[k_max_frames];
  BufferHandle mesh_indirect_draw_late_command_buffers[k_max_frames];

  // Gpu debug draw
  BufferHandle debug_line_buffer = k_invalid_buffer;
  BufferHandle debug_line_count_buffer = k_invalid_buffer;
  BufferHandle debug_line_indirect_command_buffer = k_invalid_buffer;

  GPUSceneData scene_data;

  DescriptorSetHandle mesh_shader_descriptor_set[k_max_frames];

  GPUMeshDrawCounts mesh_draw_counts;

  Renderer* renderer = nullptr;
};  // struct Scene

//
//
struct MeshEarlyCullingPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void prepare_draws(Scene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  Renderer* renderer = nullptr;

  PipelineHandle frustum_cull_pipeline;
  DescriptorSetHandle frustum_cull_descriptor_set[k_max_frames];
  SamplerHandle depth_pyramid_sampler;
  u32 depth_pyramid_texture_index;

};  // struct MeshEarlyCullingPass

//
//
struct MeshLateCullingPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void prepare_draws(Scene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  Renderer* renderer = nullptr;

  PipelineHandle frustum_cull_pipeline;
  DescriptorSetHandle frustum_cull_descriptor_set[k_max_frames];
  SamplerHandle depth_pyramid_sampler;
  u32 depth_pyramid_texture_index;

};  // struct MeshLateCullingPass

//
//
struct DepthPrePass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  // Array<MeshInstance> mesh_instances{ };
  u32 mesh_count;
  Mesh* meshes;
  Renderer* renderer;
};  // struct DepthPrePass

//
//
struct GBufferEarlyPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  u32 double_sided_mesh_count;
  u32 mesh_count;
  Mesh* meshes;
  Renderer* renderer;
  u32 meshlet_program_index;
};  // struct GBufferEarlyPass

//
//
struct GBufferLatePass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  u32 double_sided_mesh_count;
  u32 mesh_count;
  Mesh* meshes;
  Renderer* renderer;
  u32 meshlet_program_index;
};  // struct GBufferLatePass

//
//
struct DepthPyramidPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;
  void on_resize(GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width,
                 u32 new_height) override;
  void post_render(u32 current_frame_index, CommandBuffer* gpu_commands,
                   FrameGraph* frame_graph) override;

  void prepare_draws(Scene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  void create_depth_pyramid_resource(Texture* depth_texture);

  Renderer* renderer;

  PipelineHandle depth_pyramid_pipeline;
  TextureHandle depth_pyramid;
  SamplerHandle depth_pyramid_sampler;
  TextureHandle depth_pyramid_views[k_max_depth_pyramid_levels];
  DescriptorSetHandle
      depth_hierarchy_descriptor_set[k_max_depth_pyramid_levels];

  u32 depth_pyramid_levels = 0;

  bool update_depth_pyramid;
};  // struct DepthPrePass

//
//
struct LightPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void fill_gpu_material_buffer();
  void free_gpu_resources();

  DescriptorSetHandle d_set;
  PipelineHandle pipeline_handle;
  Renderer* renderer;
  bool use_compute;

  struct LightingData {
    u32 gbuffer_color_index;
    u32 gbuffer_rmo_index;
    u32 gbuffer_normal_index;
    u32 depth_texture_index;
  };
  LightingData lighting_data;
};  // struct LightPass

//
//
struct TransparentPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void free_gpu_resources();

  u32 double_sided_mesh_count;
  u32 mesh_count;
  Mesh* meshes;
  Renderer* renderer;
  u32 meshlet_program_index;
};  // struct TransparentPass

//
//
struct DebugPass : public FrameGraphRenderPass {
  void render(CommandBuffer* gpu_commands, Scene* scene) override;

  void init();
  void prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                     Allocator* resident_allocator);
  void upload_materials() {};
  void free_gpu_resources();

  PipelineHandle debug_light_pipeline;
  DescriptorSetHandle debug_light_dset;
  BufferHandle debug_icons_buffer;
  Renderer* renderer;
};  // struct DebugPass

struct glTFScene : public Scene {
  void init(Renderer* renderer, Allocator* resident_allocator,
            FrameGraph* frame_graph, StackAllocator* stack_allocator,
            AsynchronousLoader* async_loader) override;
  void load(cstring filename, cstring path, Allocator* resident_allocator,
            StackAllocator* temp_allocator,
            AsynchronousLoader* async_loader) override;
  void free_gpu_resources(Renderer* renderer) override;
  void unload(Renderer* renderer) override;

  void register_render_passes(FrameGraph* frame_graph) override;
  void prepare_draws(Renderer* renderer,
                     StackAllocator* stack_allocator) override;
  void fill_pbr_material(Renderer& renderer, glTF::Material& material,
                         PBRMaterial& pbr_material);
  u16 get_material_texture(GpuDevice& gpu, glTF::TextureInfo* texture_info);
  u16 get_material_texture(GpuDevice& gpu, i32 gltf_texture_index);

  void fill_gpu_data_buffers(f32 model_scale) override;
  void submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler,
                        enki::TaskScheduler* task_scheduler) override;

  void draw_mesh(CommandBuffer* gpu_commands, Mesh& mesh);

  void destroy_node(NodeHandle handle);

  void imgui_draw_node(NodeHandle node_handle);
  void imgui_draw_hierarchy();

  void imgui_draw_node_property(NodeHandle node_handle);

  void add_light();

  Array<Mesh> opaque_meshes;
  Array<Mesh> transparent_meshes;

  // All graphics resources used by the scene
  Array<TextureResource> images;  // TODO: Maybe just store the pool index
                                  // rather than the whole Texture resource
  Array<SamplerResource> samplers;
  Array<BufferResource> buffers;

  glm::vec4 tester = glm::vec4(1.0f);  // TODO: Remove

  u32 current_images_count = 0;
  u32 current_buffers_count = 0;
  u32 current_samplers_count = 0;

  glTF::glTF gltf_scene;  // Source gltf scene

  NodePool node_pool;

  // DepthPrePass          depth_pre_pass;
  MeshEarlyCullingPass mesh_cull_pass;
  MeshLateCullingPass mesh_cull_late_pass;
  GBufferEarlyPass gbuffer_pass;
  GBufferLatePass gbuffer_late_pass;
  DepthPyramidPass depth_pyramid_pass;
  LightPass light_pass;
  TransparentPass transparent_pass;
  DebugPass debug_pass;

  // Fullscreen data
  Program* fullscreen_program = nullptr;
  DescriptorSetHandle fullscreen_ds;
  u32 fullscreen_texture_index = u32_max;

  NodeHandle current_node{};

  FrameGraph* frame_graph;
  StackAllocator* scratch_allocator;
  Allocator* main_allocator;
  AsynchronousLoader* loader;
  Material* pbr_material;
  StringBuffer names;

  // Buffers
  TextureResource light_texture;

};  // struct GltfScene

struct glTFDrawTask : public enki::ITaskSet {
  GpuDevice* gpu = nullptr;
  FrameGraph* frame_graph = nullptr;
  Renderer* renderer = nullptr;
  ImGuiService* imgui = nullptr;
  GPUProfiler* gpu_profiler = nullptr;
  glTFScene* scene = nullptr;
  u32 thread_id = 0;

  void init(GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_,
            ImGuiService* imgui_, GPUProfiler* gpu_profiler_,
            glTFScene* scene_);

  void ExecuteRange(enki::TaskSetPartition range_, u32 threadnum_) override;

};  // struct DrawTask

}  // namespace Helix
