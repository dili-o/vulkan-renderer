#include "VulkanBackend.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Assert.hpp"
#include "Core/Defines.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"
#include "Core/String.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Platform/Process.hpp"
#include "Renderer/Camera.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
#include "Renderer/Vulkan/VulkanUtils.hpp"
#include "SpirvParser.hpp"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <cstring>
#include <tiny_obj_loader.h>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#ifdef _DEBUG
#define VULKAN_DEBUG_REPORT
#endif // _DEBUG

#define MIN_BUFFER_SIZE 4

namespace std {
template <> struct hash<Helix::Vertex> {
  size_t operator()(Helix::Vertex const &vertex) const {
    size_t h1 = hash<glm::vec3>()(vertex.pos);
    size_t h2 = hash<glm::vec2>()(vertex.tex_coord);
    return h1 ^ (h2 << 1); // Combine hashes safely
  }
};
} // namespace std

namespace Helix {

#pragma region HelperFunctions

#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult result_ = call;                                                   \
    HASSERT_MSGS(result_ == VK_SUCCESS, "Error code: {}", (u32)result_);       \
  } while (0)

static VkBool32
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
               VkDebugUtilsMessageTypeFlagsEXT message_type,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data);

static bool select_physical_device(VkInstance instance,
                                   VkPhysicalDevice &_physical_device,
                                   QueueFamilyIndices &indices,
                                   VkSurfaceKHR surface);

static void query_swapchain_support(VkPhysicalDevice physical_device,
                                    VkSurfaceKHR surface,
                                    VulkanSwapchain &swapchain);

u32 find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties,
                     VkPhysicalDevice physical_device);

// TODO: Should be in a math library
template <class T>
constexpr const T &clamp(const T &v, const T &lo, const T &hi) {
  HASSERT(!(hi < lo));
  return (v < lo) ? lo : (hi < v) ? hi : v;
}

cstring to_compiler_stage(ShaderStage::Enum stage);

VkShaderStageFlagBits to_vk_shader_stage(ShaderStage::Enum stage);

#pragma endregion HelperFunctions

