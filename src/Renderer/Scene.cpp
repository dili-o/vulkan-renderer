#include "Scene.hpp"

#include <imgui/imgui.h>
#include <vendor/imgui/stb_image.h>
#include <vendor/meshoptimizer/meshoptimizer.h>

#include <Core/Numerics.hpp>
#include <vendor/tracy/tracy/Tracy.hpp>

#include "Core/File.hpp"
#include "Core/Gltf.hpp"
#include "Core/Time.hpp"
#include "Renderer/GPUEnum.hpp"

const u32 MAX_LIGHTS = 256;

namespace Helix {
float square(float r) { return r * r; }

glm::vec4 project_sphere(glm::vec3 C, float r, float znear, float P00,
                         float P11, glm::mat4 P) {
  if (C.z + r > -znear) return glm::vec4(0, 0, 0, 0);

  glm::vec4 aabb;

  glm::vec4 min_p = glm::vec4(C.x - r, C.y - r, C.z, 1.0f);
  glm::vec4 max_p = glm::vec4(C.x + r, C.y + r, C.z, 1.0f);

  // aabb = glm::vec4(min_clip.x / min_clip.w, min_clip.y / min_clip.w,
  // max_clip.x / max_clip.w, max_clip.y / max_clip.w);

  // aabb = glm::vec4(aabb.x + 1.0f, 1.0f - aabb.y, aabb.z + 1.0f, 1.0f -
  // aabb.w) * 0.5f;

  glm::vec2 cx = glm::vec2(C.x, -C.z);
  glm::vec2 vx = glm::vec2(sqrt(dot(cx, cx) - r * r), r);
  glm::vec2 minx = glm::mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
  glm::vec2 maxx = glm::mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

  glm::vec2 cy = glm::vec2(-C.y, -C.z);
  glm::vec2 vy = glm::vec2(sqrt(dot(cy, cy) - r * r), r);
  glm::vec2 miny = glm::mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
  glm::vec2 maxy = glm::mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

  aabb = glm::vec4(minx.x / minx.y * P00, miny.x / miny.y * P11,
                   maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
  aabb = aabb * glm::vec4(0.5f, -0.5f, 0.5f, -0.5f) +
         glm::vec4(0.5f);  // clip space -> uv space

  return aabb;
}

// Helper functions //////////////////////////////////////////////////

// Light
// Helix::PipelineHandle                  light_pipeline;

void get_mesh_vertex_buffer(glTFScene& scene, i32 accessor_index,
                            BufferHandle& out_buffer_handle,
                            u32& out_buffer_offset) {
  if (accessor_index != -1) {
    glTF::Accessor& buffer_accessor =
        scene.gltf_scene.accessors[accessor_index];
    glTF::BufferView& buffer_view =
        scene.gltf_scene.buffer_views[buffer_accessor.buffer_view];
    BufferResource& buffer_gpu =
        scene
            .buffers[buffer_accessor.buffer_view + scene.current_buffers_count];

    out_buffer_handle = buffer_gpu.handle;
    out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE
                            ? 0
                            : buffer_accessor.byte_offset;
  } else {
    // TODO: Right now if a glTF model doesn't have a vertex buffer we just bind
    // the 0 index buffer
    out_buffer_handle.index = 0;
  }
}

int gltf_mesh_material_compare(const void* a, const void* b) {
  const Mesh* mesh_a = (const Mesh*)a;
  const Mesh* mesh_b = (const Mesh*)b;

  if (mesh_a->pbr_material.material->render_index <
      mesh_b->pbr_material.material->render_index)
    return -1;
  if (mesh_a->pbr_material.material->render_index >
      mesh_b->pbr_material.material->render_index)
    return 1;
  return 0;
}

int gltf_mesh_doublesided_compare(const void* a, const void* b) {
  const Mesh* mesh_a = static_cast<const Mesh*>(a);
  const Mesh* mesh_b = static_cast<const Mesh*>(b);

  // Sort double-sided meshes first.
  if (mesh_a->is_double_sided() && !mesh_b->is_double_sided()) {
    return -1;  // meshA comes before meshB
  }
  if (!mesh_a->is_double_sided() && mesh_b->is_double_sided()) {
    return 1;  // meshB comes before meshA
  }
  return 0;  // both are the same (both double-sided or both not double-sided)
}

static void copy_gpu_material_data(GPUMaterialData& gpu_material_data,
                                   const Mesh& mesh) {
  gpu_material_data.textures[0] = mesh.pbr_material.diffuse_texture_index;
  gpu_material_data.textures[1] = mesh.pbr_material.roughness_texture_index;
  gpu_material_data.textures[2] = mesh.pbr_material.normal_texture_index;
  gpu_material_data.textures[3] = mesh.pbr_material.occlusion_texture_index;

  gpu_material_data.base_color_factor = mesh.pbr_material.base_color_factor;
  gpu_material_data.roughness_metallic_occlusion_factor =
      mesh.pbr_material.roughness_metallic_occlusion_factor;
  gpu_material_data.alpha_cutoff = mesh.pbr_material.alpha_cutoff;

  gpu_material_data.flags = mesh.pbr_material.flags;
}
//
//
static void copy_gpu_mesh_data(GPUMeshData& gpu_mesh_data, const Mesh& mesh) {
  gpu_mesh_data.meshlet_offset = mesh.meshlet_offset;
  gpu_mesh_data.meshlet_count = mesh.meshlet_count;
}
//
//
static FrameGraphResource* get_output_texture(FrameGraph* frame_graph,
                                              FrameGraphResourceHandle input) {
  FrameGraphResource* input_resource = frame_graph->access_resource(input);

  FrameGraphResource* output_resource =
      frame_graph->access_resource(input_resource->output_handle);
  HASSERT(output_resource != nullptr);

  return output_resource;
}
//
//
static void copy_gpu_mesh_matrix(MeshData& gpu_mesh_data, const Mesh& mesh,
                                 const f32 global_scale,
                                 const ResourcePool* mesh_nodes) {
  // Apply global scale matrix
  glm::mat4 scale_mat = glm::scale(
      glm::mat4(1.0f), glm::vec3(global_scale, global_scale, global_scale));
  MeshNode* mesh_node = (MeshNode*)mesh_nodes->access_resource(mesh.node_index);
  gpu_mesh_data.model =
      mesh_node->world_transform.calculate_matrix() * scale_mat;
  gpu_mesh_data.inverse_model =
      glm::inverse(glm::transpose(gpu_mesh_data.model));
}
//
// MeshEarlyCullingPass
// /////////////////////////////////////////////////////////
void MeshEarlyCullingPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  if (!enabled) return;

  glTFScene* scene = (glTFScene*)scene_;

  Renderer* renderer = scene->renderer;

  // Frustum cull meshes
  GPUMeshDrawCounts& mesh_draw_counts = scene->mesh_draw_counts;
  mesh_draw_counts.opaque_mesh_visible_count = 0;
  mesh_draw_counts.opaque_mesh_culled_count = 0;
  mesh_draw_counts.transparent_mesh_visible_count = 0;
  mesh_draw_counts.transparent_mesh_culled_count = 0;

  mesh_draw_counts.total_count =
      scene->opaque_meshes.size + scene->transparent_meshes.size;
  mesh_draw_counts.total_opaque_mesh_count = scene->opaque_meshes.size;
  mesh_draw_counts.depth_pyramid_texture_index = depth_pyramid_texture_index;
  mesh_draw_counts.late_flag = 0;

  u32 buffer_frame_index = renderer->gpu->current_frame;
  // Reset mesh draw counts
  MapBufferParameters cb_map{scene->mesh_draw_count_buffers[buffer_frame_index],
                             0, 0};
  GPUMeshDrawCounts* count_data =
      (GPUMeshDrawCounts*)renderer->gpu->map_buffer(cb_map);
  if (count_data) {
    *count_data = mesh_draw_counts;

    renderer->gpu->unmap_buffer(cb_map);
  }

  // Reset debug draw counts
  cb_map.buffer = scene->debug_line_count_buffer;
  u32* debug_line_count = (u32*)renderer->gpu->map_buffer(cb_map);
  if (debug_line_count) {
    debug_line_count[0] = 0;
    debug_line_count[1] = 0;
    debug_line_count[2] = renderer->gpu->current_frame;
    debug_line_count[3] = 0;

    renderer->gpu->unmap_buffer(cb_map);
  }

  gpu_commands->bind_pipeline(frustum_cull_pipeline);

  const Buffer* indirect_draw_commands_sb = renderer->gpu->access_buffer(
      scene->mesh_indirect_draw_early_command_buffers[buffer_frame_index]);
  util_add_buffer_barrier(
      renderer->gpu, gpu_commands->vk_handle,
      indirect_draw_commands_sb->vk_handle, RESOURCE_STATE_INDIRECT_ARGUMENT,
      RESOURCE_STATE_UNORDERED_ACCESS, indirect_draw_commands_sb->size);

  const Buffer* count_sb = renderer->gpu->access_buffer(
      scene->mesh_draw_count_buffers[buffer_frame_index]);
  util_add_buffer_barrier(renderer->gpu, gpu_commands->vk_handle,
                          count_sb->vk_handle, RESOURCE_STATE_INDIRECT_ARGUMENT,
                          RESOURCE_STATE_UNORDERED_ACCESS, count_sb->size);

  gpu_commands->bind_descriptor_set(
      &frustum_cull_descriptor_set[buffer_frame_index], 1, nullptr, 0);

  const Pipeline* pipeline =
      renderer->gpu->access_pipeline(frustum_cull_pipeline);

  u32 group_x = Helix::ceilu32(scene->mesh_draw_counts.total_count /
                               (f32)pipeline->local_size[0]);
  gpu_commands->dispatch(group_x, 1, 1);

  util_add_buffer_barrier(
      renderer->gpu, gpu_commands->vk_handle,
      indirect_draw_commands_sb->vk_handle, RESOURCE_STATE_UNORDERED_ACCESS,
      RESOURCE_STATE_INDIRECT_ARGUMENT, indirect_draw_commands_sb->size);

  util_add_buffer_barrier(renderer->gpu, gpu_commands->vk_handle,
                          count_sb->vk_handle, RESOURCE_STATE_UNORDERED_ACCESS,
                          RESOURCE_STATE_INDIRECT_ARGUMENT, count_sb->size);
}

void MeshEarlyCullingPass::prepare_draws(Scene& scene, FrameGraph* frame_graph,
                                         Allocator* resident_allocator) {
  FrameGraphNode* node = frame_graph->get_node("mesh_cull_early_pass");
  if (node == nullptr) {
    enabled = false;

    return;
  }
  enabled = node->enabled;

  renderer = scene.renderer;
  GpuDevice& gpu = *renderer->gpu;

  // Cache frustum cull shader
  Program* culling_program =
      renderer->resource_cache.programs.get(hash_calculate("culling"));
  {
    u32 pipeline_index = culling_program->get_pass_index("gpu_culling");
    frustum_cull_pipeline = culling_program->passes[pipeline_index].pipeline;
    DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout(
        frustum_cull_pipeline, k_material_descriptor_set_index);

    for (u32 i = 0; i < k_max_frames; ++i) {
      DescriptorSetCreation ds_creation{};
      ds_creation.buffer(scene.scene_constant_buffer, 0)
          .buffer(scene.mesh_indirect_draw_early_command_buffers[i], 1)
          .buffer(scene.material_data_buffer, 2)
          .buffer(scene.mesh_data_buffer, 3)
          .buffer(scene.mesh_indirect_draw_late_command_buffers[i], 4)
          .buffer(scene.mesh_instances_buffer, 10)
          .buffer(scene.mesh_draw_count_buffers[i], 11)
          .buffer(scene.mesh_bounds_buffer, 12)
          .buffer(scene.debug_line_buffer, 20)
          .buffer(scene.debug_line_count_buffer, 21)
          .buffer(scene.debug_line_indirect_command_buffer, 22)
          .set_layout(layout);

      frustum_cull_descriptor_set[i] = gpu.create_descriptor_set(ds_creation);
    }
  }
}

void MeshEarlyCullingPass::free_gpu_resources() {
  if (!renderer) return;
  GpuDevice& gpu = *renderer->gpu;

  for (u32 i = 0; i < k_max_frames; ++i) {
    gpu.destroy_descriptor_set(frustum_cull_descriptor_set[i]);
  }
}

//
// MeshLateCullingPass /////////////////////////////////////////////////////////
void MeshLateCullingPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  if (!enabled) return;

