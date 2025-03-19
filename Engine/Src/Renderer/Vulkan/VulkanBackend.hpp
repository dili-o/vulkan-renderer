#pragma once

#include "Renderer/RendererBackend.hpp"
#include "VulkanTypes.hpp"
#include <vulkan/vulkan_core.h>

namespace Helix {
struct VulkanBackend : public RendererBackend {
  virtual bool init(void *config) override;
  virtual bool shutdown() override;
  virtual bool on_resize(u16 width, u16 height) override;
  virtual bool begin_frame(f32 delta_time) override;
  virtual bool end_frame(f32 delta_time) override;

  void create_swapchain();
  void destroy_swapchain();

  void create_graphics_pipeline();
  void create_command_pool(QueueFamilyIndices &indices);

  void create_command_buffer();
  void record_command_buffer(u32 index);

  void create_sync_objects();

  void draw_frame();

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkDevice vk_device{VK_NULL_HANDLE};
  VkQueue vk_graphics_queue{VK_NULL_HANDLE};

  VkPipelineLayout vk_pipeline_layout{VK_NULL_HANDLE};
  VkPipeline vk_pipeline{VK_NULL_HANDLE};

  VkCommandPool vk_command_pool{VK_NULL_HANDLE};
  VkCommandBuffer vk_command_buffer{VK_NULL_HANDLE};

  VkSemaphore image_available_semaphore{VK_NULL_HANDLE};
  VkSemaphore render_finished_semaphore{VK_NULL_HANDLE};
  VkFence in_flight_fence{VK_NULL_HANDLE};

  VulkanSwapchain swapchain{};
};
} // namespace Helix