bool VulkanBackend::init(void *_config) {
  VkResult res = volkInitialize();
  if (res != VK_SUCCESS) {
    HERROR("Volk failed to initialize");
    return false;
  }

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();

  RendererConfig *config = (RendererConfig *)_config;

#pragma region Instance_Creation
  VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.apiVersion = VK_API_VERSION_1_3;
  app_info.pApplicationName = config->application_name;
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "Helix Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create_info.pApplicationInfo = &app_info;

  Array<cstring> required_extensions{};
  required_extensions.init(stack_allocator, 1);
  u32 platform_extension_count = 0;
  const char *const *platform_extensions = SDL_Vulkan_GetInstanceExtensions(
      &platform_extension_count); // SDL Already includes the
                                  // KHR_SURFACE_EXTENSION
  for (u32 i = 0; i < platform_extension_count; ++i) {
    required_extensions.push(platform_extensions[i]);
  }

  bool debug_utils_extension_present = false;
#ifdef VULKAN_DEBUG_REPORT
  u32 num_instance_extensions = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &num_instance_extensions,
                                         nullptr);
  VkExtensionProperties *extensions = (VkExtensionProperties *)halloca(
      sizeof(VkExtensionProperties) * num_instance_extensions, stack_allocator);
  vkEnumerateInstanceExtensionProperties(nullptr, &num_instance_extensions,
                                         extensions);
  for (size_t i = 0; i < num_instance_extensions; i++) {

    if (!strcmp(extensions[i].extensionName,
                VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      debug_utils_extension_present = true;
      required_extensions.push(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      break;
    }
  }

#endif

  create_info.enabledExtensionCount = required_extensions.size;
  create_info.ppEnabledExtensionNames = required_extensions.data;

  Array<cstring> validation_layer_names{};
  validation_layer_names.init(stack_allocator, 1);
#ifdef VULKAN_DEBUG_REPORT
  validation_layer_names.push("VK_LAYER_KHRONOS_validation");

  u32 layer_not_found_index = 0;
  auto check_layer_support = [&validation_layer_names,
                              &layer_not_found_index]() {
    u32 layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (u32 i = 0; i < validation_layer_names.size; ++i) {
      bool layer_found = false;
      const char *layer_name = validation_layer_names[i];

      for (const auto &layer_properties : available_layers) {
        if (strcmp(layer_name, layer_properties.layerName) == 0) {
          layer_found = true;
          break;
        }
      }

      if (!layer_found) {
        layer_not_found_index = i;
        return false;
      }
    }

    return true;
  };

  if (!check_layer_support()) {
    HERROR("A validation layer {} was not found!",
           validation_layer_names[layer_not_found_index]);
    validation_layer_names.pop_at(layer_not_found_index);
  }
  VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
  debug_create_info.sType =
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debug_create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug_create_info.messageType =
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  debug_create_info.pfnUserCallback = debug_callback;

  create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_create_info;
#endif // VULKAN_DEBUG_REPORT
  create_info.enabledLayerCount = validation_layer_names.size;
  create_info.ppEnabledLayerNames = validation_layer_names.data;

  VK_CHECK(
      vkCreateInstance(&create_info, vk_allocation_callbacks, &vk_instance));

  volkLoadInstance(vk_instance);

  validation_layer_names.shutdown();
  required_extensions.shutdown();
  stack_allocator->free_marker(stack_marker);
#pragma endregion Instance_Creation

#ifdef VULKAN_DEBUG_REPORT

#pragma region Vulkan_Debugger
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      vk_instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    if (func(vk_instance, &debug_create_info, vk_allocation_callbacks,
             &vk_debug_utils_messenger) != VK_SUCCESS) {
      HERROR("Failed to set up debug messenger!");
    }
  } else {
    HERROR("Failed to set up debug messenger!");
  }
#endif
#pragma endregion Vulkan_Debugger

  // Surface
  SDL_Window *window = (SDL_Window *)Platform::instance()->platform_handle;
  if (!SDL_Vulkan_CreateSurface(window, vk_instance, vk_allocation_callbacks,
                                &vk_surface)) {
    HERROR("Failed to create surface!");
    return false;
  }

  QueueFamilyIndices indices{};
  Array<cstring> device_extensions{};
  device_extensions.init(stack_allocator, 1, 1);
  device_extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  // Create Physical Device
  if (!select_physical_device(vk_instance, vk_physical_device, indices,
                              vk_surface)) {
    HERROR("Failed to create physical device!");
    return false;
  }

  // Create Logical Device
  Array<VkDeviceQueueCreateInfo> queue_create_infos{};
  queue_create_infos.init(stack_allocator, 1);
  f32 queue_priority = 1.f;

  VkDeviceQueueCreateInfo main_queue_info{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  main_queue_info.queueFamilyIndex = indices.graphics_family_index;
  main_queue_info.queueCount = 1;
  main_queue_info.pQueuePriorities = &queue_priority;
  queue_create_infos.push(main_queue_info);

  if (indices.graphics_family_index != indices.transfer_family_index) {
    VkDeviceQueueCreateInfo queue_create_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex = indices.transfer_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push(queue_create_info);
  }

  VkPhysicalDeviceFeatures device_features{};

  VkDeviceCreateInfo device_create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_create_info.pQueueCreateInfos = queue_create_infos.data;
  device_create_info.queueCreateInfoCount = queue_create_infos.size;
  device_create_info.pEnabledFeatures = &device_features;
  device_create_info.enabledExtensionCount = device_extensions.size;
  device_create_info.ppEnabledExtensionNames = device_extensions.data;

  // Enable Dynamic Rendering
  VkPhysicalDeviceVulkan13Features features13 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;

  device_create_info.pNext = &features13;

  VK_CHECK(vkCreateDevice(vk_physical_device, &device_create_info,
                          vk_allocation_callbacks, &vk_device));
  volkLoadDevice(vk_device);
  // Use Volks function pointers
  VmaVulkanFunctions vma_vulkan_functions{};
  vma_vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vma_vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  vma_vulkan_functions.vkGetPhysicalDeviceProperties =
      vkGetPhysicalDeviceProperties;
  vma_vulkan_functions.vkGetPhysicalDeviceMemoryProperties =
      vkGetPhysicalDeviceMemoryProperties;
  vma_vulkan_functions.vkAllocateMemory = vkAllocateMemory;
  vma_vulkan_functions.vkFreeMemory = vkFreeMemory;
  vma_vulkan_functions.vkMapMemory = vkMapMemory;
  vma_vulkan_functions.vkUnmapMemory = vkUnmapMemory;
  vma_vulkan_functions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
  vma_vulkan_functions.vkInvalidateMappedMemoryRanges =
      vkInvalidateMappedMemoryRanges;
  vma_vulkan_functions.vkBindBufferMemory = vkBindBufferMemory;
  vma_vulkan_functions.vkBindImageMemory = vkBindImageMemory;
  vma_vulkan_functions.vkGetBufferMemoryRequirements =
      vkGetBufferMemoryRequirements;
  vma_vulkan_functions.vkGetImageMemoryRequirements =
      vkGetImageMemoryRequirements;
  vma_vulkan_functions.vkCreateBuffer = vkCreateBuffer;
  vma_vulkan_functions.vkDestroyBuffer = vkDestroyBuffer;
  vma_vulkan_functions.vkCreateImage = vkCreateImage;
  vma_vulkan_functions.vkDestroyImage = vkDestroyImage;
  vma_vulkan_functions.vkCmdCopyBuffer = vkCmdCopyBuffer;
  vma_vulkan_functions.vkGetBufferMemoryRequirements2KHR =
      vkGetBufferMemoryRequirements2KHR;
  vma_vulkan_functions.vkGetImageMemoryRequirements2KHR =
      vkGetImageMemoryRequirements2KHR;
  vma_vulkan_functions.vkBindBufferMemory2KHR = vkBindBufferMemory2KHR;
  vma_vulkan_functions.vkBindImageMemory2KHR = vkBindImageMemory2KHR;
  vma_vulkan_functions.vkGetPhysicalDeviceMemoryProperties2KHR =
      vkGetPhysicalDeviceMemoryProperties2KHR;

  VmaAllocatorCreateInfo allocator_create_info = {};
  allocator_create_info.physicalDevice = vk_physical_device;
  allocator_create_info.device = vk_device;
  allocator_create_info.instance = vk_instance;
  allocator_create_info.pVulkanFunctions = &vma_vulkan_functions;
  allocator_create_info.flags =
      VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

  VK_CHECK(vmaCreateAllocator(&allocator_create_info, &vma_allocator));

  vkGetDeviceQueue(vk_device, indices.graphics_family_index, 0,
                   &vk_graphics_queue);
  vk_transfer_queue = vk_graphics_queue;
  if (indices.graphics_family_index != indices.transfer_family_index) {
    vkGetDeviceQueue(vk_device, indices.transfer_family_index, 0,
                     &vk_transfer_queue);
  }

  queue_create_infos.shutdown();

  //  Get the function pointers to Debug Utils functions.
  if (debug_utils_extension_present) {
    pfnSetDebugUtilsObjectNameEXT =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
            vk_device, "vkSetDebugUtilsObjectNameEXT");
  }

  // Create swapchain
  query_swapchain_support(vk_physical_device, vk_surface, swapchain);
  create_swapchain();

  create_command_pool(indices);

  vertices.init(allocator, 3);
  // vertices.push({{-0.5f, -0.5f}, {1.0f, 0.0f}});
  // vertices.push({{0.5f, -0.5f}, {0.0f, 0.0f}});
  // vertices.push({{0.5f, 0.5f}, {0.0f, 1.0f}});
  // vertices.push({{-0.5f, 0.5f}, {1.0f, 1.0f}});
  indexes.init(allocator, 10);

  load_model();

  buffers.init(allocator, 10);
  pipelines.init(allocator, 10);
  descriptor_set_layouts.init(allocator, 10);
  descriptor_sets.init(allocator, 10);

  create_buffers();
  create_command_buffers(config->max_frames_in_flight);
  create_sync_objects(config->max_frames_in_flight);
  create_descriptor_pool(config->max_frames_in_flight);

  frame_number = 0;
  HINFO("Vulkan Backend Initialized");
  return true;
}