  glTFScene* scene = (glTFScene*)scene_;

  Renderer* renderer = scene->renderer;

  u32 buffer_frame_index = renderer->gpu->current_frame;

  // const Buffer* mesh_draw_count_buffer =
  // renderer->gpu->access_buffer(scene->mesh_draw_count_buffers[buffer_frame_index]);

  gpu_commands->bind_pipeline(frustum_cull_pipeline);

  const Buffer* indirect_draw_commands_sb = renderer->gpu->access_buffer(
      scene->mesh_indirect_draw_early_command_buffers[buffer_frame_index]);
  util_add_buffer_barrier(
      renderer->gpu, gpu_commands->vk_handle,
      indirect_draw_commands_sb->vk_handle, RESOURCE_STATE_INDIRECT_ARGUMENT,
      RESOURCE_STATE_UNORDERED_ACCESS, indirect_draw_commands_sb->size);

  const Buffer* count_sb = renderer->gpu->access_buffer(
      scene->mesh_draw_count_buffers[buffer_frame_index]);
  util_add_buffer_barrier(renderer->gpu, gpu_commands->vk_handle,
                          count_sb->vk_handle, RESOURCE_STATE_INDIRECT_ARGUMENT,
                          RESOURCE_STATE_UNORDERED_ACCESS, count_sb->size);

  // TODO: Right now setting this to 0 and updating the buffer does nothing
  // since it uses late_flag as the count buffer in the gbuffer_late pass.
  // scene->mesh_draw_counts.opaque_mesh_visible_count = 0;
  //// TODO: Make an API wrapper around this
  // vkCmdUpdateBuffer(
  //     gpu_commands->vk_handle,
  //     count_sb->vk_handle,
  //     offsetof(GPUMeshDrawCounts, opaque_mesh_visible_count),
  //     sizeof(u32),
  //     &scene->mesh_draw_counts.opaque_mesh_visible_count);

  gpu_commands->bind_descriptor_set(
      &frustum_cull_descriptor_set[buffer_frame_index], 1, nullptr, 0);

  u32 group_x = Helix::ceilu32(scene->mesh_draw_counts.total_count / 64.0f);
  gpu_commands->dispatch(group_x, 1, 1);

  util_add_buffer_barrier(
      renderer->gpu, gpu_commands->vk_handle,
      indirect_draw_commands_sb->vk_handle, RESOURCE_STATE_UNORDERED_ACCESS,
      RESOURCE_STATE_INDIRECT_ARGUMENT, indirect_draw_commands_sb->size);

  util_add_buffer_barrier(renderer->gpu, gpu_commands->vk_handle,
                          count_sb->vk_handle, RESOURCE_STATE_UNORDERED_ACCESS,
                          RESOURCE_STATE_INDIRECT_ARGUMENT, count_sb->size);
}

void MeshLateCullingPass::prepare_draws(Scene& scene, FrameGraph* frame_graph,
                                        Allocator* resident_allocator) {
  FrameGraphNode* node = frame_graph->get_node("mesh_cull_late_pass");
  if (node == nullptr) {
    enabled = false;

    return;
  }
  // node->enabled = false;

  enabled = node->enabled;

  renderer = scene.renderer;
  GpuDevice& gpu = *renderer->gpu;

  // Cache frustum cull shader
  Program* culling_program =
      renderer->resource_cache.programs.get(hash_calculate("culling"));
  {
    u32 pipeline_index = culling_program->get_pass_index("gpu_culling_late");
    frustum_cull_pipeline = culling_program->passes[pipeline_index].pipeline;
    DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout(
        frustum_cull_pipeline, k_material_descriptor_set_index);

    for (u32 i = 0; i < k_max_frames; ++i) {
      DescriptorSetCreation ds_creation{};
      ds_creation.buffer(scene.scene_constant_buffer, 0)
          .buffer(scene.mesh_indirect_draw_early_command_buffers[i], 1)
          .buffer(scene.material_data_buffer, 2)
          .buffer(scene.mesh_data_buffer, 3)
          .buffer(scene.mesh_indirect_draw_late_command_buffers[i], 4)
          .buffer(scene.mesh_instances_buffer, 10)
          .buffer(scene.mesh_draw_count_buffers[i], 11)
          .buffer(scene.mesh_bounds_buffer, 12)
          .buffer(scene.debug_line_buffer, 20)
          .buffer(scene.debug_line_count_buffer, 21)
          .buffer(scene.debug_line_indirect_command_buffer, 22)
          .set_layout(layout);

      frustum_cull_descriptor_set[i] = gpu.create_descriptor_set(ds_creation);
    }
  }
}

void MeshLateCullingPass::free_gpu_resources() {
  if (!renderer) return;
  GpuDevice& gpu = *renderer->gpu;

  for (u32 i = 0; i < k_max_frames; ++i) {
    gpu.destroy_descriptor_set(frustum_cull_descriptor_set[i]);
  }
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPrePass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  glTFScene* scene = (glTFScene*)scene_;

  Material* last_material = nullptr;
  for (u32 mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
    // MeshInstance& mesh_instance = mesh_instances[mesh_index];
    Mesh& mesh = meshes[mesh_index];

    if (mesh.pbr_material.material != last_material) {
      PipelineHandle pipeline =
          renderer->get_pipeline(mesh.pbr_material.material, 0);

      gpu_commands->bind_pipeline(pipeline);

      last_material = mesh.pbr_material.material;
    }

    // scene->draw_mesh(gpu_commands, mesh);
  }
}

void DepthPrePass::init() {
  renderer = nullptr;
  // mesh_instances.size = 0;
  meshes = nullptr;
  mesh_count = 0;
}

void DepthPrePass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                                 Allocator* resident_allocator) {
  renderer = scene.renderer;

  FrameGraphNode* node = frame_graph->get_node("depth_pre_pass");
  HASSERT(node);

  // const u64 hashed_name = hash_calculate("geometry");
  // Program* geometry_program =
  // renderer->resource_cache.programs.get(hashed_name);

  // glTF::glTF& gltf_scene = scene.gltf_scene;

  // mesh_instances.init(resident_allocator, 16);

  // Copy all mesh draws and change only material.
  if (scene.opaque_meshes.size) {
    meshes = &scene.opaque_meshes[0];
    mesh_count = scene.opaque_meshes.size;
  }
}

void DepthPrePass::free_gpu_resources() {
  // mesh_instances.shutdown();
}

//
// GBufferEarlyPass ////////////////////////////////////////////////////////
void GBufferEarlyPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  if (!enabled) return;

  glTFScene* scene = (glTFScene*)scene_;

  Material* last_material = nullptr;

  Renderer* renderer = scene->renderer;

#if NVIDIA
  const u64 meshlet_hashed_name = hash_calculate("meshlet_nv");
#else
  const u64 meshlet_hashed_name = hash_calculate("meshlet_ext");
#endif  // NVIDIA
  Program* meshlet_program =
      renderer->resource_cache.programs.get(meshlet_hashed_name);

  PipelineHandle pipeline =
      meshlet_program->passes[meshlet_program_index].pipeline;

  gpu_commands->bind_pipeline(pipeline);

  u32 buffer_frame_index = renderer->gpu->current_frame;
  gpu_commands->bind_descriptor_set(
      &scene->mesh_shader_descriptor_set[buffer_frame_index], 1, nullptr, 0);

  gpu_commands->draw_mesh_task_indirect_count(
      scene->mesh_indirect_draw_early_command_buffers[buffer_frame_index],
      offsetof(GPUMeshDrawCommand, indirectMS),
      scene->mesh_draw_count_buffers[buffer_frame_index], 0,
      scene->opaque_meshes.size, sizeof(GPUMeshDrawCommand));
}

void GBufferEarlyPass::init() {
  renderer = nullptr;
  double_sided_mesh_count = 0;
  mesh_count = 0;
  meshes = nullptr;
}

void GBufferEarlyPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                                     Allocator* resident_allocator) {
  renderer = scene.renderer;

  enabled = true;

  FrameGraphNode* node = frame_graph->get_node("gbuffer_pass");
  HASSERT(node);

  if (scene.opaque_meshes.size) {
    meshes = &scene.opaque_meshes[0];
    mesh_count = scene.opaque_meshes.size;
  }

  if (renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER) {
#if NVIDIA
    Program* main_program =
        renderer->resource_cache.programs.get(hash_calculate("meshlet_nv"));
#else
    Program* main_program =
        renderer->resource_cache.programs.get(hash_calculate("meshlet_ext"));
#endif  // NVIDIA
    meshlet_program_index = main_program->get_pass_index("gbuffer_culling");
  }
}

void GBufferEarlyPass::free_gpu_resources() {
  // mesh_instances.shutdown();
}

//
// GBufferLatePass ////////////////////////////////////////////////////////
void GBufferLatePass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  if (!enabled) return;

  glTFScene* scene = (glTFScene*)scene_;

  Material* last_material = nullptr;

  Renderer* renderer = scene->renderer;

#if NVIDIA
  const u64 meshlet_hashed_name = hash_calculate("meshlet_nv");
#else
  const u64 meshlet_hashed_name = hash_calculate("meshlet_ext");
#endif  // NVIDIA
  Program* meshlet_program =
      renderer->resource_cache.programs.get(meshlet_hashed_name);

  PipelineHandle pipeline =
      meshlet_program->passes[meshlet_program_index].pipeline;

  gpu_commands->bind_pipeline(pipeline);

  u32 buffer_frame_index = renderer->gpu->current_frame;
  gpu_commands->bind_descriptor_set(
      &scene->mesh_shader_descriptor_set[buffer_frame_index], 1, nullptr, 0);

  gpu_commands->draw_mesh_task_indirect_count(
      scene->mesh_indirect_draw_late_command_buffers[buffer_frame_index],
      offsetof(GPUMeshDrawCommand, indirectMS),
      scene->mesh_draw_count_buffers[buffer_frame_index],
      offsetof(GPUMeshDrawCounts, late_flag), scene->opaque_meshes.size,
      sizeof(GPUMeshDrawCommand));
}

void GBufferLatePass::init() {
  renderer = nullptr;
  double_sided_mesh_count = 0;
  mesh_count = 0;
  meshes = nullptr;
}

void GBufferLatePass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                                    Allocator* resident_allocator) {
  renderer = scene.renderer;

  enabled = true;

  FrameGraphNode* node = frame_graph->get_node("gbuffer_late_pass");
  HASSERT(node);

  if (scene.opaque_meshes.size) {
    meshes = &scene.opaque_meshes[0];
    mesh_count = scene.opaque_meshes.size;
  }

  if (renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER) {
#if NVIDIA
    const u64 meshlet_hashed_name = hash_calculate("meshlet_nv");
#else
    const u64 meshlet_hashed_name = hash_calculate("meshlet_ext");
#endif  // NVIDIA
    Program* main_program =
        renderer->resource_cache.programs.get(meshlet_hashed_name);
    meshlet_program_index = main_program->get_pass_index("gbuffer_culling");
  }
}

