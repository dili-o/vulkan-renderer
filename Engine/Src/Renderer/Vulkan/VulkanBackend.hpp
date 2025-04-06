#pragma once

#include "CommandBuffer.hpp"
#include "Containers/Array.hpp"
#include "Containers/ResourcePool.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererTypes.hpp"
#include "VulkanTypes.hpp"

namespace Helix {

struct ResourceQueueObject {
  VkObjectType type;
  ResourceHandle handle;
  cstring name{nullptr};
};

struct VulkanBackend : public RendererBackend {
  virtual bool init(void *config) override;
  virtual bool shutdown() override;
  virtual bool on_resize(u16 width, u16 height) override;
  virtual bool begin_frame(RenderPacket *packet) override;
  virtual bool end_frame(RenderPacket *packet) override;

  void create_swapchain();
  void destroy_swapchain();
  void resize_swapchain();

  void record_command_buffer(VulkanCommandBuffer *command_buffer,
                             RenderPacket *packet, u32 image_index,
                             u32 current_frame);

  void vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VulkanBuffer &buffer);
  virtual BufferHandle create_buffer(BufferCreation &creation) override;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) override;
  // NOTE: Internal TextureResource handle is a VulkanImageView
  virtual TextureHandle create_texture(TextureCreation &creation) override;
  ImageViewHandle create_image_view(TextureCreation &creation);
  ImageHandle create_image(TextureCreation &creation);
  virtual bool update_shader_uniform_set(ShaderUniformSet &set,
                                         PipelineHandle pipeline) override;
  void create_descriptor_pool(u32 max_frames_in_flight);
  void create_sync_objects(u32 max_frames_in_flight);
  SamplerHandle create_sampler(SamplerCreation &creation);

  VulkanBuffer *access_buffer(BufferHandle handle);
  VulkanPipeline *access_pipeline(PipelineHandle handle);
  VulkanDescriptorSetLayout *
  access_descriptor_set_layout(DescriptorSetLayoutHandle handle);
  VulkanDescriptorSet *access_descriptor_set(DescriptorSetHandle handle);
  VulkanImage *access_image(TextureHandle handle);
  VulkanImageView *access_image_view(TextureHandle handle);
  VulkanSampler *access_sampler(SamplerHandle handle);

  virtual void destroy_buffer(BufferHandle handle) override;
  virtual void destroy_pipeline(PipelineHandle handle) override;
  virtual void destroy_texture(TextureHandle handle) override;
  void destroy_image(TextureHandle handle);
  void destroy_image_view(TextureHandle handle);
  void destroy_descriptor_set_layout(DescriptorSetLayoutHandle handle);
  void destroy_sampler(SamplerHandle handle);

  void destroy_buffer_instant(BufferHandle handle);
  void destroy_pipeline_instant(PipelineHandle handle);
  void destroy_descriptor_set_layout_instant(DescriptorSetLayoutHandle handle);
  void destroy_image_instant(TextureHandle handle);
  void destroy_image_view_instant(TextureHandle handle);
  void destroy_sampler_instant(SamplerHandle handle);

  void free_queued_resources();

  void upload_buffer_data(void *data, VkBuffer dst_buffer, u32 size);

  void draw_frame(RenderPacket *packet, u32 image_index);
  // TODO: Move this to the RendererFrontend
  void update_uniform_buffer(RenderPacket *packet);

  void set_resource_name(VkObjectType type, u64 handle, cstring name);

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VmaAllocator vma_allocator{};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkDevice vk_device{VK_NULL_HANDLE};
  VkQueue vk_graphics_queue{VK_NULL_HANDLE};
  VkQueue vk_transfer_queue{VK_NULL_HANDLE};

  QueueFamilyIndices queue_family_indices{};

  CommandBufferManager command_buffer_manager{};
  CommandBufferManager transfer_command_buffer_manager{};

  Array<VkSemaphore> image_available_semaphores;
  Array<VkSemaphore> render_finished_semaphores;
  VkSemaphore vk_timeline_graphics_semaphore{VK_NULL_HANDLE};
  // Array<VkFence> in_flight_fences;
  SamplerHandle default_sampler{};

  VulkanSwapchain swapchain{};

  VkDescriptorPool vk_descriptor_pool{VK_NULL_HANDLE};
  TextureHandle depth_handle{};

  ResourcePool<VulkanBuffer> buffers{};
  ResourcePool<VulkanPipeline> pipelines{};
  ResourcePool<VulkanDescriptorSetLayout> descriptor_set_layouts{};
  ResourcePool<VulkanDescriptorSet> descriptor_sets{};
  ResourcePool<VulkanImageView> image_views{};
  ResourcePool<VulkanImage> images{};
  ResourcePool<VulkanSampler> samplers{};

  Array<ResourceQueueObject> resource_deletion_queue{};

  bool resize_frame = false;
};
} // namespace Helix