bool VulkanBackend::shutdown() {
  vkDeviceWaitIdle(vk_device);

  // TODO: Create a vulkan resource queue
  for (u32 i = 0; i < 2; ++i) {
    vkDestroySemaphore(vk_device, image_available_semaphores[i],
                       vk_allocation_callbacks);
    vkDestroySemaphore(vk_device, render_finished_semaphores[i],
                       vk_allocation_callbacks);
    vkDestroyFence(vk_device, in_flight_fences[i], vk_allocation_callbacks);
  }

  image_available_semaphores.shutdown();
  render_finished_semaphores.shutdown();
  in_flight_fences.shutdown();

  vkDestroyDescriptorPool(vk_device, vk_descriptor_pool,
                          vk_allocation_callbacks);

  vkDestroyCommandPool(vk_device, vk_command_pool, vk_allocation_callbacks);
  vkDestroyCommandPool(vk_device, vk_transfer_pool, vk_allocation_callbacks);
  vk_command_buffers.shutdown();

  // vkDestroyPipeline(vk_device, vk_pipeline, vk_allocation_callbacks);
  // vkDestroyPipelineLayout(vk_device, vk_pipeline_layout,
  //                         vk_allocation_callbacks);
  destroy_swapchain();

  vmaDestroyBuffer(vma_allocator, vertex_buffer.vk_handle,
                   vertex_buffer.vma_allocation);
  vmaDestroyBuffer(vma_allocator, index_buffer.vk_handle,
                   index_buffer.vma_allocation);

  vertices.shutdown();
  indexes.shutdown();

  pipelines.shutdown();
  descriptor_set_layouts.shutdown();
  descriptor_sets.shutdown();
  buffers.shutdown();
  vmaDestroyAllocator(vma_allocator);

  vkDestroyDevice(vk_device, vk_allocation_callbacks);
  vkDestroySurfaceKHR(vk_instance, vk_surface, vk_allocation_callbacks);
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      vk_instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(vk_instance, vk_debug_utils_messenger, vk_allocation_callbacks);
  }
  vkDestroyInstance(vk_instance, vk_allocation_callbacks);
  HINFO("Vulkan Backend shutdown");
  return true;
}

bool VulkanBackend::on_resize(u16 width, u16 height) {
  resize_frame = true;
  return true;
}

void VulkanBackend::resize_swapchain() {
  vkDeviceWaitIdle(vk_device);

  VkSurfaceCapabilitiesKHR surface_capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface,
                                            &surface_capabilities);
  VkExtent2D swapchain_extent = surface_capabilities.currentExtent;

  if (swapchain_extent.width == 0 || swapchain_extent.height == 0) {
    return;
  }

  destroy_swapchain();
  query_swapchain_support(vk_physical_device, vk_surface, swapchain);
  create_swapchain();
}

bool VulkanBackend::begin_frame(RenderPacket *packet) {
  draw_frame(packet);
  return true;
}

bool VulkanBackend::end_frame(RenderPacket *packet) { return true; }

void VulkanBackend::create_swapchain() {
  VkSurfaceCapabilitiesKHR surface_capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface,
                                            &surface_capabilities);

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = vk_surface;
  create_info.minImageCount = swapchain.image_count;
  create_info.imageFormat = swapchain.vk_surface_format.format;
  create_info.imageColorSpace = swapchain.vk_surface_format.colorSpace;
  create_info.imageExtent = swapchain.vk_extents;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = surface_capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = swapchain.vk_present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = swapchain.vk_handle;

  VK_CHECK(vkCreateSwapchainKHR(vk_device, &create_info,
                                vk_allocation_callbacks, &swapchain.vk_handle));

  HASSERT(swapchain.image_count <= MAX_SWAPCHAIN_IMAGES);
  vkGetSwapchainImagesKHR(vk_device, swapchain.vk_handle,
                          &swapchain.image_count, swapchain.vk_images);

  for (u32 i = 0; i < swapchain.image_count; ++i) {
    VkImageViewCreateInfo view_create_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_create_info.image = swapchain.vk_images[i];
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create_info.format = swapchain.vk_surface_format.format;
    view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_create_info.subresourceRange.baseMipLevel = 0;
    view_create_info.subresourceRange.levelCount = 1;
    view_create_info.subresourceRange.baseArrayLayer = 0;
    view_create_info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(vk_device, &view_create_info, nullptr,
                               &swapchain.vk_image_views[i]));
  }
  HTRACE("Created swapchain successfully");
}

void VulkanBackend::destroy_swapchain() {
  for (u32 i = 0; i < swapchain.image_count; ++i) {
    vkDestroyImageView(vk_device, swapchain.vk_image_views[i],
                       vk_allocation_callbacks);
    swapchain.vk_image_views[i] = VK_NULL_HANDLE;
  }

  vkDestroySwapchainKHR(vk_device, swapchain.vk_handle,
                        vk_allocation_callbacks);
  swapchain.vk_handle = VK_NULL_HANDLE;
}

void VulkanBackend::create_command_pool(QueueFamilyIndices &indices) {
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = indices.graphics_family_index;
  VK_CHECK(vkCreateCommandPool(vk_device, &pool_info, vk_allocation_callbacks,
                               &vk_command_pool));

  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = indices.transfer_family_index;
  VK_CHECK(vkCreateCommandPool(vk_device, &pool_info, vk_allocation_callbacks,
                               &vk_transfer_pool));
}

void VulkanBackend::create_command_buffers(u32 max_frames_in_flight) {
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  vk_command_buffers.init(allocator, max_frames_in_flight,
                          max_frames_in_flight);
  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = vk_command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = vk_command_buffers.size;

  VK_CHECK(vkAllocateCommandBuffers(vk_device, &alloc_info,
                                    vk_command_buffers.data));
}

