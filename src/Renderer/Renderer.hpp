#pragma once

#include "Core/ResourceManager.hpp"
#include "Renderer/GPUDevice.hpp"
#include "Renderer/GPUResources.hpp"

namespace Helix {

struct Renderer;

//
//
struct BufferResource : public Helix::Resource {
  BufferHandle handle;
  u32 pool_index;
  BufferDescription desc;

  static constexpr cstring k_type = "helix_buffer_type";
  static u64 k_type_hash;

};  // struct Buffer

//
//
struct TextureResource : public Helix::Resource {
  TextureHandle handle;
  u32 pool_index;
  TextureDescription desc;

  static constexpr cstring k_type = "helix_texture_type";
  static u64 k_type_hash;

};  // struct Texture

//
//
struct SamplerResource : public Helix::Resource {
  SamplerHandle handle;
  u32 pool_index;
  SamplerDescription desc;

  static constexpr cstring k_type = "helix_sampler_type";
  static u64 k_type_hash;

};  // struct Sampler

// Material/Shaders ///////////////////////////////////////////////////////

//
//
struct ProgramPass {
  PipelineHandle pipeline;
  DescriptorSetLayoutHandle descriptor_set_layout;
};  // struct ProgramPass

//
//
struct ProgramCreation {
  PipelineCreation creations[8];
  u32 num_creations = 0;

  cstring name = nullptr;

  ProgramCreation& reset();
  ProgramCreation& add_pipeline(const PipelineCreation& pipeline);
  ProgramCreation& set_name(cstring name);

};  // struct ProgramCreation

//
//
struct Program : public Helix::Resource {
  Array<ProgramPass> passes;
  FlatHashMap<u64, u16> name_hash_to_index;

  u32 pool_index;

  u32 get_pass_index(cstring name);

  static constexpr cstring k_type = "helix_program_type";
  static u64 k_type_hash;

};  // struct Program

//
//
struct MaterialCreation {
  MaterialCreation& reset();
  MaterialCreation& set_program(Program* program);
  MaterialCreation& set_name(cstring name);
  MaterialCreation& set_render_index(u32 render_index);

  Program* program = nullptr;
  cstring name = nullptr;
  u32 render_index = ~0u;

};  // struct MaterialCreation

//
//
struct Material : public Helix::Resource {
  Program* program;

  u32 render_index;

  u32 pool_index;

  static constexpr cstring k_type = "helix_material_type";
  static u64 k_type_hash;

};  // struct Material

// ResourceCache
// ////////////////////////////////////////////////////////////////

//
//
struct ResourceCache {
  void init(Allocator* allocator);
  void shutdown(Renderer* renderer);

  FlatHashMap<u64, TextureResource*> textures;
  FlatHashMap<u64, BufferResource*> buffers;
  FlatHashMap<u64, SamplerResource*> samplers;
  FlatHashMap<u64, Program*> programs;
  FlatHashMap<u64, Material*> materials;

};  // struct ResourceCache

// Renderer
// /////////////////////////////////////////////////////////////////////
struct RendererCreation {
  Helix::GpuDevice* gpu;
  Allocator* allocator;

};  // struct RendererCreation

//
// Main class responsible for handling all high level resources
//
struct Renderer : public Service {
  HELIX_DECLARE_SERVICE(Renderer);

  void init(const RendererCreation& creation);
  void shutdown();

  void set_loaders(Helix::ResourceManager* manager);

  void begin_frame();
  void end_frame();

  void imgui_draw();
  void imgui_resources_draw();

  void resize_swapchain(u32 width, u32 height);

  f32 aspect_ratio() const;

  // Creation/destruction
  BufferResource* create_buffer(const BufferCreation& creation);
  BufferResource* create_buffer(VkBufferUsageFlags type,
                                ResourceUsageType::Enum usage, u32 size,
                                void* data, cstring name);

  TextureResource* create_texture(const TextureCreation& creation);
  TextureResource* create_texture(cstring name, cstring filename,
                                  bool create_mipmaps);

  SamplerResource* create_sampler(const SamplerCreation& creation);

  Program* create_program(const ProgramCreation& creation);

  Material* create_material(const MaterialCreation& creation);

  // Draw
  PipelineHandle get_pipeline(Material* material, u32 pass_index);
  DescriptorSetHandle create_descriptor_set(CommandBuffer* command_buffer,
                                            Material* material,
                                            DescriptorSetCreation& ds_creation);

  void destroy_buffer(BufferResource* buffer);
  void destroy_texture(TextureResource* texture);
  void destroy_sampler(SamplerResource* sampler);
  void destroy_program(Program* program);
  void destroy_material(Material* material);

  // Update resources
  void* map_buffer(BufferResource* buffer, u32 offset = 0, u32 size = 0);
  void unmap_buffer(BufferResource* buffer);

  CommandBuffer* get_command_buffer(u32 thread_index, bool begin) {
    return gpu->get_command_buffer(thread_index, begin);
  }
  void queue_command_buffer(Helix::CommandBuffer* commands) {
    gpu->queue_command_buffer(commands);
  }

  // Multithread friendly update to textures
  void add_texture_to_update(Helix::TextureHandle texture);
  void add_texture_update_commands(u32 thread_id);

  f64 fps = 0;

  std::mutex texture_update_mutex;

  ResourcePoolTyped<TextureResource> textures;
  ResourcePoolTyped<BufferResource> buffers;
  ResourcePoolTyped<SamplerResource> samplers;
  ResourcePoolTyped<Program> programs;
  ResourcePoolTyped<Material> materials;

  ResourceCache resource_cache;

  TextureHandle textures_to_update[128];
  u32 num_textures_to_update = 0;

  StringBuffer resource_name_buffer;

  Helix::GpuDevice* gpu;

  Array<VmaBudget> gpu_heap_budgets;

  u16 width;
  u16 height;

  static constexpr cstring k_name = "helix_rendering_service";

};  // struct Renderer

}  // namespace Helix
