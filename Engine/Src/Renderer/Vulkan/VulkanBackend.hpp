#pragma once

#include "Containers/ResourcePool.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererTypes.hpp"
#include "VulkanTypes.hpp"

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

  void create_pipeline_old(PipelineCreation &creation);
  void create_command_pool(QueueFamilyIndices &indices);

  void create_command_buffers(u32 max_frames_in_flight);
  void record_command_buffer(VkCommandBuffer vk_command_buffer, u32 image_index,
                             u32 current_frame);

  void create_sync_objects(u32 max_frames_in_flight);

  // TODO: Make a generic create_buffers
  void create_buffers();
  void vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VulkanBuffer &buffer);
  virtual BufferHandle create_buffer(BufferCreation &creation) override;
  virtual PipelineHandle create_pipeline(PipelineCreation &creation) override;

  virtual void destroy_buffer(BufferHandle handle) override;

  void copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size);
  void upload_buffer_data(void *data, VkBuffer dst_buffer, u32 size);

  void create_descriptor_set_layout();
  void create_descriptor_pool(u32 max_frames_in_flight);
  void create_descriptor_sets(u32 max_frames_in_flight);

  void load_model();

  void draw_frame(RenderPacket *packet);
  void update_uniform_buffer(RenderPacket *packet);

  void set_resource_name(VkObjectType type, u64 handle, cstring name);

  // u32 const max_frames_in_flight = 2;
  // u32 current_frame = 0;

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

  VkPipelineLayout vk_pipeline_layout{VK_NULL_HANDLE};
  VkPipeline vk_pipeline{VK_NULL_HANDLE};

  VkCommandPool vk_command_pool{VK_NULL_HANDLE};
  VkCommandPool vk_transfer_pool{VK_NULL_HANDLE};
  Array<VkCommandBuffer> vk_command_buffers;

  Array<VkSemaphore> image_available_semaphores;
  Array<VkSemaphore> render_finished_semaphores;
  Array<VkFence> in_flight_fences;

  VulkanSwapchain swapchain{};

  // TODO: These should be stored in a MeshDraw Object
  VulkanBuffer vertex_buffer{};
  VulkanBuffer index_buffer{};

  VkDescriptorSetLayout vk_descriptor_set_layout{VK_NULL_HANDLE};
  VkDescriptorPool vk_descriptor_pool{VK_NULL_HANDLE};
  Array<VkDescriptorSet> vk_descriptor_sets{};

  Array<Vertex> vertices{};
  Array<u32> indexes{};

  ResourcePool<VulkanBuffer> buffers{};

  bool resize_frame = false;
};
} // namespace Helix