void VulkanBackend::record_command_buffer(VkCommandBuffer vk_command_buffer,
                                          u32 image_index, u32 current_frame) {
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = 0;                  // Optional
  begin_info.pInheritanceInfo = nullptr; // Optional

  VK_CHECK(vkBeginCommandBuffer(vk_command_buffer, &begin_info));

  // Transition Image
  {
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = swapchain.vk_images[image_index];
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;

    // Synchronization settings
    image_barrier.srcAccessMask = 0;
    image_barrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // No further writes needed before
                                              // presenting

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    vkCmdPipelineBarrier(vk_command_buffer, src_stage, dst_stage, 0, 0, nullptr,
                         0, nullptr, 1, &image_barrier);
  }
  ////////////////////
  VkRenderingAttachmentInfo color_attachment_info{
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

  color_attachment_info.imageView = swapchain.vk_image_views[image_index];
  color_attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment_info.clearValue = {{{0.0f, 0.0f, 0.1f, 1.0f}}};
  color_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;

  VkRenderingInfo render_info{VK_STRUCTURE_TYPE_RENDERING_INFO};
  render_info.layerCount = 1;
  render_info.renderArea = {
      {0, 0}, {swapchain.vk_extents.width, swapchain.vk_extents.height}};
  render_info.viewMask = 0;
  render_info.colorAttachmentCount = 1;
  render_info.pColorAttachments = &color_attachment_info;
  render_info.pDepthAttachment = nullptr;
  render_info.pStencilAttachment = nullptr;

  vkCmdBeginRendering(vk_command_buffer, &render_info);

  // TODO:
  VulkanPipeline *pipeline = pipelines.obtain({0, 0});
  vkCmdBindPipeline(vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->vk_handle);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (f32)swapchain.vk_extents.width;
  viewport.height = (f32)swapchain.vk_extents.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(vk_command_buffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapchain.vk_extents;
  vkCmdSetScissor(vk_command_buffer, 0, 1, &scissor);

  VkBuffer vertex_buffers[] = {vertex_buffer.vk_handle};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(vk_command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(vk_command_buffer, index_buffer.vk_handle, 0,
                       VK_INDEX_TYPE_UINT32);

  // TODO:
  VulkanDescriptorSetLayout *dset_layout =
      descriptor_set_layouts.obtain(pipeline->set_layouts[0]);
  VulkanDescriptorSet *dset =
      descriptor_sets.obtain(dset_layout->allocated_sets[current_frame]);

  vkCmdBindDescriptorSets(vk_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->vk_layout, 0, 1, &dset->vk_handle, 0,
                          nullptr);
  vkCmdDrawIndexed(vk_command_buffer, indexes.size, 1, 0, 0, 0);
  vkCmdEndRendering(vk_command_buffer);

  // Transition to Present
  {
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Previous layout (after
                                                  // rendering)
    image_barrier.newLayout =
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Layout for presentation
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = swapchain.vk_images[image_index];
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;

    // Synchronization settings
    image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier.dstAccessMask =
        0; // No further writes needed before presenting

    VkPipelineStageFlags src_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    vkCmdPipelineBarrier(vk_command_buffer, src_stage, dst_stage, 0, 0, nullptr,
                         0, nullptr, 1, &image_barrier);
  }

  VK_CHECK(vkEndCommandBuffer(vk_command_buffer));
}

void VulkanBackend::create_sync_objects(u32 max_frames_in_flight) {

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  image_available_semaphores.init(allocator, max_frames_in_flight,
                                  max_frames_in_flight);
  render_finished_semaphores.init(allocator, max_frames_in_flight,
                                  max_frames_in_flight);
  in_flight_fences.init(allocator, max_frames_in_flight, max_frames_in_flight);

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                               vk_allocation_callbacks,
                               &image_available_semaphores[i]));
    VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                               vk_allocation_callbacks,
                               &render_finished_semaphores[i]));
    VK_CHECK(vkCreateFence(vk_device, &fence_info, vk_allocation_callbacks,
                           &in_flight_fences[i]));
  }
}

void VulkanBackend::create_buffers() {
  VkDeviceSize buffer_size = sizeof(vertices[0]) * vertices.size;

  vk_create_buffer(buffer_size,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertex_buffer);

  VulkanBuffer staging_buffer{};
  vk_create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging_buffer);

  void *data;
  vmaMapMemory(vma_allocator, staging_buffer.vma_allocation, &data);
  memcpy(data, vertices.data, (size_t)buffer_size);
  vmaUnmapMemory(vma_allocator, staging_buffer.vma_allocation);

  copy_buffer(staging_buffer.vk_handle, vertex_buffer.vk_handle, buffer_size);

  vmaDestroyBuffer(vma_allocator, staging_buffer.vk_handle,
                   staging_buffer.vma_allocation);

  buffer_size = sizeof(u32) * indexes.size;
  vk_create_buffer(buffer_size,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index_buffer);

  vk_create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging_buffer);

  vmaMapMemory(vma_allocator, staging_buffer.vma_allocation, &data);
  memcpy(data, indexes.data, (size_t)buffer_size);
  vmaUnmapMemory(vma_allocator, staging_buffer.vma_allocation);

  copy_buffer(staging_buffer.vk_handle, index_buffer.vk_handle, buffer_size);

  vmaDestroyBuffer(vma_allocator, staging_buffer.vk_handle,
                   staging_buffer.vma_allocation);

  // Uniform Buffers
  // buffer_size = sizeof(UniformBufferObject);

  // HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  // uniform_buffers.init(allocator, max_frames_in_flight,
  // max_frames_in_flight);

  // for (u32 i = 0; i < max_frames_in_flight; ++i) {
  //   vk_create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
  //                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
  //                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  //                    uniform_buffers[i]);
  //   vmaMapMemory(vma_allocator, uniform_buffers[i].vma_allocation,
  //                &uniform_buffers[i].mapped_data);
  // }
}

void VulkanBackend::vk_create_buffer(VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VulkanBuffer &buffer) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
  memory_info.requiredFlags = properties;
  memory_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VK_CHECK(vmaCreateBuffer(vma_allocator, &buffer_info, &memory_info,
                           &buffer.vk_handle, &buffer.vma_allocation, nullptr));
}

BufferHandle VulkanBackend::create_buffer(BufferCreation &creation) {
  BufferHandle handle = buffers.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a Vulkan Buffer Resource!");
    return handle;
  }

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size =
      creation.size < MIN_BUFFER_SIZE ? MIN_BUFFER_SIZE : creation.size;

  if (creation.usage_flags == BufferUsage::None) {
    HERROR("Creating a buffer with no usage flags");
  }

  buffer_info.usage = to_vk_usage_flags(creation.usage_flags);
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = VMA_MEMORY_USAGE_AUTO;
  memory_info.requiredFlags =
      to_vk_mem_property_flags(creation.memory_access_flags);
  // Note to self: VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT for buffers that
  // change a lot in a frame,
  // VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT for buffers that
  // change only once per frame
  memory_info.flags =
      creation.memory_state_flags & MemoryState::Persistent
          ? VMA_ALLOCATION_CREATE_MAPPED_BIT |
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
          : 0;

  // TODO: Make use of MemoryState::Enum
  VulkanBuffer *buffer = buffers.obtain(handle);
  VmaAllocationInfo alloc_info{};

  VK_CHECK(vmaCreateBuffer(vma_allocator, &buffer_info, &memory_info,
                           &buffer->vk_handle, &buffer->vma_allocation,
                           &alloc_info));

  if (creation.initial_data) {
    if (creation.memory_access_flags & MemoryAccess::GPU_ONLY) {
      upload_buffer_data(creation.initial_data, buffer->vk_handle,
                         creation.size);
    } else {
      vmaMapMemory(vma_allocator, buffer->vma_allocation, &buffer->mapped_data);
      memcpy(buffer->mapped_data, creation.initial_data, (size_t)creation.size);
      vmaUnmapMemory(vma_allocator, buffer->vma_allocation);
    }
  }

  if (creation.memory_state_flags & MemoryState::Persistent) {
    vmaMapMemory(vma_allocator, buffer->vma_allocation, &buffer->mapped_data);
  }

  set_resource_name(VK_OBJECT_TYPE_BUFFER, (u64)buffer->vk_handle,
                    creation.name);

  return handle;
}