void GBufferLatePass::free_gpu_resources() {
  // mesh_instances.shutdown();
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPyramidPass::render(CommandBuffer* gpu_commands, Scene* scene) {
  if (!enabled) return;

  update_depth_pyramid = (scene->scene_data.freeze_occlusion_camera == 0);
}

u32 previous_pow2(u32 value) {
  if (value == 0) return 0;
#if defined(_MSC_VER)
  unsigned long index;
  _BitScanReverse(&index, value);
#else
  u32 index = 31 - __builtin_clz(value);
#endif  // MSVC
  u32 result = 1;
  return result << index;
}

void DepthPyramidPass::post_render(u32 current_frame_index,
                                   CommandBuffer* gpu_commands,
                                   FrameGraph* frame_graph) {
  if (!enabled) return;

  GpuDevice* gpu = renderer->gpu;

  Texture* depth_pyramid_texture = gpu->access_texture(depth_pyramid);

  if (update_depth_pyramid) {
    gpu_commands->bind_pipeline(depth_pyramid_pipeline);

    u32 width = depth_pyramid_texture->width;
    u32 height = depth_pyramid_texture->height;

    FrameGraphResource* depth_resource =
        (FrameGraphResource*)frame_graph->get_resource("depth");
    TextureHandle depth_handle = depth_resource->resource_info.texture.handle;
    Texture* depth_texture = gpu->access_texture(depth_handle);

    util_add_image_barrier(gpu, gpu_commands->vk_handle, depth_texture,
                           RESOURCE_STATE_SHADER_RESOURCE, 0, 1, true);

    for (u32 mip_index = 0; mip_index < depth_pyramid_texture->mip_level_count;
         ++mip_index) {
      util_add_image_barrier(
          gpu, gpu_commands->vk_handle, depth_pyramid_texture->vk_image,
          RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_UNORDERED_ACCESS, mip_index,
          1, false);

      gpu_commands->bind_descriptor_set(
          &depth_hierarchy_descriptor_set[mip_index], 1, nullptr, 0);

      // TODO Make a function for this
      Pipeline* pipeline = gpu->access_pipeline(depth_pyramid_pipeline);

      struct PushConst {
        glm::vec2 src_image_size;
      };
      PushConst push_const;
      push_const.src_image_size = glm::vec2(width, height);

      vkCmdPushConstants(gpu_commands->vk_handle, pipeline->vk_pipeline_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConst),
                         &push_const);

      // NOTE(marco): local workgroup is 8 x 8
      u32 group_x =
          (width + pipeline->local_size[0] - 1) / pipeline->local_size[0];
      u32 group_y =
          (height + pipeline->local_size[1] - 1) / pipeline->local_size[1];

      gpu_commands->dispatch(group_x, group_y, 1);

      util_add_image_barrier(
          gpu, gpu_commands->vk_handle, depth_pyramid_texture->vk_image,
          RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_SHADER_RESOURCE,
          mip_index, 1, false);

      width /= 2;
      height /= 2;
    }
  }
}

void DepthPyramidPass::on_resize(GpuDevice& gpu, FrameGraph* frame_graph,
                                 u32 new_width, u32 new_height) {
  // Destroy old resources
  // gpu.destroy_texture(depth_pyramid);
  //// Use old depth pyramid levels value
  // for (u32 i = 0; i < depth_pyramid_levels; ++i) {
  //     gpu.destroy_descriptor_set(depth_hierarchy_descriptor_set[i]);
  //     gpu.destroy_texture(depth_pyramid_views[i]);
  // }
  //
  // FrameGraphResource* depth_resource =
  // (FrameGraphResource*)frame_graph->get_resource("depth"); TextureHandle
  // depth_handle = depth_resource->resource_info.texture.handle; Texture*
  // depth_texture = gpu.access_texture(depth_handle);
  //
  // create_depth_pyramid_resource(depth_texture);
}

void DepthPyramidPass::prepare_draws(Scene& scene_, FrameGraph* frame_graph,
                                     Allocator* resident_allocator) {
  glTFScene& scene = (glTFScene&)scene_;

  renderer = scene.renderer;

  FrameGraphNode* node = frame_graph->get_node("depth_pyramid_pass");
  if (node == nullptr) {
    enabled = false;

    return;
  }
  enabled = node->enabled;
  if (!enabled) return;

  GpuDevice& gpu = *renderer->gpu;

  FrameGraphResource* depth_resource =
      (FrameGraphResource*)frame_graph->get_resource("depth");
  TextureHandle depth_handle = depth_resource->resource_info.texture.handle;
  Texture* depth_texture = gpu.access_texture(depth_handle);

  // Sampler does not need to be recreated
  SamplerCreation sc;
  sc.set_address_mode_uvw(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
      .set_min_mag_mip(VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                       VK_SAMPLER_MIPMAP_MODE_NEAREST)
      .set_reduction_mode(VK_SAMPLER_REDUCTION_MODE_MAX)
      .set_name("depth_pyramid_sampler");
  depth_pyramid_sampler = gpu.create_sampler(sc);

  create_depth_pyramid_resource(depth_texture);

  gpu.link_texture_sampler(depth_pyramid, depth_pyramid_sampler);
}

void DepthPyramidPass::free_gpu_resources() {
  if (!enabled) return;

  GpuDevice& gpu = *renderer->gpu;

  gpu.destroy_sampler(depth_pyramid_sampler);
  gpu.destroy_texture(depth_pyramid);

  for (u32 i = 0; i < depth_pyramid_levels; ++i) {
    gpu.destroy_texture(depth_pyramid_views[i]);
    gpu.destroy_descriptor_set(depth_hierarchy_descriptor_set[i]);
  }
}

void DepthPyramidPass::create_depth_pyramid_resource(Texture* depth_texture) {
  // u32 depth_pyramid_width = depth_texture->width / 2;
  // u32 depth_pyramid_height = depth_texture->height / 2;
  //  Note previous_pow2 makes sure all reductions are at most 2x2 which makes
  //  sure they are conservative
  u32 depth_pyramid_width = previous_pow2(depth_texture->width);
  u32 depth_pyramid_height = previous_pow2(depth_texture->height);
  depth_pyramid_width /= 2;
  depth_pyramid_height /= 2;

  GpuDevice& gpu = *renderer->gpu;

  depth_pyramid_levels = 0;
  u32 width = depth_pyramid_width;
  u32 height = depth_pyramid_height;
  while (width >= 2 && height >= 2) {
    depth_pyramid_levels++;

    width /= 2;
    height /= 2;
  }

  TextureCreation depth_hierarchy_creation{};
  depth_hierarchy_creation
      .set_format_type(VK_FORMAT_R32_SFLOAT, TextureType::Enum::Texture2D)
      .set_flags(depth_pyramid_levels, TextureFlags::Compute_mask)
      .set_size(depth_pyramid_width, depth_pyramid_height, 1)
      .set_name("depth_hierarchy");

  depth_pyramid = gpu.create_texture(depth_hierarchy_creation);

  TextureViewCreation depth_pyramid_view_creation;
  depth_pyramid_view_creation.parent_texture = depth_pyramid;
  depth_pyramid_view_creation.array_base_layer = 0;
  depth_pyramid_view_creation.array_layer_count = 1;
  depth_pyramid_view_creation.mip_level_count = 1;
  depth_pyramid_view_creation.name = "depth_pyramid_view";

  DescriptorSetCreation descriptor_set_creation{};

  Program* culling_program =
      renderer->resource_cache.programs.get(hash_calculate("culling"));
  u32 pipeline_index = culling_program->get_pass_index("depth_pyramid");
  depth_pyramid_pipeline = culling_program->passes[pipeline_index].pipeline;
  DescriptorSetLayoutHandle depth_pyramid_layout =
      gpu.get_descriptor_set_layout(depth_pyramid_pipeline,
                                    k_material_descriptor_set_index);

  for (u32 i = 0; i < depth_pyramid_levels; ++i) {
    depth_pyramid_view_creation.mip_base_level = i;

    depth_pyramid_views[i] =
        gpu.create_texture_view(depth_pyramid_view_creation);

    if (i == 0) {
      descriptor_set_creation.reset()
          .texture_sampler(depth_texture->handle, depth_pyramid_sampler, 0)
          .texture_sampler(depth_pyramid_views[i], depth_pyramid_sampler, 1)
          .set_layout(depth_pyramid_layout);
    } else {
      descriptor_set_creation.reset()
          .texture_sampler(depth_pyramid_views[i - 1], depth_pyramid_sampler, 0)
          .texture_sampler(depth_pyramid_views[i], depth_pyramid_sampler, 1)
          .set_layout(depth_pyramid_layout);
    }

    depth_hierarchy_descriptor_set[i] =
        gpu.create_descriptor_set(descriptor_set_creation);
  }
}

//
// LightPass //////////////////////////////////////////////////////////
void LightPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  glTFScene* scene = (glTFScene*)scene_;

  if (renderer) {
    if (/*use_compute*/ false) {
      // PipelineHandle pipeline =
      // renderer->get_pipeline(mesh.pbr_material.material, 1);
      // gpu_commands->bind_pipeline(pipeline);
      // gpu_commands->bind_descriptor_set(&mesh.pbr_material.descriptor_set, 1,
      // nullptr, 0);
      //
      // gpu_commands->dispatch(ceilu32(renderer->gpu->swapchain_width * 1.f /
      // 8), ceilu32(renderer->gpu->swapchain_height * 1.f / 8), 1);
    } else {
      gpu_commands->bind_pipeline(pipeline_handle);
      gpu_commands->bind_vertex_buffer(
          renderer->gpu->get_fullscreen_vertex_buffer(), 0, 0);
      gpu_commands->bind_descriptor_set(&d_set, 1, nullptr, 0);
      Pipeline* pipeline = renderer->gpu->access_pipeline(pipeline_handle);
      vkCmdPushConstants(gpu_commands->vk_handle, pipeline->vk_pipeline_layout,
                         VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LightingData),
                         &lighting_data);

      gpu_commands->draw(3, 1, 0, 0);
    }
  }
}

void LightPass::init() { renderer = nullptr; }

void LightPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                              Allocator* resident_allocator) {
  if (renderer) return;
  renderer = scene.renderer;

  FrameGraphNode* node = frame_graph->get_node("lighting_pass");
  HASSERT(node);
  enabled = node->enabled;
  if (!enabled) return;

  const u64 hashed_name = hash_calculate("pbr_lighting");
  Program* lighting_program =
      renderer->resource_cache.programs.get(hashed_name);
  pipeline_handle = lighting_program->passes[0].pipeline;

  DescriptorSetCreation ds_creation{};
  DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(
      lighting_program->passes[0].pipeline, k_material_descriptor_set_index);
  ds_creation.buffer(scene.scene_constant_buffer, 0)
      .buffer(scene.light_data_buffer, 1)
      .set_layout(layout);
  d_set = renderer->gpu->create_descriptor_set(ds_creation);

  FrameGraphResource* color_texture =
      get_output_texture(frame_graph, node->inputs[0]);
  FrameGraphResource* normal_texture =
      get_output_texture(frame_graph, node->inputs[1]);
  FrameGraphResource* roughness_texture =
      get_output_texture(frame_graph, node->inputs[2]);
  FrameGraphResource* depth_texture =
      get_output_texture(frame_graph, node->inputs[3]);

  lighting_data.gbuffer_color_index =
      color_texture->resource_info.texture.handle.index;
  lighting_data.gbuffer_rmo_index =
      roughness_texture->resource_info.texture.handle.index;
  lighting_data.gbuffer_normal_index =
      normal_texture->resource_info.texture.handle.index;
  lighting_data.depth_texture_index =
      depth_texture->resource_info.texture.handle.index;
}

void LightPass::fill_gpu_material_buffer() {}

void LightPass::free_gpu_resources() {
  if (renderer) {
    GpuDevice& gpu = *renderer->gpu;

    gpu.destroy_descriptor_set(d_set);
  }
}

//
// TransparentPass ////////////////////////////////////////////////////////
void TransparentPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  if (!renderer) return;
  glTFScene* scene = (glTFScene*)scene_;

  Renderer* renderer = scene->renderer;

#if NVIDIA
  const u64 meshlet_hashed_name = hash_calculate("meshlet_nv");
#else
  const u64 meshlet_hashed_name = hash_calculate("meshlet_ext");
#endif  // NVIDIA
  Program* meshlet_program =
      renderer->resource_cache.programs.get(meshlet_hashed_name);

  PipelineHandle pipeline =
      meshlet_program->passes[meshlet_program_index].pipeline;

  gpu_commands->bind_pipeline(pipeline);

  u32 buffer_frame_index = renderer->gpu->current_frame;
  gpu_commands->bind_descriptor_set(
      &scene->mesh_shader_descriptor_set[buffer_frame_index], 1, nullptr, 0);

  gpu_commands->draw_mesh_task_indirect_count(
      scene->mesh_indirect_draw_early_command_buffers[buffer_frame_index],
      offsetof(GPUMeshDrawCommand, indirectMS) +
          (sizeof(GPUMeshDrawCommand) * scene->opaque_meshes.size),
      scene->mesh_draw_count_buffers[buffer_frame_index],
      offsetof(GPUMeshDrawCounts, transparent_mesh_visible_count),
      scene->transparent_meshes.size, sizeof(GPUMeshDrawCommand));
}

void TransparentPass::init() {
  renderer = nullptr;
  double_sided_mesh_count = 0;
  meshes = nullptr;
  mesh_count = 0;
}

void TransparentPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                                    Allocator* resident_allocator) {
  renderer = scene.renderer;

  FrameGraphNode* node = frame_graph->get_node("transparent_pass");
  HASSERT(node);

  enabled = true;

  if (renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER) {
#if NVIDIA
    Program* main_program =
        renderer->resource_cache.programs.get(hash_calculate("meshlet_nv"));
#else
    Program* main_program =
        renderer->resource_cache.programs.get(hash_calculate("meshlet_ext"));
#endif  // NVIDIA
    meshlet_program_index = main_program->get_pass_index("transparent_no_cull");
  }
}

void TransparentPass::free_gpu_resources() {}

cstring node_type_to_cstring(NodeType type) {
  switch (type) {
    case Helix::NodeType::Node:
      return "Node";
      break;
    case Helix::NodeType::MeshNode:
      return "Mesh Node";
      break;
    case Helix::NodeType::LightNode:
      return "Light Node";
      break;
    default:
      HCRITICAL("Invalid node type");
      return "Invalid node type";
      break;
  }
}

//
// DebugPass ////////////////////////////////////////////////////////
void DebugPass::render(CommandBuffer* gpu_commands, Scene* scene_) {
  glTFScene* scene = (glTFScene*)scene_;
  gpu_commands->bind_pipeline(debug_light_pipeline);
  gpu_commands->bind_descriptor_set(&debug_light_dset, 1, nullptr, 0);
  gpu_commands->draw(6, scene->node_pool.light_nodes.used_indices, 0, 0);
}

void DebugPass::init() { renderer = nullptr; }

void DebugPass::prepare_draws(glTFScene& scene, FrameGraph* frame_graph,
                              Allocator* resident_allocator) {
  if (renderer) return;
  renderer = scene.renderer;

  FrameGraphNode* node = frame_graph->get_node("debug_pass");
  HASSERT(node);

  const u64 hashed_name = hash_calculate("debug");
  Program* debug_program =
      scene.renderer->resource_cache.programs.get(hashed_name);

  debug_light_pipeline =
      debug_program->passes[debug_program->get_pass_index("debug_light")]
          .pipeline;

  // glm::vec4 positions[2] = { {glm::vec4(0, 10, 0, 6)}, {glm::vec4(10, 10, 0,
  // 7)} }; GPUDebugIcon debug_icons{}; debug_icons.position_texture_index[0] =
  // positions[0]; debug_icons.position_texture_index[1] = positions[1];
  // debug_icons.count = 2;

  // BufferCreation buffer_creation{};
  // buffer_creation.reset().set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
  // ResourceUsageType::Dynamic,
  // sizeof(GPUDebugIcon)).set_name("debug_icons_buffer").set_data(&debug_icons);
  // debug_icons_buffer = renderer->create_buffer(buffer_creation)->handle;

  DescriptorSetCreation ds_creation{};
  DescriptorSetLayoutHandle layout =
      scene.renderer->gpu->get_descriptor_set_layout(debug_light_pipeline, 1);
  ds_creation.buffer(scene.scene_constant_buffer, 0)
      .buffer(scene.light_data_buffer, 1)
      .set_layout(layout);
  debug_light_dset = scene.renderer->gpu->create_descriptor_set(ds_creation);
}

void DebugPass::free_gpu_resources() {
  renderer->gpu->destroy_descriptor_set(debug_light_dset);
}

// glTFDrawTask //////////////////////////////////

void glTFDrawTask::init(GpuDevice* gpu_, FrameGraph* frame_graph_,
                        Renderer* renderer_, ImGuiService* imgui_,
                        GPUProfiler* gpu_profiler_, glTFScene* scene_) {
  gpu = gpu_;
  frame_graph = frame_graph_;
  renderer = renderer_;
  imgui = imgui_;
  gpu_profiler = gpu_profiler_;
  scene = scene_;
}

void glTFDrawTask::ExecuteRange(enki::TaskSetPartition range_,
                                uint32_t threadnum_) {
  ZoneScoped;

  thread_id = threadnum_;

  // HTRACE( "Executing draw task from thread {}", threadnum_ );
  //  TODO: improve getting a command buffer/pool
  CommandBuffer* gpu_commands = gpu->get_command_buffer(threadnum_, true);

  frame_graph->render(gpu->current_frame, gpu_commands, scene);

  gpu_commands->push_marker("Fullscreen");
  gpu_commands->clear(0.3f, 0.3f, 0.3f, 1.f, 0);
  gpu_commands->clear_depth_stencil(1.0f, 0);

  // util_add_image_barrier(gpu, gpu_commands->vk_handle, fullscreen_texture,
  // RESOURCE_STATE_RENDER_TARGET, 0, 1, false);

  gpu_commands->bind_pass(gpu->fullscreen_render_pass,
                          gpu->fullscreen_framebuffer, false);
  gpu_commands->set_scissor(nullptr);
  Viewport viewport{};
  viewport.rect = {0, 0, gpu->swapchain_width, gpu->swapchain_height};
  viewport.max_depth = 1.0f;
  viewport.min_depth = 0.0f;

  gpu_commands->set_viewport(&viewport);
  gpu_commands->bind_pipeline(scene->fullscreen_program->passes[0].pipeline);
  gpu_commands->bind_descriptor_set(&scene->fullscreen_ds, 1, nullptr, 0);
  gpu_commands->draw(3, 1, 0, scene->fullscreen_texture_index);

  gpu_commands->end_current_render_pass();

  gpu_commands->pop_marker();

  imgui->render(*gpu_commands, false);
  gpu_commands->end_current_render_pass();

  gpu_commands->pop_marker();

  gpu_profiler->update(*gpu);

  // Send commands to GPU
  gpu->queue_command_buffer(gpu_commands);
}

// gltfScene //////////////////////////////////////////////////

void glTFScene::init(Renderer* _renderer, Allocator* resident_allocator,
                     FrameGraph* _frame_graph, StackAllocator* stack_allocator,
                     AsynchronousLoader* async_loader) {
  u32 k_num_meshes = 200;
  renderer = _renderer;
  frame_graph = _frame_graph;
  scratch_allocator = stack_allocator;
  main_allocator = resident_allocator;
  loader = async_loader;

  transparent_meshes.init(resident_allocator, k_num_meshes);
  opaque_meshes.init(resident_allocator, k_num_meshes);

  node_pool.init(resident_allocator);

  images.init(resident_allocator, k_num_meshes);
  samplers.init(resident_allocator, 1);
  buffers.init(resident_allocator, k_num_meshes);

  meshlets.init(resident_allocator, 16);
  meshlet_vertex_and_index_indices.init(resident_allocator, 16);
  meshlets_vertex_positions.init(resident_allocator, 16);
  meshlets_vertex_data.init(resident_allocator, 16);

  lights.init(resident_allocator, MAX_LIGHTS);

  names.init(hmega(1), main_allocator);

  // Creating the light image
  TextureResource* tr = renderer->create_texture(
      "Light", HELIX_TEXTURE_FOLDER "lights/point_light.png", true);
  HASSERT(tr != nullptr);
  light_texture = *tr;

  BufferCreation buffer_creation;

  add_light();
  // Constant buffer
  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(GPUSceneData))
      .set_name("scene_constant_buffer");
  scene_constant_buffer = renderer->create_buffer(buffer_creation)->handle;

  // depth_pre_pass.init();
  gbuffer_pass.init();
  transparent_pass.init();
  light_pass.init();
  debug_pass.init();

  fullscreen_program =
      renderer->resource_cache.programs.get(hash_calculate("fullscreen"));

  DescriptorSetCreation dsc;
  DescriptorSetLayoutHandle descriptor_set_layout =
      renderer->gpu->get_descriptor_set_layout(
          fullscreen_program->passes[0].pipeline,
          k_material_descriptor_set_index);
  dsc.reset()
      .buffer(scene_constant_buffer, 0)
      .set_layout(descriptor_set_layout);
  fullscreen_ds = renderer->gpu->create_descriptor_set(dsc);

  FrameGraphResource* texture = frame_graph->get_resource("final");
  if (texture != nullptr) {
    fullscreen_texture_index = texture->resource_info.texture.handle.index;
  }

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(GPULight) * MAX_LIGHTS)
      .set_name("lights_data_buffer");
  light_data_buffer = renderer->create_buffer(buffer_creation)->handle;
}

