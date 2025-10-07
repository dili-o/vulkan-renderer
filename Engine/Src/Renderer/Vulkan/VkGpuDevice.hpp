#include "CommandBuffer.hpp"
#include "Renderer/RendererBackend.hpp"
#include "VulkanTypes.hpp"

namespace Helix {
struct VkGpuDevice final : public GpuDevice {
  virtual u32 create_backbuffers(u32 width, u32 height, u32 count) override;
  virtual void process_display_changes() override;
  virtual void create_buffer() override;
  virtual void create_texture() override;
  virtual void create_pipeline() override;
  virtual GraphicsContext *create_graphics_context() override;
  virtual void destroy_graphics_context(Context *context) override;

  virtual void submit_work(Context *context) override;
  virtual void wait_on_work() override;
  virtual void present_to_display() override;

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
  VulkanSwapchain swapchain{};

  ResourcePool<VulkanImageView> image_views{};
  ResourcePool<VulkanImage> images{};

  VkCommandPool vk_command_pool;
  VulkanCommandBuffer command_buffer;
};

GpuDevice *create_vulkan_device();
void destroy_vulkan_device(VkGpuDevice *device);

struct VkGraphicsContext final : public GraphicsContext {
  virtual void begin() override;
  virtual void end() override;
  virtual void resource_barrier() override;
  virtual void bind_pipeline() override;
  virtual void bind_vertex_buffer() override;
  virtual void bind_index_buffer() override;
  virtual void draw() override;
};

} // namespace Helix