PipelineHandle VulkanBackend::create_pipeline(PipelineCreation &creation) {
  PipelineHandle handle = pipelines.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain VulkanPipeline");
    return handle;
  }

  VulkanPipeline *pipeline = pipelines.obtain(handle);
  pipeline->vk_handle = VK_NULL_HANDLE;

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();
  FileService *file_service = FileService::instance();

  // Parse shaders
  StringBuffer temp_string_buffer{};
  temp_string_buffer.init(stack_allocator, hkilo(1));

  char *vulkan_sdk_path = temp_string_buffer.reserve(512);
  file_service->expand_enviroment_variable("%VULKAN_SDK%", vulkan_sdk_path,
                                           512);
  cstring glsl_compiler_path =
      temp_string_buffer.append_use_f("%s\\Bin\\glslc.exe", vulkan_sdk_path);
#ifdef VULKAN_DEBUG_REPORT
  cstring compiler_debug = "-g";
#else
  cstring compiler_debug = "";
#endif

  VkShaderModule *vk_shader_modules = (VkShaderModule *)halloca(
      sizeof(VkShaderModule) * creation.shader_count, stack_allocator);
  VkPipelineShaderStageCreateInfo *vk_shader_stages =
      (VkPipelineShaderStageCreateInfo *)halloca(
          sizeof(VkPipelineShaderStageCreateInfo) * creation.shader_count,
          stack_allocator);

  Directory dir;
  file_service->current_directory(&dir);

  file_service->change_directory(ASSETS_PATH "/Shaders/");
  ParseResult parse_result{};
  parse_result.set_layouts.init(stack_allocator, 4);
  for (u32 i = 0; i < creation.shader_count; ++i) {
    ShaderCreateInfo shader = creation.shader_create_infos[i];
    cstring shader_args = temp_string_buffer.append_use_f(
        " -fshader-stage=%s %s.glsl -o %s.spv --target-env=vulkan1.3 %s",
        to_compiler_stage(shader.stage), shader.filename, shader.filename,
        compiler_debug);
    HASSERT(process_execute(".", glsl_compiler_path, shader_args));
    HDEBUG("{}", process_get_output());

    cstring binary_name =
        temp_string_buffer.append_use_f("%s.spv", shader.filename);
    FileReadResult shader_binary =
        file_service->read_file_binary(binary_name, stack_allocator);
    file_service->delete_file(binary_name);

    VkShaderModuleCreateInfo shader_create_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_create_info.codeSize = shader_binary.size;
    shader_create_info.pCode = (u32 *)shader_binary.data;

    VK_CHECK(vkCreateShaderModule(vk_device, &shader_create_info,
                                  vk_allocation_callbacks,
                                  &vk_shader_modules[i]));

    VkPipelineShaderStageCreateInfo &pipeline_stage_info = vk_shader_stages[i];
    pipeline_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_stage_info.stage = to_vk_shader_stage(shader.stage);
    pipeline_stage_info.module = vk_shader_modules[i];
    pipeline_stage_info.pName = "main";
    pipeline_stage_info.pSpecializationInfo = nullptr;
    pipeline_stage_info.flags = 0;
    pipeline_stage_info.pNext = nullptr;

    parse_binary((u32 *)shader_binary.data, shader_binary.size, parse_result);
  }

  temp_string_buffer.clear();

  // Dynamic State
  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = ArraySize(dynamic_states);
  dynamic_state.pDynamicStates = dynamic_states;

  // Vertex Input State
  VkPipelineVertexInputStateCreateInfo vertex_input_state{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input_state.vertexBindingDescriptionCount =
      1; // TODO: For now assume only one descriptor binding
  vertex_input_state.pVertexBindingDescriptions = &parse_result.vertex_binding;
  vertex_input_state.vertexAttributeDescriptionCount =
      parse_result.vertex_attribute_count;
  vertex_input_state.pVertexAttributeDescriptions =
      parse_result.vertex_attributes;

  // Input Assembly
  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  // Viewport
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (f32)swapchain.vk_extents.width;
  viewport.height = (f32)swapchain.vk_extents.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  // Scissor
  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapchain.vk_extents;

  VkPipelineViewportStateCreateInfo viewport_state{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;
  viewport_state.pViewports = &viewport;

  // Rasterizer State
  VkPipelineRasterizationStateCreateInfo rasterizer{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE; // VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f; // Optional
  rasterizer.depthBiasClamp = 0.0f;          // Optional
  rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

  // Multisampling State
  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;          // Optional
  multisampling.pSampleMask = nullptr;            // Optional
  multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
  multisampling.alphaToOneEnable = VK_FALSE;      // Optional

  // Depth and Stencil State

  // Color Blend State
  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_FALSE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;             // Optional

  VkPipelineColorBlendStateCreateInfo color_blending{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f; // Optional
  color_blending.blendConstants[1] = 0.0f; // Optional
  color_blending.blendConstants[2] = 0.0f; // Optional
  color_blending.blendConstants[3] = 0.0f; // Optional

  // Descriptor Set Layouts
  VkDescriptorSetLayout *layouts = (VkDescriptorSetLayout *)halloca(
      sizeof(VkDescriptorSetLayout) * parse_result.set_layouts.size,
      stack_allocator);
  pipeline->set_layouts = (DescriptorSetLayoutHandle *)halloca(
      sizeof(DescriptorSetLayoutHandle) * parse_result.set_layouts.size,
      allocator);
  pipeline->set_layout_count = parse_result.set_layouts.size;
  for (u32 i = 0; i < parse_result.set_layouts.size; ++i) {
    DescriptorSetLayoutHandle dset_layout_handle =
        descriptor_set_layouts.obtain_new();
    if (dset_layout_handle.index != k_invalid_index) {
      VulkanDescriptorSetLayout *dset_layout =
          descriptor_set_layouts.obtain(dset_layout_handle);

      dset_layout->vk_bindings = parse_result.set_layouts[i].vk_bindings;
      dset_layout->num_bindings = parse_result.set_layouts[i].num_bindings;
      dset_layout->set_index = parse_result.set_layouts[i].set_index;
      dset_layout->allocated_sets.init(allocator, 4);

      VkDescriptorSetLayoutCreateInfo layout_info{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      layout_info.bindingCount = dset_layout->num_bindings;
      layout_info.pBindings = dset_layout->vk_bindings;

      VK_CHECK(vkCreateDescriptorSetLayout(vk_device, &layout_info,
                                           vk_allocation_callbacks,
                                           &dset_layout->vk_handle));
      layouts[i] = dset_layout->vk_handle;
      pipeline->set_layouts[i] = dset_layout_handle;
    }
  }
  // Pipeline Layout
  VkPipelineLayoutCreateInfo pipeline_layout_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_info.setLayoutCount = parse_result.set_layouts.size;
  pipeline_layout_info.pSetLayouts = layouts;

  VK_CHECK(vkCreatePipelineLayout(vk_device, &pipeline_layout_info,
                                  vk_allocation_callbacks,
                                  &pipeline->vk_layout));
  // Dynamic Rendering
  VkPipelineRenderingCreateInfoKHR pipeline_create{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
  pipeline_create.pNext = VK_NULL_HANDLE;
  pipeline_create.colorAttachmentCount = 1;
  pipeline_create.pColorAttachmentFormats = &swapchain.vk_surface_format.format;
  pipeline_create.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
  pipeline_create.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

  if (creation.pipeline_type == PipelineType::Graphics) {

    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = creation.shader_count;
    pipeline_info.pStages = vk_shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr; // Optional
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline->vk_layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.pNext = &pipeline_create;

    (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pipeline_info,
                               vk_allocation_callbacks, &pipeline->vk_handle));
  } else {
    HASSERT_MSG(false, "Unknown pipeline type");
  }

  for (u32 i = 0; i < creation.shader_count; ++i) {
    vkDestroyShaderModule(vk_device, vk_shader_modules[i],
                          vk_allocation_callbacks);
  }

  set_resource_name(VK_OBJECT_TYPE_PIPELINE, (u64)pipeline->vk_handle,
                    creation.name);

  stack_allocator->free_marker(stack_marker);
  return handle;
}

void VulkanBackend::destroy_buffer(BufferHandle handle) {
  if (handle.index == k_invalid_index) {
    HWARN("Attempting to free an invalid VulkanBuffer");
    return;
  }
  VulkanBuffer *buffer = buffers.obtain(handle);
  VmaAllocationInfo alloc_info{};
  alloc_info.pMappedData = nullptr;
  vmaGetAllocationInfo(vma_allocator, buffer->vma_allocation, &alloc_info);

  if (alloc_info.pMappedData)
    vmaUnmapMemory(vma_allocator, buffer->vma_allocation);

  vmaDestroyBuffer(vma_allocator, buffer->vk_handle, buffer->vma_allocation);

  buffers.release(handle);
}

void VulkanBackend::destroy_pipeline(PipelineHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanPipeline");
    return;
  }
  // TODO: Add a destroy queue system
  vkDeviceWaitIdle(vk_device);
  VulkanPipeline *pipeline = pipelines.obtain(handle);

  for (u32 i = 0; i < pipeline->set_layout_count; ++i) {
    destroy_descriptor_set_layout(pipeline->set_layouts[i]);
  }
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  hfree(pipeline->set_layouts, allocator);

  vkDestroyPipelineLayout(vk_device, pipeline->vk_layout,
                          vk_allocation_callbacks);

  vkDestroyPipeline(vk_device, pipeline->vk_handle, vk_allocation_callbacks);

  pipelines.release(handle);
}

void VulkanBackend::destroy_descriptor_set_layout(
    DescriptorSetLayoutHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanDescriptorSetLayout");
    return;
  }
  VulkanDescriptorSetLayout *layout = descriptor_set_layouts.obtain(handle);
  for (u32 i = 0; i < layout->allocated_sets.size; ++i) {
    descriptor_sets.release(layout->allocated_sets[i]);
  }
  layout->allocated_sets.shutdown();
  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  hfree(layout->vk_bindings, allocator);
  vkDestroyDescriptorSetLayout(vk_device, layout->vk_handle,
                               vk_allocation_callbacks);

  descriptor_set_layouts.release(handle);
}

bool VulkanBackend::update_shader_uniform_set(ShaderUniformSet &set,
                                              PipelineHandle pipeline_handle) {
  DescriptorSetHandle handle = descriptor_sets.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a VulkanDescriptorSet resource!");
    return false;
  }
  VulkanDescriptorSet *d_set = descriptor_sets.obtain(handle);

  VulkanPipeline *pipeline = pipelines.obtain(pipeline_handle);
  if (pipeline_handle.index == k_invalid_index) {
    HERROR("Invalid VulkanPipeline resource!");
    return false;
  }

  VulkanDescriptorSetLayout *layout =
      descriptor_set_layouts.obtain(pipeline->set_layouts[set.set_index]);

  VkDescriptorSetLayout vk_layout = layout->vk_handle;
  VkDescriptorSetAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = vk_descriptor_pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &vk_layout;

  VK_CHECK(vkAllocateDescriptorSets(vk_device, &alloc_info, &d_set->vk_handle));

  // TODO: Assuming a max number of bindings for a descriptor set
  VkWriteDescriptorSet descriptor_writes[10];
  HASSERT(set.uniform_count <= 10);
  for (u32 i = 0; i < set.uniform_count; i++) {
    if (set.uniforms[i].resource_type == ResourceType::Buffer) {
      VkDescriptorBufferInfo buffer_info{};
      VulkanBuffer *buffer =
          buffers.obtain(set.uniforms[i].internal_resource_handle);
      buffer_info.buffer = buffer->vk_handle;
      buffer_info.offset = 0;
      buffer_info.range = sizeof(UniformBufferObject);

      VkWriteDescriptorSet &descriptor_write = descriptor_writes[i];
      descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptor_write.pNext = nullptr;
      descriptor_write.pImageInfo = nullptr;
      descriptor_write.pTexelBufferView = nullptr;
      descriptor_write.dstSet = d_set->vk_handle;
      descriptor_write.dstBinding = set.uniforms[i].binding;
      descriptor_write.dstArrayElement = 0;
      descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptor_write.descriptorCount = 1;
      descriptor_write.pBufferInfo = &buffer_info;
    } else {
      HERROR("Unkown descriptor type");
      return false;
    }

    vkUpdateDescriptorSets(vk_device, set.uniform_count, descriptor_writes, 0,
                           nullptr);
  }
  layout->allocated_sets.push(handle);

  return true;
}

void VulkanBackend::create_descriptor_pool(u32 max_frames_in_flight) {
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_size.descriptorCount = (max_frames_in_flight);

  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  pool_info.maxSets = max_frames_in_flight;

  VK_CHECK(vkCreateDescriptorPool(
      vk_device, &pool_info, vk_allocation_callbacks, &vk_descriptor_pool));
}

void VulkanBackend::draw_frame(RenderPacket *packet) {
  VK_CHECK(vkWaitForFences(vk_device, 1,
                           &in_flight_fences[packet->current_frame], VK_TRUE,
                           UINT64_MAX));

  uint32_t image_index;
  VkResult result =
      vkAcquireNextImageKHR(vk_device, swapchain.vk_handle, UINT64_MAX,
                            image_available_semaphores[packet->current_frame],
                            VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    resize_swapchain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    HERROR("Failed to acquire swap chain image!");
  }

  VK_CHECK(
      vkResetFences(vk_device, 1, &in_flight_fences[packet->current_frame]));

  VK_CHECK(vkResetCommandBuffer(vk_command_buffers[packet->current_frame], 0));

  update_uniform_buffer(packet);
  record_command_buffer(vk_command_buffers[packet->current_frame], image_index,
                        packet->current_frame);

  VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};

  VkSemaphore wait_semaphores[] = {
      image_available_semaphores[packet->current_frame]};
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &vk_command_buffers[packet->current_frame];

  VkSemaphore signal_semaphores[] = {
      render_finished_semaphores[packet->current_frame]};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  VK_CHECK(vkQueueSubmit(vk_graphics_queue, 1, &submit_info,
                         in_flight_fences[packet->current_frame]));

  VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;

  VkSwapchainKHR swapchains[] = {swapchain.vk_handle};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &image_index;
  present_info.pResults = nullptr;

  result = vkQueuePresentKHR(vk_graphics_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      resize_frame) {
    resize_frame = false;
    resize_swapchain();
  } else if (result != VK_SUCCESS) {
    HERROR("Failed to present swap chain image!");
  }
  ++frame_number;
}