void glTFScene::load(cstring filename, cstring path,
                     Allocator* resident_allocator,
                     StackAllocator* temp_allocator,
                     AsynchronousLoader* async_loader) {
  sizet temp_allocator_initial_marker = temp_allocator->get_marker();

  // Time statistics
  i64 start_scene_loading = Time::now();

  gltf_scene = gltf_load_file(filename);

  i64 end_loading_file = Time::now();

  StringBuffer name_buffer;
  name_buffer.init(hkilo(100), temp_allocator);

  // Load all textures
  Array<FileLoadRequest> texture_requests;
  texture_requests.init(
      resident_allocator, gltf_scene.images_count,
      gltf_scene.images_count);  // TODO: Maybe use stack allocator;

  for (u32 image_index = 0; image_index < gltf_scene.images_count;
       ++image_index) {
    glTF::Image& image = gltf_scene.images[image_index];

    int comp, width, height;

    stbi_info(image.uri.data, &width, &height, &comp);

    u32 mip_levels = 1;
    if (true) {
      u32 w = width;
      u32 h = height;

      while (w > 1 && h > 1) {
        w /= 2;
        h /= 2;

        ++mip_levels;
      }
    }
    // Creates an empty texture resource.
    TextureCreation tc;
    tc.set_data(nullptr)
        .set_format_type(VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D)
        .set_flags(mip_levels, 0)
        .set_size((u16)width, (u16)height, 1)
        .set_name(image.uri.data);
    TextureResource* tr = renderer->create_texture(tc);
    HASSERT(tr != nullptr);

    images.push(*tr);

    // Reconstruct file path
    char* full_filename =
        name_buffer.append_use_f("%s%s", path, image.uri.data);

    FileLoadRequest& request = texture_requests[image_index];
    request.texture = tr->handle;
    strcpy(request.path, full_filename);

    // Reset name buffer
    name_buffer.clear();
  }

  async_loader->file_load_requests.push_array(texture_requests);

  texture_requests.shutdown();

  i64 end_loading_textures_files = Time::now();

  i64 end_creating_textures = Time::now();

  // Load all samplers
  for (u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count;
       ++sampler_index) {
    glTF::Sampler& sampler = gltf_scene.samplers[sampler_index];

    char* sampler_name = renderer->resource_name_buffer.append_use_f(
        "sampler_%u", sampler_index + current_samplers_count);

    SamplerCreation creation;
    switch (sampler.min_filter) {
      case glTF::Sampler::NEAREST:
        creation.min_filter = VK_FILTER_NEAREST;
        break;
      case glTF::Sampler::LINEAR:
        creation.min_filter = VK_FILTER_LINEAR;
        break;
      case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
        creation.min_filter = VK_FILTER_LINEAR;
        creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
      case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
        creation.min_filter = VK_FILTER_LINEAR;
        creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
      case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
        creation.min_filter = VK_FILTER_NEAREST;
        creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
      case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
        creation.min_filter = VK_FILTER_NEAREST;
        creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    }

    creation.mag_filter = sampler.mag_filter == glTF::Sampler::Filter::LINEAR
                              ? VK_FILTER_LINEAR
                              : VK_FILTER_NEAREST;

    switch (sampler.wrap_s) {
      case glTF::Sampler::CLAMP_TO_EDGE:
        creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
      case glTF::Sampler::MIRRORED_REPEAT:
        creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
      case glTF::Sampler::REPEAT:
        creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    }

    switch (sampler.wrap_t) {
      case glTF::Sampler::CLAMP_TO_EDGE:
        creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        break;
      case glTF::Sampler::MIRRORED_REPEAT:
        creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        break;
      case glTF::Sampler::REPEAT:
        creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        break;
    }

    creation.name = sampler_name;

    SamplerResource* sr = renderer->create_sampler(creation);
    HASSERT(sr != nullptr);

    samplers.push(*sr);
  }

  i64 end_creating_samplers = Time::now();

  // Temporary array of buffer data
  Array<void*> buffers_data;
  buffers_data.init(resident_allocator, gltf_scene.buffers_count);

  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count;
       ++buffer_index) {
    glTF::Buffer& buffer = gltf_scene.buffers[buffer_index];

    FileReadResult buffer_data =
        file_read_binary(buffer.uri.data, resident_allocator);
    if (buffer_data.data == nullptr) {
      HWARN("No .bin found in the glTF model");
      buffer_data.data = (char*)halloca(buffer.byte_length, resident_allocator);
      memory_copy(buffer_data.data, buffer.uri.data, buffer.byte_length);
      buffer_data.size = buffer.byte_length;
    }
    buffers_data.push(buffer_data.data);
  }

  i64 end_reading_buffers_data = Time::now();

  // Load all buffers and initialize them with buffer data
  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffer_views_count;
       ++buffer_index) {
    glTF::BufferView& buffer = gltf_scene.buffer_views[buffer_index];

    i32 offset = buffer.byte_offset;
    if (offset == glTF::INVALID_INT_VALUE) {
      offset = 0;
    }

    u8* buffer_data = (u8*)buffers_data[buffer.buffer] + offset;

    // NOTE(marco): the target attribute of a BufferView is not mandatory, so we
    // prepare for both uses
    VkBufferUsageFlags flags =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    char* buffer_name = buffer.name.data;
    if (buffer_name == nullptr) {
      // buffer_name = name_buffer.append_use_f("buffer_%u", buffer_index);
    }
    // TODO: Identify resources (buffers in this case) that have the same name
    buffer_name = renderer->resource_name_buffer.append_use_f(
        "buffer_%u", buffer_index + current_buffers_count);
    BufferCreation buffer_creation;
    buffer_creation.reset()
        .set_device_only(true)
        .set_name(buffer_name)
        .set(flags, ResourceUsageType::Immutable, buffer.byte_length);
    BufferResource* br = renderer->create_buffer(buffer_creation);
    HASSERT(br != nullptr);

    async_loader->request_buffer_upload(buffer_data, br->handle);

    buffers.push(*br);
  }

  i64 end_creating_buffers = Time::now();

  // Init runtime meshes
  // TODO: Right now mesh node has a reference to the data in meshes, This data
  // changes because
  //       its in an array.
  //       Figure out how to make it so when meshes "grows" this does not affect
  //       MeshNode.
  // meshes.init(resident_allocator, 200);

  i64 end_loading = Time::now();

  HINFO(
      "Loaded scene {} in {} seconds.\nStats:\n\tReading GLTF file {} "
      "seconds\n\tTextures Creating {} seconds\n\tCreating Samplers {} "
      "seconds\n\tReading Buffers Data {} seconds\n\tCreating Buffers {} "
      "seconds",
      filename, Time::delta_seconds(start_scene_loading, end_loading),
      Time::delta_seconds(start_scene_loading, end_loading_file),
      Time::delta_seconds(end_loading_file, end_creating_textures),
      Time::delta_seconds(end_creating_textures, end_creating_samplers),
      Time::delta_seconds(end_creating_samplers, end_reading_buffers_data),
      Time::delta_seconds(end_reading_buffers_data, end_creating_buffers));

  ///////////////////////// Prepare Draws ///////////////////////////////

  const u64 hashed_name = hash_calculate("geometry");
  Program* geometry_program =
      renderer->resource_cache.programs.get(hashed_name);

  glTF::Scene& root_gltf_scene = gltf_scene.scenes[gltf_scene.scene];

  Array<i32> node_parents;
  node_parents.init(temp_allocator, gltf_scene.nodes_count,
                    gltf_scene.nodes_count);

  Array<glm::mat4> node_matrix;
  node_matrix.init(temp_allocator, gltf_scene.nodes_count,
                   gltf_scene.nodes_count);

  Array<u32> node_stack;
  node_stack.init(&MemoryService::instance()->system_allocator, 8);

  // Create the node resources
  Array<NodeHandle> node_handles;
  node_handles.init(temp_allocator, gltf_scene.nodes_count,
                    gltf_scene.nodes_count);

  for (u32 node_index = 0; node_index < gltf_scene.nodes_count; ++node_index) {
    glTF::Node& node = gltf_scene.nodes[node_index];
    node_handles[node_index] = node_pool.obtain_node(NodeType::Node);
  }

  // Root Nodes
  for (u32 node_index = 0; node_index < root_gltf_scene.nodes_count;
       ++node_index) {
    u32 root_node_index = root_gltf_scene.nodes[node_index];
    node_parents[root_node_index] = -1;
    node_stack.push(root_node_index);
    Node* node = (Node*)node_pool.access_node(node_handles[root_node_index]);
    node_pool.get_root_node()->add_child(node);
  }

  u32 num_double_sided_meshes_t = 0;  // Transparent meshes
  u32 num_double_sided_meshes = 0;

  // Meshlets
  const sizet max_vertices = 64;
  const sizet max_triangles = 124;
  const f32 cone_weight = 0.0f;

  while (node_stack.size) {
    u32 node_index = node_stack.back();
    node_stack.pop();
    glTF::Node& node = gltf_scene.nodes[node_index];

    glm::mat4 local_matrix{};
    Transform local_transform{};
    local_transform.translation = glm::vec3(0.0f);
    local_transform.scale = glm::vec3(1.0f);
    local_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (node.matrix_count) {
      memcpy(&local_matrix, node.matrix, sizeof(glm::mat4));
      local_transform.set_transform(local_matrix);
    } else {
      glm::vec3 node_scale(1.0f, 1.0f, 1.0f);
      if (node.scale_count != 0) {
        HASSERT(node.scale_count == 3);
        node_scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
      }

      glm::vec3 node_translation(0.f, 0.f, 0.f);
      if (node.translation_count) {
        HASSERT(node.translation_count == 3);
        node_translation = glm::vec3(node.translation[0], node.translation[1],
                                     node.translation[2]);
      }

      // Rotation is written as a plain quaternion
      glm::quat node_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      if (node.rotation_count) {
        HASSERT(node.rotation_count == 4);
        node_rotation = glm::quat(node.rotation[3], node.rotation[0],
                                  node.rotation[1], node.rotation[2]);
      }

      local_transform.translation = node_translation;
      local_transform.scale = node_scale;
      local_transform.rotation = node_rotation;

      local_matrix = local_transform.calculate_matrix();
    }

    node_matrix[node_index] = local_matrix;

    Node* base_node = (Node*)node_pool.access_node(node_handles[node_index]);

    cstring node_name =
        names.append_use_f("%s%d", "Node_", node_handles[node_index].index);

    base_node->name = node.name.data ? node.name.data : node_name;
    base_node->local_transform = local_transform;

    i32 node_parent = node_parents[node_index];
    // Nodes that don't have parents would already have their parent set to the
    // root node
    if (node_parent != -1) base_node->parent = node_handles[node_parent];

    // Assuming nodes that contain meshes don't contain other glTF nodes
    if (node.mesh == glTF::INVALID_INT_VALUE) {
      base_node->children.init(node_pool.allocator, node.children_count,
                               node.children_count);
      for (u32 child_index = 0; child_index < node.children_count;
           ++child_index) {
        u32 child_node_index = node.children[child_index];
        node_parents[child_node_index] = node_index;

        node_stack.push(child_node_index);
        base_node->children[child_index] = node_handles[child_node_index];
      }
      continue;
    }

    glTF::Mesh& gltf_mesh = gltf_scene.meshes[node.mesh];

    base_node->children.init(node_pool.allocator, gltf_mesh.primitives_count);

    glm::vec3 node_scale{1.0f, 1.0f, 1.0f};  // TODO: Needed?
    if (node.scale_count != 0) {
      HASSERT(node.scale_count == 3);
      node_scale = glm::vec3{node.scale[0], node.scale[1], node.scale[2]};
    }

    // gltf_mesh_to_mesh_offset.init(resident_allocator_, 16);

    // Gltf primitives are conceptually submeshes.
    for (u32 primitive_index = 0; primitive_index < gltf_mesh.primitives_count;
         ++primitive_index) {
      Mesh mesh{};

      // mesh.gpu_mesh_index = opaque_meshes.size;

      glTF::MeshPrimitive& mesh_primitive =
          gltf_mesh.primitives[primitive_index];

      const i32 position_accessor_index = gltf_get_attribute_accessor_index(
          mesh_primitive.attributes, mesh_primitive.attribute_count,
          "POSITION");
      const i32 tangent_accessor_index = gltf_get_attribute_accessor_index(
          mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT");
      const i32 normal_accessor_index = gltf_get_attribute_accessor_index(
          mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL");
      const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index(
          mesh_primitive.attributes, mesh_primitive.attribute_count,
          "TEXCOORD_0");

      get_mesh_vertex_buffer(*this, position_accessor_index,
                             mesh.position_buffer, mesh.position_offset);
      get_mesh_vertex_buffer(*this, tangent_accessor_index, mesh.tangent_buffer,
                             mesh.tangent_offset);
      get_mesh_vertex_buffer(*this, normal_accessor_index, mesh.normal_buffer,
                             mesh.normal_offset);
      get_mesh_vertex_buffer(*this, texcoord_accessor_index,
                             mesh.texcoord_buffer, mesh.texcoord_offset);

      // Vertex positions
      glTF::Accessor& position_accessor =
          gltf_scene.accessors[position_accessor_index];
      glTF::BufferView& position_buffer_view =
          gltf_scene.buffer_views[position_accessor.buffer_view];
      i32 position_data_offset = glTF::get_data_offset(
          position_accessor.byte_offset, position_buffer_view.byte_offset);
      f32* vertices = (f32*)((u8*)buffers_data[position_buffer_view.buffer] +
                             position_data_offset);

      // Calculate bounding sphere center
      glm::vec3 position_min{position_accessor.min[0], position_accessor.min[1],
                             position_accessor.min[2]};
      glm::vec3 position_max{position_accessor.max[0], position_accessor.max[1],
                             position_accessor.max[2]};
      glm::vec3 bounding_center = position_min + position_max;
      bounding_center = bounding_center / 2.0f;

      // Calculate bounding sphere radius
      f32 radius = Helix::max(glm::distance(position_max, bounding_center),
                              glm::distance(position_min, bounding_center));
      mesh.bounding_sphere = {bounding_center.x, bounding_center.y,
                              bounding_center.z, radius};

      // Vertex normals
      f32* normals = nullptr;
      if (normal_accessor_index != -1) {
        glTF::Accessor& normal_buffer_accessor =
            gltf_scene.accessors[normal_accessor_index];
        glTF::BufferView& normal_buffer_view =
            gltf_scene.buffer_views[normal_buffer_accessor.buffer_view];
        i32 normal_data_offset = glTF::get_data_offset(
            normal_buffer_accessor.byte_offset, normal_buffer_view.byte_offset);
        normals = (f32*)((u8*)buffers_data[normal_buffer_view.buffer] +
                         normal_data_offset);
        mesh.pbr_material.flags |= DrawFlags_HasNormals;
      }

      // Vertex texture coords
      f32* tex_coords = nullptr;
      if (texcoord_accessor_index != -1) {
        glTF::Accessor& tex_coord_buffer_accessor =
            gltf_scene.accessors[texcoord_accessor_index];
        glTF::BufferView& tex_coord_buffer_view =
            gltf_scene.buffer_views[tex_coord_buffer_accessor.buffer_view];
        i32 tex_coord_data_offset =
            glTF::get_data_offset(tex_coord_buffer_accessor.byte_offset,
                                  tex_coord_buffer_view.byte_offset);
        tex_coords = (f32*)((u8*)buffers_data[tex_coord_buffer_view.buffer] +
                            tex_coord_data_offset);
        mesh.pbr_material.flags |= DrawFlags_HasTexCoords;
      }

      // Vertex tangents
      f32* tangents = nullptr;
      if (tangent_accessor_index != -1) {
        glTF::Accessor& tangent_buffer_accessor =
            gltf_scene.accessors[tangent_accessor_index];
        glTF::BufferView& tangent_buffer_view =
            gltf_scene.buffer_views[tangent_buffer_accessor.buffer_view];
        i32 tangent_data_offset =
            glTF::get_data_offset(tangent_buffer_accessor.byte_offset,
                                  tangent_buffer_view.byte_offset);
        tangents = (f32*)((u8*)buffers_data[tangent_buffer_view.buffer] +
                          tangent_data_offset);
        mesh.pbr_material.flags |= DrawFlags_HasTangents;
      }

      // Create index buffer
      glTF::Accessor& indices_accessor =
          gltf_scene.accessors[mesh_primitive.indices];
      HASSERT(indices_accessor.component_type ==
                  glTF::Accessor::ComponentType::UNSIGNED_SHORT ||
              indices_accessor.component_type ==
                  glTF::Accessor::ComponentType::UNSIGNED_INT);
      mesh.index_type = (indices_accessor.component_type ==
                         glTF::Accessor::ComponentType::UNSIGNED_SHORT)
                            ? VK_INDEX_TYPE_UINT16
                            : VK_INDEX_TYPE_UINT32;

      glTF::BufferView& indices_buffer_view =
          gltf_scene.buffer_views[indices_accessor.buffer_view];
      BufferResource& indices_buffer_gpu =
          buffers[indices_accessor.buffer_view + current_buffers_count];
      mesh.index_buffer = indices_buffer_gpu.handle;
      mesh.index_offset =
          indices_accessor.byte_offset == glTF::INVALID_INT_VALUE
              ? 0
              : indices_accessor.byte_offset;
      mesh.primitive_count = indices_accessor.count;

      i32 indicies_data_offset = glTF::get_data_offset(
          indices_accessor.byte_offset, indices_buffer_view.byte_offset);
      u16* indices = (u16*)((u8*)buffers_data[indices_buffer_view.buffer] +
                            indicies_data_offset);

      // meshopt_optimizeVertexCache(indices, indices, indices_accessor.count,
      // position_accessor.count); meshopt_optimizeVertexFetch(vertices,
      // indices, indices_accessor.count, vertices, position_accessor.count,
      // sizeof(glm::vec3)); if(normals)
      //     meshopt_optimizeVertexFetch(normals, indices,
      //     indices_accessor.count, normals, position_accessor.count,
      //     sizeof(glm::vec3));
      // if(tangents)
      //     meshopt_optimizeVertexFetch(tangents, indices,
      //     indices_accessor.count, tangents, position_accessor.count,
      //     sizeof(glm::vec4));
      // if(tex_coords)
      //     meshopt_optimizeVertexFetch(tex_coords, indices,
      //     indices_accessor.count, tex_coords, position_accessor.count,
      //     sizeof(glm::vec2));

      // Create material
      if (mesh_primitive.material != glTF::INVALID_INT_VALUE) {
        glTF::Material& material =
            gltf_scene.materials[mesh_primitive.material];
        fill_pbr_material(*renderer, material, mesh.pbr_material);
      }
      // Create material buffer
      BufferCreation buffer_creation;
      cstring mesh_data_name = renderer->resource_name_buffer.append_use_f(
          "mesh_data_%u", opaque_meshes.size + transparent_meshes.size);
      buffer_creation.reset()
          .set(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic,
               sizeof(MeshData))
          .set_name(mesh_data_name);
      mesh.pbr_material.material_buffer =
          renderer->create_buffer(buffer_creation)->handle;

      // Meshlets
      const sizet max_meshlets = meshopt_buildMeshletsBound(
          indices_accessor.count, max_vertices, max_triangles);
      sizet temp_marker = temp_allocator->get_marker();

      Array<meshopt_Meshlet> local_meshlets;
      local_meshlets.init(temp_allocator, (u32)max_meshlets, (u32)max_meshlets);

      Array<u32> meshlet_vertex_indices;
      meshlet_vertex_indices.init(temp_allocator,
                                  (u32)max_meshlets * (u32)max_vertices,
                                  (u32)max_meshlets * (u32)max_vertices);

      Array<u8> meshlet_triangles;
      meshlet_triangles.init(temp_allocator,
                             (u32)max_meshlets * (u32)max_triangles * 3,
                             (u32)max_meshlets * (u32)max_triangles * 3);

      sizet meshlet_count = meshopt_buildMeshlets(
          local_meshlets.data, meshlet_vertex_indices.data,
          meshlet_triangles.data, indices, indices_accessor.count, vertices,
          position_accessor.count, sizeof(glm::vec3), max_vertices,
          max_triangles, cone_weight);

      u32 meshlet_vertex_offset = meshlets_vertex_positions.size;
      for (u32 v = 0; v < (u32)position_accessor.count; ++v) {
        GPUMeshletVertexPosition meshlet_vertex_pos{};

        meshlet_vertex_pos.position[0] = vertices[v * 3 + 0];
        meshlet_vertex_pos.position[1] = vertices[v * 3 + 1];
        meshlet_vertex_pos.position[2] = vertices[v * 3 + 2];

        meshlets_vertex_positions.push(meshlet_vertex_pos);

        GPUMeshletVertexData meshlet_vertex_data{};

        if (normals != nullptr) {
          meshlet_vertex_data.normal[0] =
              (u8)((normals[v * 3 + 0] + 1.0f) * 127.0f);
          meshlet_vertex_data.normal[1] =
              (u8)((normals[v * 3 + 1] + 1.0f) * 127.0f);
          meshlet_vertex_data.normal[2] =
              (u8)((normals[v * 3 + 2] + 1.0f) * 127.0f);
        }

        if (tangents != nullptr) {
          meshlet_vertex_data.tangent[0] =
              (u8)((tangents[v * 3 + 0] + 1.0f) * 127.0f);
          meshlet_vertex_data.tangent[1] =
              (u8)((tangents[v * 3 + 1] + 1.0f) * 127.0f);
          meshlet_vertex_data.tangent[2] =
              (u8)((tangents[v * 3 + 2] + 1.0f) * 127.0f);
          meshlet_vertex_data.tangent[3] =
              (u8)((tangents[v * 3 + 3] + 1.0f) * 127.0f);
        }

        if (tex_coords != nullptr) {
          meshlet_vertex_data.uv_coords[0] =
              meshopt_quantizeHalf(tex_coords[v * 2 + 0]);
          meshlet_vertex_data.uv_coords[1] =
              meshopt_quantizeHalf(tex_coords[v * 2 + 1]);
        }

        meshlets_vertex_data.push(meshlet_vertex_data);
      }

      // Cache meshlet offset
      mesh.meshlet_offset = meshlets.size;
      mesh.meshlet_count = (u32)meshlet_count;

      // Nodes
      // TODO Make this a primitive struct. Not a MeshNode
      NodeHandle mesh_handle = node_pool.obtain_node(NodeType::MeshNode);
      MeshNode* mesh_node_primitive =
          (MeshNode*)node_pool.access_node(mesh_handle);
      mesh_node_primitive->children.size = 0;
      mesh_node_primitive->name = "Mesh_Primitive";
      mesh_node_primitive->parent = node_handles[node_index];

      mesh_node_primitive->children.size = 0;

      // TODO: Extract the position from the position buffer.
      base_node->children.push(mesh_handle);

      mesh.node_index = mesh_handle.index;

      if (mesh.is_transparent()) {
        transparent_meshes.push(mesh);
        mesh_node_primitive->mesh =
            &transparent_meshes[transparent_meshes.size - 1];
        if (mesh.is_double_sided()) num_double_sided_meshes_t++;
      } else {
        opaque_meshes.push(mesh);
        mesh_node_primitive->mesh = &opaque_meshes[opaque_meshes.size - 1];
        if (mesh.is_double_sided()) num_double_sided_meshes++;
      }

      for (u32 m = 0; m < meshlet_count; ++m) {
        meshopt_Meshlet& local_meshlet = local_meshlets[m];

        meshopt_Bounds meshlet_bounds = meshopt_computeMeshletBounds(
            meshlet_vertex_indices.data + local_meshlet.vertex_offset,
            meshlet_triangles.data + local_meshlet.triangle_offset,
            local_meshlet.triangle_count, vertices, position_accessor.count,
            sizeof(glm::vec3));

        GPUMeshlet meshlet{};
        meshlet.data_offset = meshlet_vertex_and_index_indices.size;
        meshlet.vertex_count = local_meshlet.vertex_count;
        meshlet.triangle_count = local_meshlet.triangle_count;

        meshlet.center =
            glm::vec3(meshlet_bounds.center[0], meshlet_bounds.center[1],
                      meshlet_bounds.center[2]);
        meshlet.radius = meshlet_bounds.radius;

        meshlet.cone_axis[0] = meshlet_bounds.cone_axis_s8[0];
        meshlet.cone_axis[1] = meshlet_bounds.cone_axis_s8[1];
        meshlet.cone_axis[2] = meshlet_bounds.cone_axis_s8[2];

        meshlet.cone_cutoff = meshlet_bounds.cone_cutoff_s8;
        // meshlet.mesh_index = mesh.is_transparent() ? opaque_meshes.size +
        // transparent_meshes.size - 1 : opaque_meshes.size - 1; // TODO: What
        // about transparent meshes?
#if NVIDIA

        // Resize data array
        const u32 index_group_count =
            (local_meshlet.triangle_count * 3 + 3) / 4;
        meshlet_vertex_and_index_indices.set_capacity(
            meshlet_vertex_and_index_indices.size + local_meshlet.vertex_count +
            index_group_count);

        for (u32 i = 0; i < meshlet.vertex_count; ++i) {
          u32 vertex_index =
              meshlet_vertex_offset +
              meshlet_vertex_indices[local_meshlet.vertex_offset + i];
          meshlet_vertex_and_index_indices.push(vertex_index);
        }
        // Store indices as uint32
        // NOTE(marco): we write 4 indices at at time, it will come in handy in
        // the mesh shader
        const u32* index_groups = reinterpret_cast<const u32*>(
            meshlet_triangles.data + local_meshlet.triangle_offset);
        for (u32 i = 0; i < index_group_count; ++i) {
          const u32 index_group = index_groups[i];
          meshlet_vertex_and_index_indices.push(index_group);
        }
#else
        // Resize data array
        // Pack 3 u8 incicies into a u32
        const u32 index_group_count = (local_meshlet.triangle_count * 3) / 3;
        meshlet_vertex_and_index_indices.set_capacity(
            meshlet_vertex_and_index_indices.size + local_meshlet.vertex_count +
            index_group_count);

        for (u32 i = 0; i < meshlet.vertex_count; ++i) {
          u32 vertex_index =
              meshlet_vertex_offset +
              meshlet_vertex_indices[local_meshlet.vertex_offset + i];
          meshlet_vertex_and_index_indices.push(vertex_index);
        }
        // Store indices as uint32
        // NOTE(marco): we write to the gl_PrimitiveTriangleIndicesEXT uvec3
        // array
        const u8* p_indicies = reinterpret_cast<const u8*>(
            meshlet_triangles.data + local_meshlet.triangle_offset);
        for (u32 i = 0; i < index_group_count; ++i) {
          const u32 index_group = (u32(p_indicies[i * 3 + 0]) << 16) |
                                  (u32(p_indicies[i * 3 + 1]) << 8) |
                                  (u32(p_indicies[i * 3 + 2]));
          meshlet_vertex_and_index_indices.push(index_group);
        }
#endif  // NVIDIA
        meshlets.push(meshlet);
      }
      //
      while (meshlets.size % 32) meshlets.push(GPUMeshlet());
    }
  }

  HWARN("Scene meshlet count: {}", meshlets.size);

  current_images_count += gltf_scene.images_count;
  current_buffers_count += gltf_scene.buffer_views_count;
  current_samplers_count += gltf_scene.samplers_count;

  // qsort(transparent_meshes.data, transparent_meshes.size, sizeof(Mesh),
  // gltf_mesh_doublesided_compare); qsort(opaque_meshes.data,
  // opaque_meshes.size, sizeof(Mesh), gltf_mesh_doublesided_compare);
  node_pool.get_root_node()->update_transform(&node_pool);

  transparent_pass.double_sided_mesh_count += num_double_sided_meshes_t;
  gbuffer_pass.double_sided_mesh_count += num_double_sided_meshes;

  node_stack.shutdown();

  for (u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count;
       ++buffer_index) {
    void* buffer = buffers_data[buffer_index];
    resident_allocator->deallocate(buffer);  // TODO: NEEDED?
  }

  buffers_data.shutdown();

  temp_allocator->free_marker(temp_allocator_initial_marker);
}

void glTFScene::free_gpu_resources(Renderer* renderer) {
  GpuDevice& gpu = *renderer->gpu;

  // depth_pre_pass.free_gpu_resources();
  mesh_cull_pass.free_gpu_resources();
  mesh_cull_late_pass.free_gpu_resources();
  gbuffer_pass.free_gpu_resources();
  gbuffer_late_pass.free_gpu_resources();
  light_pass.free_gpu_resources();
  transparent_pass.free_gpu_resources();
  depth_pyramid_pass.free_gpu_resources();
  debug_pass.free_gpu_resources();

  for (u32 i = 0; i < k_max_frames; ++i) {
    gpu.destroy_descriptor_set(mesh_shader_descriptor_set[i]);
  }
  gpu.destroy_descriptor_set(fullscreen_ds);

  transparent_meshes.shutdown();
  opaque_meshes.shutdown();
}

void glTFScene::unload(Renderer* renderer) {
  GpuDevice& gpu = *renderer->gpu;

  destroy_node(node_pool.root_node);

  node_pool.shutdown();
  // Free scene buffers
  samplers.shutdown();
  images.shutdown();
  buffers.shutdown();

  meshlets.shutdown();
  meshlets_vertex_positions.shutdown();
  meshlets_vertex_data.shutdown();
  meshlet_vertex_and_index_indices.shutdown();
  lights.shutdown();

  // NOTE(marco): we can't destroy this sooner as textures and buffers
  // hold a pointer to the names stored here
  gltf_free(gltf_scene);

  names.shutdown();
}

void glTFScene::register_render_passes(FrameGraph* frame_graph_) {
  frame_graph = frame_graph_;

  // frame_graph->builder->register_render_pass("depth_pre_pass",
  // &depth_pre_pass);
  frame_graph->builder->register_render_pass("mesh_cull_early_pass",
                                             &mesh_cull_pass);
  frame_graph->builder->register_render_pass("mesh_cull_late_pass",
                                             &mesh_cull_late_pass);
  frame_graph->builder->register_render_pass("gbuffer_pass", &gbuffer_pass);
  frame_graph->builder->register_render_pass("gbuffer_late_pass",
                                             &gbuffer_late_pass);
  frame_graph->builder->register_render_pass("lighting_pass", &light_pass);
  frame_graph->builder->register_render_pass("transparent_pass",
                                             &transparent_pass);
  frame_graph->builder->register_render_pass("depth_pyramid_pass",
                                             &depth_pyramid_pass);
  frame_graph->builder->register_render_pass("debug_pass", &debug_pass);

  debug_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
}

void glTFScene::prepare_draws(Renderer* renderer,
                              StackAllocator* stack_allocator) {
  for (u32 i = 0; i < opaque_meshes.size; ++i) {
    Mesh& mesh = opaque_meshes[i];
    for (u32 m = 0; m < mesh.meshlet_count; ++m) {
      meshlets[m + mesh.meshlet_offset].mesh_index = i;
    }
  }

  for (u32 i = 0; i < transparent_meshes.size; ++i) {
    Mesh& mesh = transparent_meshes[i];
    for (u32 m = 0; m < mesh.meshlet_count; ++m) {
      meshlets[m + mesh.meshlet_offset].mesh_index = i + opaque_meshes.size;
    }
  }

  u32 total_meshes = opaque_meshes.size + transparent_meshes.size;

  BufferCreation buffer_creation;
  // Meshlets buffers
  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable,
           sizeof(GPUMeshlet) * meshlets.size)
      .set_name("meshlets_buffer")
      .set_data(meshlets.data);
  meshlets_buffer = renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(GPUMaterialData) * total_meshes)
      .set_name("material_data_buffer");
  material_data_buffer = renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(GPUMeshData) * total_meshes)
      .set_name("mesh_data_buffer");
  mesh_data_buffer = renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(GPUMeshInstanceData) * total_meshes)
      .set_name("mesh_instances_buffer");
  mesh_instances_buffer = renderer->create_buffer(buffer_creation)->handle;

  // Create mesh bound ssbo
  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
           sizeof(glm::vec4) * total_meshes)
      .set_name("mesh_bounds_buffer");
  mesh_bounds_buffer = renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable,
           sizeof(u32) * meshlet_vertex_and_index_indices.size)
      .set_name("meshlet_vertex_and_index_indices_buffer")
      .set_data(meshlet_vertex_and_index_indices.data);
  meshlet_vertex_and_index_indices_buffer =
      renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable,
           sizeof(GPUMeshletVertexPosition) * meshlets_vertex_positions.size)
      .set_name("meshlets_vertex_pos_buffer")
      .set_data(meshlets_vertex_positions.data);
  meshlets_vertex_pos_buffer = renderer->create_buffer(buffer_creation)->handle;

  buffer_creation.reset()
      .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable,
           sizeof(GPUMeshletVertexData) * meshlets_vertex_data.size)
      .set_name("meshlets_vertex_data_buffer")
      .set_data(meshlets_vertex_data.data);
  meshlets_vertex_data_buffer =
      renderer->create_buffer(buffer_creation)->handle;

  // Create indirect buffers, dynamic so need multiple buffering.
  for (u32 i = 0; i < k_max_frames; ++i) {
    char* name = renderer->resource_name_buffer.append_use_f(
        "mesh_indirect_draw_early_command_buffer_%d", i);
    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             ResourceUsageType::Dynamic,
             total_meshes * sizeof(GPUMeshDrawCommand))
        .set_name(name)
        .set_device_only(true);
    mesh_indirect_draw_early_command_buffers[i] =
        renderer->create_buffer(buffer_creation)->handle;

    name = renderer->resource_name_buffer.append_use_f(
        "mesh_indirect_draw_late_command_buffer_%d", i);
    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             ResourceUsageType::Dynamic,
             total_meshes * sizeof(GPUMeshDrawCommand))
        .set_name(name)
        .set_device_only(true);
    mesh_indirect_draw_late_command_buffers[i] =
        renderer->create_buffer(buffer_creation)->handle;

    name = renderer->resource_name_buffer.append_use_f(
        "mesh_draw_count_buffer_%d", i);
    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             ResourceUsageType::Dynamic, sizeof(GPUMeshDrawCounts))
        .set_name(name);
    mesh_draw_count_buffers[i] =
        renderer->create_buffer(buffer_creation)->handle;

    // TODO(marco): create buffer to track meshlet visibility
  }

  // Create debug draw buffers
  {
    static constexpr u32 k_max_lines =
        64000 + 64000;  // 3D + 2D lines in the same buffer
    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic,
             k_max_lines * sizeof(glm::vec4) * 2)
        .set_name("debug_lines_buffer");
    debug_line_buffer = renderer->create_buffer(buffer_creation)->handle;

    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             ResourceUsageType::Dynamic, sizeof(glm::ivec4))
        .set_name("debug_line_count_buffer");
    debug_line_count_buffer = renderer->create_buffer(buffer_creation)->handle;

    // Gather 3D and 2D gpu drawing commands
    buffer_creation.reset()
        .set(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             ResourceUsageType::Dynamic, sizeof(VkDrawIndirectCommand) * 2)
        .set_name("debug_line_commands_sb");
    debug_line_indirect_command_buffer =
        renderer->create_buffer(buffer_creation)->handle;
  }

  if (renderer->gpu->gpu_device_features & GpuDeviceFeature_MESH_SHADER) {
#if NVIDIA
    const u64 meshlet_hashed_name = hash_calculate("meshlet_nv");
#else
    const u64 meshlet_hashed_name = hash_calculate("meshlet_ext");
#endif  // NVIDIA
    Program* meshlet_program =
        renderer->resource_cache.programs.get(meshlet_hashed_name);

    DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout(
        meshlet_program->passes[1].pipeline, k_material_descriptor_set_index);

    for (u32 i = 0; i < k_max_frames; ++i) {
      DescriptorSetCreation ds_creation{};
      ds_creation.buffer(scene_constant_buffer, 0)
          .buffer(meshlets_buffer, 1)
          .buffer(material_data_buffer, 2)
          .buffer(mesh_data_buffer, 3)
          .buffer(meshlet_vertex_and_index_indices_buffer, 4)
          .buffer(meshlets_vertex_pos_buffer, 5)
          .buffer(meshlets_vertex_data_buffer, 6)
          .buffer(mesh_indirect_draw_early_command_buffers[i], 7)
          .buffer(mesh_draw_count_buffers[i], 8)
          .buffer(light_data_buffer, 9)
          .buffer(mesh_instances_buffer, 10)
          .buffer(mesh_bounds_buffer, 12)
          .buffer(debug_line_buffer, 20)
          .buffer(debug_line_count_buffer, 21)
          .buffer(debug_line_indirect_command_buffer, 22)
          .set_layout(layout);

      mesh_shader_descriptor_set[i] =
          renderer->gpu->create_descriptor_set(ds_creation);
    }
  }

  // depth_pre_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
  depth_pyramid_pass.prepare_draws(*this, frame_graph,
                                   renderer->gpu->allocator);

  mesh_cull_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
  mesh_cull_late_pass.prepare_draws(*this, frame_graph,
                                    renderer->gpu->allocator);
  gbuffer_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
  gbuffer_late_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
  light_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);
  transparent_pass.prepare_draws(*this, frame_graph, renderer->gpu->allocator);

  mesh_cull_pass.depth_pyramid_texture_index =
      depth_pyramid_pass.depth_pyramid.index;
  // gbuffer_pass.depth_pyramid_texture_index =
  // depth_pyramid_pass.depth_pyramid.index;
  mesh_cull_late_pass.depth_pyramid_texture_index =
      depth_pyramid_pass.depth_pyramid.index;
  // gbuffer_late_pass.depth_pyramid_texture_index =
  // depth_pyramid_pass.depth_pyramid.index;
}

