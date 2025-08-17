#pragma once

#include "Containers/ResourcePool.hpp"
#include "Core/String.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/CommandBuffer.hpp"
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
  virtual void render_frame(RenderPacket *packet) override;
  virtual bool end_frame(RenderPacket *packet) override;

  void create_swapchain();
  void destroy_swapchain();
  void resize_swapchain();

  void record_command_buffer(VulkanCommandBuffer *command_buffer,
                             RenderPacket *packet, u32 current_frame);

  void vma_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VulkanBuffer &buffer);
  void vma_create_image(const VkImageCreateInfo *create_info,
                        VkImage *image_handle, VmaAllocation *image_allocation);
  virtual BufferHandle create_buffer(BufferCreation &creation) override;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) override;
  // NOTE: Internal TextureResource handle is a VulkanImageView
  virtual TextureHandle create_texture(TextureCreation &creation) override;
  ImageViewHandle create_image_view(TextureCreation &creation);
  ImageHandle create_image(TextureCreation &creation);

  inline virtual BindingSetLayoutHandle
  create_binding_set_layout(BindingSetLayoutCreation &creation) override {
    return create_descriptor_set_layout(creation);
  }
  inline virtual BindingSetHandle
  create_binding_set(BindingSetCreation &creation) override {
    return create_descriptor_set(creation);
  }

  BindingSetLayoutHandle
  create_descriptor_set_layout(BindingSetLayoutCreation &creation);
  BindingSetHandle create_descriptor_set(BindingSetCreation &creation);

  virtual bool update_binding_set(BindingSetHandle set,
                                  BindingSetUpdateInfo *update_infos,
                                  u32 update_count) override;
  virtual RenderPassHandle get_swapchain_pass() override;

  virtual void set_pipeline_binding_set(PipelineHandle pipeline,
                                        BindingSetHandle set,
                                        u32 set_index) override;
  virtual RenderPassHandle
  create_render_pass(RenderPassCreation &creation) override;
  void create_descriptor_pool(u32 max_frames_in_flight);
  void create_sync_objects(u32 swapchain_image_count);
  SamplerHandle create_sampler(SamplerCreation &creation);

  VulkanBuffer *access_buffer(BufferHandle handle);
  VulkanPipeline *access_pipeline(PipelineHandle handle);
  VulkanDescriptorSetLayout *
  access_descriptor_set_layout(DescriptorSetLayoutHandle handle);
  VulkanDescriptorSet *access_descriptor_set(DescriptorSetHandle handle);
  VulkanImage *access_image(TextureHandle handle);
  VulkanImageView *access_image_view(TextureHandle handle);
  VulkanSampler *access_sampler(SamplerHandle handle);
  RenderPass *access_render_pass(RenderPassHandle handle);

  virtual BufferInfo access_buffer_view(BufferHandle handle) override;
  virtual TextureInfo access_texture_view(TextureHandle handle) override;
  virtual PipelineInfo access_pipeline_view(PipelineHandle handle) override;

  virtual void destroy_buffer(BufferHandle handle) override;
  virtual void destroy_pipeline(PipelineHandle handle) override;
  virtual void destroy_texture(TextureHandle handle) override;
  inline virtual void destroy_binding_set(BindingSetHandle handle) override {
    destroy_descriptor_set(handle);
  }
  virtual void destroy_render_pass(RenderPassHandle handle) override;
  void destroy_image(TextureHandle handle);
  void destroy_image_view(TextureHandle handle);
  void destroy_descriptor_set_layout(DescriptorSetLayoutHandle handle);
  void destroy_descriptor_set(DescriptorSetHandle handle);
  void destroy_sampler(SamplerHandle handle);

  void destroy_buffer_instant(BufferHandle handle);
  void destroy_pipeline_instant(PipelineHandle handle);
  void destroy_descriptor_set_layout_instant(DescriptorSetLayoutHandle handle);
  void destroy_descriptor_set_instant(DescriptorSetHandle handle);
  void destroy_image_instant(TextureHandle handle);
  void destroy_image_view_instant(TextureHandle handle);
  void destroy_sampler_instant(SamplerHandle handle);

  void resize_texture(TextureHandle handle, u32 width, u32 height);

  void free_queued_resources();

  virtual void upload_buffer_data(void *data, BufferHandle dst_buffer, u64 size,
                                  u64 offset) override;
  virtual void upload_to_image(void *data, TextureHandle dst_image) override;

  void update_uniform_buffer(RenderPacket *packet);

  virtual void update_draw_commands(Scene *scene) override;
  virtual void print_gpu_stats() override;

  void set_resource_name(VkObjectType type, u64 handle, cstring name);

  StringBuffer string_buffer{};

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VmaAllocator vma_allocator{};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;
  PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
  PFN_vkCmdInsertDebugUtilsLabelEXT pfnCmdInsertDebugUtilsLabelEXT;
  PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkPhysicalDeviceProperties vk_physical_device_properties{};
  VkDevice vk_device{VK_NULL_HANDLE};
  VkQueue vk_graphics_queue{VK_NULL_HANDLE};
  VkQueue vk_compute_queue{VK_NULL_HANDLE};
  VkQueue vk_transfer_queue{VK_NULL_HANDLE};

  QueueFamilyIndices queue_family_indices{};

  CommandBufferManager command_buffer_manager{};
  CommandBufferManager transfer_command_buffer_manager{};

  Array<VkSemaphore> image_available_semaphores;
  Array<VkSemaphore> render_finished_semaphores;
  VkSemaphore vk_timeline_graphics_semaphore{VK_NULL_HANDLE};

  VulkanSwapchain swapchain{};

  VkDescriptorPool vk_descriptor_pool{VK_NULL_HANDLE};
  VkDescriptorPool vk_bindless_descriptor_pool{VK_NULL_HANDLE};

  Array<TextureHandle> bindless_textures_to_update{};
  Array<ResourceQueueObject> resource_deletion_queue{};

  TextureHandle depth_images[max_frames_in_flight];
  SamplerHandle default_sampler{};
  SamplerHandle default_u32_sampler{};
  RenderPassHandle swapchain_pass{};

  ResourcePool<VulkanBuffer> buffers{};
  ResourcePool<VulkanPipeline> pipelines{};
  ResourcePool<VulkanDescriptorSetLayout> descriptor_set_layouts{};
  ResourcePool<VulkanDescriptorSet> descriptor_sets{};
  ResourcePool<VulkanImageView> image_views{};
  ResourcePool<VulkanImage> images{};
  ResourcePool<VulkanSampler> samplers{};
  ResourcePool<RenderPass> render_passes{};

  BufferHandle indirect_draw_buffer{};

  bool resize_frame = false;
};
} // namespace Helix