void VulkanBackend::update_uniform_buffer(RenderPacket *packet) {

  UniformBufferObject ubo{};
  ubo.model = glm::mat4(1.f);
  // ubo.model =
  //     glm::rotate(ubo.model, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f,
  //     0.0f));
  // ubo.model =
  //     glm::rotate(ubo.model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f,
  //     0.0f));
  ubo.view = packet->camera->get_view();
  ubo.proj = glm::perspective(glm::radians(45.0f),
                              swapchain.vk_extents.width /
                                  (float)swapchain.vk_extents.height,
                              0.1f, 10.0f);
  ubo.proj[1][1] *= -1;

  VulkanBuffer *uniform_buffer = buffers.obtain(packet->scene_data_buffer);

  memcpy(uniform_buffer->mapped_data, &ubo, sizeof(ubo));
}

void VulkanBackend::copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                                VkDeviceSize size) {

  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandPool = vk_transfer_pool;
  alloc_info.commandBufferCount = 1;

  VkCommandBuffer command_buffer;
  vkAllocateCommandBuffers(vk_device, &alloc_info, &command_buffer);

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(command_buffer, &begin_info);

  VkBufferCopy copy_region{};
  copy_region.size = size;
  vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

  vkEndCommandBuffer(command_buffer);

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;

  vkQueueSubmit(vk_transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(vk_transfer_queue);

  vkFreeCommandBuffers(vk_device, vk_transfer_pool, 1, &command_buffer);
}

void VulkanBackend::upload_buffer_data(void *data, VkBuffer dst_buffer,
                                       u32 buffer_size) {
  VulkanBuffer staging_buffer{};
  vk_create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging_buffer);

  vmaMapMemory(vma_allocator, staging_buffer.vma_allocation, &data);
  memcpy(data, vertices.data, (size_t)buffer_size);
  vmaUnmapMemory(vma_allocator, staging_buffer.vma_allocation);

  copy_buffer(staging_buffer.vk_handle, vertex_buffer.vk_handle, buffer_size);

  vmaDestroyBuffer(vma_allocator, staging_buffer.vk_handle,
                   staging_buffer.vma_allocation);
}