void glTFScene::fill_pbr_material(Renderer& renderer, glTF::Material& material,
                                  PBRMaterial& pbr_material) {
  GpuDevice& gpu = *renderer.gpu;

  // Handle flags
  if (material.alpha_mode.data != nullptr &&
      strcmp(material.alpha_mode.data, "MASK") == 0) {
    pbr_material.flags |= DrawFlags_AlphaMask;
  } else if (material.alpha_mode.data != nullptr &&
             strcmp(material.alpha_mode.data, "BLEND") == 0) {
    pbr_material.flags |= DrawFlags_Transparent;
  }

  pbr_material.flags |= material.double_sided ? DrawFlags_DoubleSided : 0;
  // Alpha cutoff
  pbr_material.alpha_cutoff = material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE
                                  ? material.alpha_cutoff
                                  : 1.f;

  if (material.pbr_metallic_roughness != nullptr) {
    if (material.pbr_metallic_roughness->base_color_factor_count != 0) {
      HASSERT(material.pbr_metallic_roughness->base_color_factor_count == 4);

      memcpy(&pbr_material.base_color_factor.x,
             material.pbr_metallic_roughness->base_color_factor,
             sizeof(glm::vec4));
    } else {
      pbr_material.base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    pbr_material.roughness_metallic_occlusion_factor.x =
        material.pbr_metallic_roughness->roughness_factor !=
                glTF::INVALID_FLOAT_VALUE
            ? material.pbr_metallic_roughness->roughness_factor
            : 1.f;
    pbr_material.roughness_metallic_occlusion_factor.y =
        material.pbr_metallic_roughness->metallic_factor !=
                glTF::INVALID_FLOAT_VALUE
            ? material.pbr_metallic_roughness->metallic_factor
            : 1.f;

    pbr_material.diffuse_texture_index = get_material_texture(
        gpu, material.pbr_metallic_roughness->base_color_texture);
    pbr_material.roughness_texture_index = get_material_texture(
        gpu, material.pbr_metallic_roughness->metallic_roughness_texture);
  }

  pbr_material.occlusion_texture_index =
      get_material_texture(gpu, (material.occlusion_texture != nullptr)
                                    ? material.occlusion_texture->index
                                    : -1);
  pbr_material.normal_texture_index = get_material_texture(
      gpu, (material.normal_texture != nullptr) ? material.normal_texture->index
                                                : -1);

  if (material.occlusion_texture != nullptr) {
    if (material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE) {
      pbr_material.roughness_metallic_occlusion_factor.z =
          material.occlusion_texture->strength;
    } else {
      pbr_material.roughness_metallic_occlusion_factor.z = 1.0f;
    }
  }
}

u16 glTFScene::get_material_texture(GpuDevice& gpu,
                                    glTF::TextureInfo* texture_info) {
  if (texture_info != nullptr) {
    glTF::Texture& gltf_texture = gltf_scene.textures[texture_info->index];
    TextureResource& texture_gpu =
        images[gltf_texture.source + current_images_count];
    SamplerHandle sampler_gpu{};
    sampler_gpu = gpu.default_sampler;
    if (gltf_texture.sampler != 2147483647) {
      sampler_gpu =
          samplers[gltf_texture.sampler + current_samplers_count].handle;
    }

    gpu.link_texture_sampler(texture_gpu.handle, sampler_gpu);

    return (u16)texture_gpu.handle.index;
  } else {
    return (u16)k_invalid_index;
  }
}

u16 glTFScene::get_material_texture(GpuDevice& gpu, i32 gltf_texture_index) {
  if (gltf_texture_index >= 0) {
    glTF::Texture& gltf_texture = gltf_scene.textures[gltf_texture_index];
    TextureResource& texture_gpu =
        images[gltf_texture.source + current_images_count];
    SamplerHandle sampler_gpu{};
    sampler_gpu = gpu.default_sampler;
    if (gltf_texture.sampler != 2147483647) {
      sampler_gpu =
          samplers[gltf_texture.sampler + current_samplers_count].handle;
    }

    gpu.link_texture_sampler(texture_gpu.handle, sampler_gpu);

    return (u16)texture_gpu.handle.index;
  } else {
    return (u16)k_invalid_index;
  }
}

void glTFScene::fill_gpu_data_buffers(float model_scale) {
  // Update per mesh material buffer

  MapBufferParameters material_buffer_map = {material_data_buffer, 0, 0};
  GPUMaterialData* gpu_material_data =
      (GPUMaterialData*)renderer->gpu->map_buffer(material_buffer_map);

  MapBufferParameters mesh_buffer_map = {mesh_data_buffer, 0, 0};
  GPUMeshData* gpu_mesh_data =
      (GPUMeshData*)renderer->gpu->map_buffer(mesh_buffer_map);

  MapBufferParameters mesh_instance_map = {mesh_instances_buffer, 0, 0};
  GPUMeshInstanceData* gpu_mesh_instance_data =
      (GPUMeshInstanceData*)renderer->gpu->map_buffer(mesh_instance_map);

  if (gpu_material_data && gpu_mesh_data) {
    for (u32 mesh_index = 0; mesh_index < opaque_meshes.size; ++mesh_index) {
      copy_gpu_material_data(gpu_material_data[mesh_index],
                             opaque_meshes[mesh_index]);
      copy_gpu_mesh_data(gpu_mesh_data[mesh_index], opaque_meshes[mesh_index]);
      Mesh& mesh = opaque_meshes[mesh_index];

      MapBufferParameters material_buffer_map = {
          mesh.pbr_material.material_buffer, 0, 0};
      MeshData* mesh_data =
          (MeshData*)renderer->gpu->map_buffer(material_buffer_map);
      if (gpu_mesh_instance_data && mesh_data) {
        copy_gpu_mesh_matrix(*mesh_data, mesh, model_scale,
                             &node_pool.mesh_nodes);
        gpu_mesh_instance_data[mesh_index].world = mesh_data->model;
        gpu_mesh_instance_data[mesh_index].inverse_world =
            mesh_data->inverse_model;
        gpu_mesh_instance_data[mesh_index].mesh_index = mesh_index;
        renderer->gpu->unmap_buffer(material_buffer_map);
      }
    }
    for (u32 mesh_index = 0; mesh_index < transparent_meshes.size;
         ++mesh_index) {
      copy_gpu_material_data(gpu_material_data[mesh_index + opaque_meshes.size],
                             transparent_meshes[mesh_index]);
      copy_gpu_mesh_data(gpu_mesh_data[mesh_index + opaque_meshes.size],
                         transparent_meshes[mesh_index]);
      Mesh& mesh = transparent_meshes[mesh_index];

      MapBufferParameters material_buffer_map = {
          mesh.pbr_material.material_buffer, 0, 0};
      MeshData* mesh_data =
          (MeshData*)renderer->gpu->map_buffer(material_buffer_map);
      if (mesh_data) {
        copy_gpu_mesh_matrix(*mesh_data, mesh, model_scale,
                             &node_pool.mesh_nodes);
        gpu_mesh_instance_data[mesh_index + opaque_meshes.size].world =
            mesh_data->model;
        gpu_mesh_instance_data[mesh_index + opaque_meshes.size].inverse_world =
            mesh_data->inverse_model;
        gpu_mesh_instance_data[mesh_index + opaque_meshes.size].mesh_index =
            mesh_index + opaque_meshes.size;
        renderer->gpu->unmap_buffer(material_buffer_map);
      }
    }
    renderer->gpu->unmap_buffer(material_buffer_map);
    renderer->gpu->unmap_buffer(mesh_buffer_map);
    renderer->gpu->unmap_buffer(mesh_instance_map);
  }

  // Copy mesh bounding spheres
  mesh_buffer_map.buffer = mesh_bounds_buffer;
  glm::vec4* gpu_bounds_data =
      (glm::vec4*)renderer->gpu->map_buffer(mesh_buffer_map);
  if (gpu_bounds_data) {
    for (u32 mesh_index = 0; mesh_index < opaque_meshes.size; ++mesh_index) {
      gpu_bounds_data[mesh_index] = opaque_meshes[mesh_index].bounding_sphere;
    }
    for (u32 mesh_index = 0; mesh_index < transparent_meshes.size;
         ++mesh_index) {
      gpu_bounds_data[mesh_index + opaque_meshes.size] =
          transparent_meshes[mesh_index].bounding_sphere;
    }
    renderer->gpu->unmap_buffer(mesh_buffer_map);
  }

  mesh_buffer_map.buffer = light_data_buffer;
  GPULight* lights_data = (GPULight*)renderer->gpu->map_buffer(mesh_buffer_map);
  if (lights_data) {
    for (u32 i = 0; i < node_pool.light_nodes.used_indices; ++i) {
      glm::vec3 light_pos =
          ((LightNode*)node_pool.access_node({i, NodeType::LightNode}))
              ->world_transform.translation;
      lights[i].position = glm::vec4(light_pos, light_texture.handle.index);
      lights_data[i] = lights[i];
    }
    renderer->gpu->unmap_buffer(mesh_buffer_map);
  }

  light_pass.fill_gpu_material_buffer();
}

void glTFScene::submit_draw_task(ImGuiService* imgui, GPUProfiler* gpu_profiler,
                                 enki::TaskScheduler* task_scheduler) {
  glTFDrawTask draw_task;
  draw_task.init(renderer->gpu, frame_graph, renderer, imgui, gpu_profiler,
                 this);
  task_scheduler->AddTaskSetToPipe(&draw_task);
  task_scheduler->WaitforTask(&draw_task);
  // Avoid using the same command buffer
  renderer->add_texture_update_commands((draw_task.thread_id + 1) %
                                        task_scheduler->GetNumTaskThreads());
}

void glTFScene::draw_mesh(CommandBuffer* gpu_commands, Mesh& mesh) {
  gpu_commands->bind_vertex_buffer(mesh.position_buffer, 0,
                                   mesh.position_offset);
  gpu_commands->bind_vertex_buffer(mesh.tangent_buffer, 1, mesh.tangent_offset);
  gpu_commands->bind_vertex_buffer(mesh.normal_buffer, 2, mesh.normal_offset);
  gpu_commands->bind_vertex_buffer(mesh.texcoord_buffer, 3,
                                   mesh.texcoord_offset);
  gpu_commands->bind_index_buffer(mesh.index_buffer, mesh.index_offset,
                                  mesh.index_type);

  gpu_commands->bind_descriptor_set(&mesh.pbr_material.descriptor_set, 1,
                                    nullptr, 0);

  gpu_commands->draw_indexed(mesh.primitive_count, 1, 0, 0, 0);
}

void glTFScene::destroy_node(NodeHandle handle) {
  Node* node = (Node*)node_pool.access_node(handle);
  for (u32 i = 0; i < node->children.size; i++) {
    destroy_node(node->children[i]);
  }
  node->children.shutdown();
  switch (handle.type) {
    case NodeType::Node:
      node_pool.base_nodes.release_resource(handle.index);
      break;
    case NodeType::MeshNode:
      node_pool.mesh_nodes.release_resource(handle.index);
      break;
    case NodeType::LightNode:
      node_pool.light_nodes.release_resource(handle.index);
      break;
    default:
      HERROR("Invalid NodeType");
      break;
  }
}

void glTFScene::imgui_draw_node_property(NodeHandle node_handle) {
  if (ImGui::Begin("Node Properties")) {
    if (node_handle.index == k_invalid_index) {
      ImGui::Text("No node selected");
    } else {
      Node* node = (Node*)node_pool.access_node(node_handle);
      ImGui::Text("Name: %s", node->name);
      ImGui::Text("Type: %s", node_type_to_cstring(node_handle.type));

      if (!(node->parent.index == k_invalid_index)) {
        Node* parent = (Node*)node_pool.access_node(node->parent);
        ImGui::Text("Parent: %s", parent->name);
      }

      bool modified = false;

      glm::vec3 local_rotation =
          glm::degrees(glm::eulerAngles(node->local_transform.rotation));
      glm::vec3 world_rotation =
          glm::degrees(glm::eulerAngles(node->world_transform.rotation));

      // TODO: Represent rotation as quats
      ImGui::Text("Local Transform");
      modified |= ImGui::InputFloat3(
          "position##local", (float*)&node->local_transform.translation);
      modified |= ImGui::InputFloat3("scale##local",
                                     (float*)&node->local_transform.scale);
      modified |=
          ImGui::InputFloat3("rotation##local", (float*)&local_rotation);

      ImGui::Text("World Transform");
      ImGui::InputFloat3("position##world",
                         (float*)&node->world_transform.translation);
      ImGui::InputFloat3("scale##world", (float*)&node->world_transform.scale);
      ImGui::InputFloat3("rotation##world", (float*)&world_rotation);

      if (node_handle.type == NodeType::LightNode) {
        LightNode* light_node = (LightNode*)node;
        GPULight& light = lights[light_node->light_index];
        ImGui::SliderFloat("Light Intensity", &light.intensity, 0.f, 100.f);
        ImGui::SliderFloat("Light Range", &light.range, 0.f, 100.f);
      }

      if (modified) {
        node->local_transform.rotation =
            glm::quat(glm::radians(local_rotation));
        node->update_transform(&node_pool);
      }
    }
  }
  ImGui::End();
}

void glTFScene::add_light() {
  NodeHandle light_node_handle = node_pool.obtain_node(NodeType::LightNode);

  LightNode* light_node = (LightNode*)node_pool.access_node(light_node_handle);
  light_node->name = names.append_use_f("Point Light_%d",
                                        node_pool.light_nodes.used_indices - 1);
  light_node->local_transform.scale = {1.0f, 1.0f, 1.0f};
  light_node->world_transform.scale = {1.0f, 1.0f, 1.0f};
  light_node->world_transform.translation.y = 1.0f;
  light_node->light_index = lights.size;

  node_pool.get_root_node()->add_child(light_node);

  GPULight light;
  light.range = 50.f;
  light.intensity = 50.f;
  light.position.w = light_texture.handle.index;
  lights.push(light);
}

void glTFScene::imgui_draw_node(NodeHandle node_handle) {
  Node* node = (Node*)node_pool.access_node(node_handle);

  if (node->name == nullptr) return;
  // Make a tree node for nodes with children
  ImGuiTreeNodeFlags tree_node_flags = 0;
  tree_node_flags |=
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (!node->children.size) {
    tree_node_flags |= ImGuiTreeNodeFlags_Leaf;
  }
  if (ImGui::TreeNodeEx(node->name, tree_node_flags)) {
    if (current_node != node_handle && ImGui::IsItemClicked()) {
      current_node.index = node_handle.index;
      current_node.type = node_handle.type;
    }

    for (u32 i = 0; i < node->children.size; i++) {
      imgui_draw_node(node->children[i]);
    }
    ImGui::TreePop();
  }
}

void glTFScene::imgui_draw_hierarchy() {
  if (ImGui::Begin("Scene Hierarchy")) {
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    if (ImGui::Button("Add GLTF model", {viewportPanelSize.x, 30})) {
      char* file_path = nullptr;
      char* filename = nullptr;
      if (file_open_dialog(file_path, filename)) {
        HDEBUG("Found file!, {}, Oath: {}", filename, file_path);

        Directory cwd{};
        directory_current(&cwd);

        directory_change(file_path);

        load(filename, file_path, main_allocator, scratch_allocator, loader);

        directory_change(cwd.path);

        prepare_draws(renderer, scratch_allocator);

        directory_change(cwd.path);

        delete[] filename, file_path;
      }
    }
    if (ImGui::Button("Add Light", {viewportPanelSize.x, 30})) {
      add_light();
    }

    imgui_draw_node(node_pool.root_node);
    ImGui::End();
  }
}

// Nodes //////////////////////////////////////////

}  // namespace Helix
