#pragma once

#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererTypes.hpp"
#include "VulkanTypes.hpp"
#include <vulkan/vulkan_core.h>

namespace Helix {
struct VulkanBackend : public RendererBackend {
  virtual bool init(void *config) override;
  virtual bool shutdown() override;
  virtual bool on_resize(u16 width, u16 height) override;
  virtual bool begin_frame(RenderPacket *packet) override;
  virtual bool end_frame(RenderPacket *packet) override;

  void create_swapchain();
  void destroy_swapchain();
  void resize_swapchain();

  void create_pipeline(PipelineCreation &creation);
  void create_command_pool(QueueFamilyIndices &indices);

  void create_command_buffers();
  void record_command_buffer(VkCommandBuffer vk_command_buffer, u32 index);

  void create_sync_objects();

  // TODO: Make a generic create_buffers
  void create_buffers();
  void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VulkanBuffer &buffer);

  void copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

  void create_descriptor_set_layout();
  void create_descriptor_pool();
  void create_descriptor_sets();

  void draw_frame(RenderPacket *packet);
  void update_uniform_buffer(u32 current_image_index, RenderPacket *packet);

  u32 const max_frames_in_flight = 2;
  u32 current_frame = 0;

  VkInstance vk_instance{VK_NULL_HANDLE};
  VkAllocationCallbacks *vk_allocation_callbacks{nullptr};
  VkDebugUtilsMessengerEXT vk_debug_utils_messenger{VK_NULL_HANDLE};
  VkSurfaceKHR vk_surface{VK_NULL_HANDLE};
  VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
  VkDevice vk_device{VK_NULL_HANDLE};
  VkQueue vk_graphics_queue{VK_NULL_HANDLE};
  VkQueue vk_transfer_queue{VK_NULL_HANDLE};

  VkPipelineLayout vk_pipeline_layout{VK_NULL_HANDLE};
  VkPipeline vk_pipeline{VK_NULL_HANDLE};

  VkCommandPool vk_command_pool{VK_NULL_HANDLE};
  VkCommandPool vk_transfer_pool{VK_NULL_HANDLE};
  Array<VkCommandBuffer> vk_command_buffers;

  Array<VkSemaphore> image_available_semaphores;
  Array<VkSemaphore> render_finished_semaphores;
  Array<VkFence> in_flight_fences;

  VulkanSwapchain swapchain{};

  VulkanBuffer vertex_buffer{};
  VulkanBuffer index_buffer{};
  Array<VulkanBuffer> uniform_buffers{};

  VkDescriptorSetLayout vk_descriptor_set_layout{VK_NULL_HANDLE};
  VkDescriptorPool vk_descriptor_pool{VK_NULL_HANDLE};
  Array<VkDescriptorSet> vk_descriptor_sets{};

  Array<Vertex> vertices{};

  bool resize_frame = false;
};
} // namespace Helix