void VulkanBackend::load_model() {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  bool res = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              ASSETS_PATH "/Models/Sponza/sponza.obj",
                              ASSETS_PATH "/Models/Sponza/");
  if (!warn.empty()) {
    HWARN("TINYOBJLOADER: {}", warn);
  }
  if (!err.empty()) {
    HERROR("TINYOBJLOADER: {}", err);
  }
  if (!res) {
    HERROR("TINYOBJLOADER: Failed to load .obj");
  }

  HDEBUG("# of vertices = {}", attrib.vertices.size() / 3);
  HDEBUG("# of normals = {}", attrib.normals.size() / 3);
  HDEBUG("# of texcoords = {}", attrib.texcoords.size() / 2);
  HDEBUG("# of materials = {}", materials.size());
  HDEBUG("# of shapes = {}", shapes.size());
  HDEBUG("# of indices in shape[0]: {}", shapes[0].mesh.indices.size());

  std::unordered_map<Vertex, uint32_t> unique_vertices{};
  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      Vertex vertex{};

      vertex.pos = {attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]};

      if (index.texcoord_index != -1) {
        vertex.tex_coord = {attrib.texcoords[2 * index.texcoord_index + 0],
                            1.0f -
                                attrib.texcoords[2 * index.texcoord_index + 1]};
      }
      if (unique_vertices.count(vertex) == 0) {
        unique_vertices[vertex] = static_cast<u32>(vertices.size);
        vertices.push(vertex);
      }
      indexes.push(unique_vertices[vertex]);
    }
  }
  HDEBUG("Vertex size = {}", vertices.size);
}

