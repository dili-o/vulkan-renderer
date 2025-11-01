#include "CommandBuffer.hpp"
#include "Core/String.hpp"
#include "Renderer/RendererBackend.hpp"

namespace hlx {
struct VkGpuDevice final : public GpuDevice {
  virtual u32 create_backbuffers(u32 width, u32 height, u32 count) override;
  virtual void process_display_changes() override;

  // Resource Creation
  virtual BufferHandle create_buffer(const BufferCreation &creation) override;
  virtual TextureHandle
  create_texture(const TextureCreation &creation) override;
  VkImageHandle create_image(const TextureCreation &creation);
  VkImageViewHandle create_image_view(const TextureCreation &creation,
                                      VkImageHandle image);
  virtual SamplerHandle
  create_sampler(const SamplerCreation &creation) override;
  virtual BindingSetHandle
  create_binding_set(const BindingSetCreation &creation) override;
  virtual BindingSetLayoutHandle
  create_binding_set_layout(const BindingSetLayoutCreation &creation) override;
  virtual PipelineHandle
  create_pipeline(const PipelineCreation &creation) override;
  virtual RenderPassHandle
  create_render_pass(const RenderPassCreation &creation) override;

  // Resource Deletion
  virtual void destroy_buffer(BufferHandle handle) override;
  virtual void destroy_texture(TextureHandle handle) override;
  void destroy_image(VkImageHandle handle);
  void destroy_image_view(VkImageViewHandle handle);
  virtual void destroy_sampler(SamplerHandle handle) override;
  virtual void destroy_binding_set(BindingSetHandle handle) override;
  virtual void
  destroy_binding_set_layout(BindingSetLayoutHandle handle) override;
  virtual void destroy_pipeline(PipelineHandle handle) override;
  virtual void destroy_render_pass(RenderPassHandle handle) override;

  virtual void resize_texture(TextureHandle handle, u32 width,
                              u32 height) override;

  virtual void destroy_receipt(WorkReceipt *receipt) override;
  virtual void destroy_context(Context *context) override;

  void destroy_buffer_instant(BufferHandle handle);
  void destroy_image_instant(VkImageHandle handle);
  void destroy_image_view_instant(VkImageViewHandle handle);
  void destroy_sampler_instant(SamplerHandle handle);
  void destroy_binding_set_layout_instant(BindingSetLayoutHandle handle);
  void destroy_pipeline_instant(PipelineHandle handle);

  void free_queued_resources();

  virtual PipelineInfo access_pipeline_view(PipelineHandle handle) override;

  virtual Context *create_context(ContextType::Enum type) override;
  virtual WorkReceipt *create_receipt() override;

  virtual u32 get_next_image_index(Context *context,
                                   u32 current_frame_in_flight) override;
  virtual TextureHandle get_backbuffer_texture(u32 index) override;
  virtual bool update_binding_set(BindingSetHandle set,
                                  BindingSetUpdateInfo *update_infos,
                                  u32 update_count) override;
  virtual void submit_work(Context *context, WorkReceipt *receipt) override;
  virtual void wait_on_work(WorkReceipt *receipt) override;
  virtual void present_to_display() override;
  virtual void set_render_pass_texture(RenderPassHandle handle,
                                       TextureHandle texture_handle,
                                       bool is_depth, u32 index = 0) override;

  virtual void *get_buffer_map(BufferHandle handle) override;
  virtual void resize_backbuffers() override;

  void set_resource_name(VkObjectType type, u64 handle, cstring name);
  void create_swapchain();
  void destroy_swapchain();
  void resize_swapchain();

  inline VulkanBuffer *access_buffer(BufferHandle handle) {
    return buffers.obtain(handle);
  }
  inline VulkanImage *access_image(TextureHandle handle) {
    return images.obtain(handle);
  }
  inline VulkanImageView *access_image_view(TextureHandle handle) {
    return image_views.obtain(handle);
  }
  inline VulkanSampler *access_sampler(SamplerHandle handle) {
    return samplers.obtain(handle);
  }
  inline VulkanDescriptorSet *access_descriptor_set(BindingSetHandle handle) {
    return descriptor_sets.obtain(handle);
  }
  inline VulkanDescriptorSetLayout *
  access_descriptor_set_layout(BindingSetLayoutHandle handle) {
    return descriptor_set_layouts.obtain(handle);
  }
  inline VulkanPipeline *access_pipeline(PipelineHandle handle) {
    return pipelines.obtain(handle);
  }
  inline RenderPass *access_render_pass(RenderPassHandle handle) {
    return render_passes.obtain(handle);
  }

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VmaAllocator vma_allocator{};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkPhysicalDeviceVulkan11Properties vk_physical_device_vulkan11_properties{};
  VkPhysicalDeviceProperties2 vk_physical_device_properties2{};
  VkDevice vk_device{VK_NULL_HANDLE};
  QueueFamilyIndices queue_family_indices{};
  VkQueue vk_graphics_queue{VK_NULL_HANDLE};
  VkQueue vk_transfer_queue{VK_NULL_HANDLE};
  VkQueue vk_compute_queue{VK_NULL_HANDLE};
  PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT;
  PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugUtilsLabelEXT;
  PFN_vkCmdInsertDebugUtilsLabelEXT pfnCmdInsertDebugUtilsLabelEXT;
  PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugUtilsLabelEXT;
  Array<VkSemaphore> vk_image_available_semaphores;
  Array<VkSemaphore> vk_render_finished_semaphores;
  VkSemaphore vk_timeline_semaphore{VK_NULL_HANDLE};
  VkDescriptorPool vk_bindless_pool{VK_NULL_HANDLE};
  VkDescriptorPool vk_descriptor_pool{VK_NULL_HANDLE};

  VulkanSwapchain swapchain{};

  ResourcePool<VulkanBuffer> buffers{};
  ResourcePool<VulkanImageView> image_views{};
  ResourcePool<VulkanImage> images{};
  ResourcePool<VulkanSampler> samplers{};
  ResourcePool<VulkanDescriptorSet> descriptor_sets{};
  ResourcePool<VulkanDescriptorSetLayout> descriptor_set_layouts{};
  ResourcePool<VulkanPipeline> pipelines{};
  ResourcePool<RenderPass> render_passes{};

  Array<ResourceQueueObject> resource_deletion_queue{};
  StringBuffer string_buffer{};
};

GpuDevice *create_vulkan_device();
void destroy_vulkan_device(VkGpuDevice *device);

} // namespace hlx
