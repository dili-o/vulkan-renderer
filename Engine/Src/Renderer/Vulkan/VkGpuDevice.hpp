#include "CommandBuffer.hpp"
#include "Renderer/RendererBackend.hpp"

namespace Helix {
struct VkGpuDevice final : public GpuDevice {
  virtual u32 create_backbuffers(u32 width, u32 height, u32 count) override;
  virtual void process_display_changes() override;
  virtual void create_buffer() override;
  virtual void create_texture() override;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) override;
  virtual RenderPassHandle
  create_render_pass(const RenderPassCreation &creation) override;
  virtual Context *create_context(ContextType::Enum type) override;
  virtual WorkReceipt *create_receipt() override;

  virtual PipelineInfo access_pipeline_view(PipelineHandle handle) override;

  virtual void destroy_render_pass(RenderPassHandle handle) override;
  virtual void destroy_pipeline(PipelineHandle handle) override;
  virtual void destroy_receipt(WorkReceipt *receipt) override;
  virtual void destroy_context(Context *context) override;
  void destroy_pipeline_instant(PipelineHandle handle);

  void free_queued_resources();

  virtual u32 get_next_image_index(Context *context,
                                   u32 current_frame_in_flight) override;
  virtual TextureHandle get_backbuffer_texture(u32 index) override;
  virtual void submit_work(Context *context, WorkReceipt *receipt) override;
  virtual void wait_on_work(WorkReceipt *receipt) override;
  virtual void present_to_display() override;
  virtual void set_render_pass_texture(RenderPassHandle handle,
                                       TextureHandle texture_handle,
                                       bool is_depth, u32 index = 0) override;

  virtual void resize_backbuffers() override;

  void set_resource_name(VkObjectType type, u64 handle, cstring name);
  void create_swapchain();
  void destroy_swapchain();
  void resize_swapchain();

  inline VulkanImage *access_image(TextureHandle handle) {
    return images.obtain(handle);
  }
  inline VulkanImageView *access_image_view(TextureHandle handle) {
    return image_views.obtain(handle);
  }
  inline RenderPass *access_render_pass(RenderPassHandle handle) {
    return render_passes.obtain(handle);
  }
  inline VulkanPipeline *access_pipeline(PipelineHandle handle) {
    return pipelines.obtain(handle);
  }

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VmaAllocator vma_allocator{};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkPhysicalDeviceProperties vk_physical_device_properties{};
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

  VulkanSwapchain swapchain{};
  Array<ResourceQueueObject> resource_deletion_queue{};

  ResourcePool<VulkanImageView> image_views{};
  ResourcePool<VulkanImage> images{};
  ResourcePool<RenderPass> render_passes{};
  ResourcePool<VulkanPipeline> pipelines{};
};

GpuDevice *create_vulkan_device();
void destroy_vulkan_device(VkGpuDevice *device);

} // namespace Helix