void VulkanBackend::set_resource_name(VkObjectType type, u64 handle,
                                      cstring name) {
#ifdef VULKAN_DEBUG_REPORT
  VkDebugUtilsObjectNameInfoEXT name_info = {
      VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
  name_info.objectType = type;
  name_info.objectHandle = handle;
  name_info.pObjectName = name;
  pfnSetDebugUtilsObjectNameEXT(vk_device, &name_info);
#endif // VULKAN_DEBUG_REPORT
}

#pragma region HelperFunctions

static VkBool32
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
               VkDebugUtilsMessageTypeFlagsEXT message_type,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data) {

  if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    HERROR(" MessageID: {} {}\nMessage: {}\n", callback_data->pMessageIdName,
           callback_data->messageIdNumber, callback_data->pMessage);
    HELIX_DEBUG_BREAK;
  } else if (message_severity &
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    HWARN(" MessageID: {} {}\nMessage: {}\n", callback_data->pMessageIdName,
          callback_data->messageIdNumber, callback_data->pMessage);
  }

  return VK_FALSE;
}

static bool select_physical_device(VkInstance instance,
                                   VkPhysicalDevice &_physical_device,
                                   QueueFamilyIndices &indices,
                                   VkSurfaceKHR surface) {
  u32 physical_device_count = 0;
  VK_CHECK(
      vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));

  if (physical_device_count == 0) {
    HERROR("Failed to find a GPU that supports Vulkan!");
    return false;
  }

  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();

  Array<VkPhysicalDevice> physical_devices{};
  physical_devices.init(stack_allocator, physical_device_count,
                        physical_device_count);

  VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                      physical_devices.data));

  bool found_suitable_device = false;
  for (u32 i = 0; i < physical_device_count; ++i) {
    VkPhysicalDevice device = physical_devices[i];
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);

    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                         nullptr);

    Array<VkExtensionProperties> extension_properties{};
    extension_properties.init(stack_allocator, extension_count,
                              extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                         extension_properties.data);

    // TODO: Use a set instead
    bool extensions_supported = false;
    for (u32 idx = 0; idx < extension_properties.size; ++idx) {
      if (!strcmp(extension_properties[idx].extensionName,
                  VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        extensions_supported = true;
        break;
      }
    }
    if (!extensions_supported) {
      break;
    }

    // Check if device has suitable queue family
    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                             nullptr);

    Array<VkQueueFamilyProperties> queue_families{};
    queue_families.init(stack_allocator, queue_family_count,
                        queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                             queue_families.data);
    for (u32 idx = 0; idx < queue_family_count; ++idx) {
      VkBool32 present_queue_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, idx, surface,
                                           &present_queue_support);
      if (present_queue_support &&
          queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
          indices.graphics_family_index == UINT32_MAX) {
        indices.graphics_family_index = idx;
        continue;
      }

      if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
        indices.transfer_family_index = idx;
        continue;
      }
    }
    if (indices.is_complete()) {
      found_suitable_device = true;
      _physical_device = device;
      HTRACE("Suitable device found: {}", device_properties.deviceName);
    }

    queue_families.shutdown();
    if (found_suitable_device) {
      break;
    }
  }
  stack_allocator->free_marker(stack_marker);
  return found_suitable_device;
}

static void query_swapchain_support(VkPhysicalDevice physical_device,
                                    VkSurfaceKHR surface,
                                    VulkanSwapchain &swapchain) {
  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  size_t stack_marker = stack_allocator->get_marker();
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                            &capabilities);
  u32 format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count,
                                       nullptr);
  HASSERT(format_count != 0);
  VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)halloca(
      sizeof(VkSurfaceFormatKHR) * format_count, stack_allocator);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count,
                                       formats);

  u32 present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                            &present_mode_count, nullptr);
  HASSERT(present_mode_count != 0);
  VkPresentModeKHR *present_modes = (VkPresentModeKHR *)halloca(
      sizeof(VkPresentModeKHR) * present_mode_count, stack_allocator);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                            &format_count, present_modes);

  const VkFormat preferred_surface_image_formats[] = {
      VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
  const VkColorSpaceKHR preferred_surface_color_space =
      VK_COLORSPACE_SRGB_NONLINEAR_KHR;

  u32 surface_format_count = ArraySize(preferred_surface_image_formats);
  bool format_found = false;
  for (u32 i = 0; i < surface_format_count; ++i) {
    for (u32 j = 0; j < format_count; j++) {
      if (formats[j].format == preferred_surface_image_formats[i] &&
          formats[j].colorSpace == preferred_surface_color_space) {
        swapchain.vk_surface_format = formats[j];
        format_found = true;
        break;
      }
    }
    if (format_found)
      break;
  }
  // Default to the first format
  if (!format_found) {
    swapchain.vk_surface_format = formats[0];
    HWARN("Could not find preferred surface format, defaulting to first "
          "available format");
  }

  bool present_mode_found = false;
  for (u32 i = 0; i < present_mode_count; ++i) {
    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      swapchain.vk_present_mode = present_modes[i];
      present_mode_found = true;
      break;
    }
  }
  if (!present_mode_found) {
    swapchain.vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    HWARN("Could not find preferred present mode, defaulting to FIFO");
  }

  if (capabilities.currentExtent.width != UINT32_MAX) {
    swapchain.vk_extents = capabilities.currentExtent;
  } else {
    Platform *platform = Platform::instance();
    VkExtent2D extents = {(u32)platform->width, (u32)platform->height};

    swapchain.vk_extents.width =
        clamp(extents.width, capabilities.minImageExtent.width,
              capabilities.maxImageExtent.width);
    swapchain.vk_extents.height =
        clamp(extents.height, capabilities.minImageExtent.height,
              capabilities.maxImageExtent.height);
  }

  u32 image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  swapchain.image_count = image_count;

  stack_allocator->free_marker(stack_marker);
}

u32 find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties,
                     VkPhysicalDevice physical_device) {
  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

  for (u32 i = 0; i < mem_properties.memoryTypeCount; i++) {
    if (type_filter & (1 << i) && (mem_properties.memoryTypes[i].propertyFlags &
                                   properties) == properties) {
      return i;
    }
  }
  HASSERT_MSG(false, "Failed to find suitable memory type!");
  return 0;
}

cstring to_compiler_stage(ShaderStage::Enum stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return "vert";
  case ShaderStage::Fragment:
    return "frag";
  case ShaderStage::Compute:
    return "comp";
  default:
    HERROR("Unknown shader stage!");
    return nullptr;
  }
}

VkShaderStageFlagBits to_vk_shader_stage(ShaderStage::Enum stage) {
  switch (stage) {
  case ShaderStage::Vertex:
    return VK_SHADER_STAGE_VERTEX_BIT;
  case ShaderStage::Fragment:
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  case ShaderStage::Compute:
    return VK_SHADER_STAGE_COMPUTE_BIT;
  }
}
#pragma endregion HelperFunctions

} // namespace Helix
