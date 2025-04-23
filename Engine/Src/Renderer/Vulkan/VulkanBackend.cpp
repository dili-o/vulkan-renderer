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
#include "Renderer/RendererFrontEnd.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/CommandBuffer.hpp"
#include "Renderer/Vulkan/VulkanTypes.hpp"
#include "Renderer/Vulkan/VulkanUtils.hpp"
#include "SpirvParser.hpp"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <cstring>

#ifdef _DEBUG
#define VULKAN_DEBUG_REPORT
#define VULKAN_EXTRA_VALIDATION
#endif // _DEBUG

#define MIN_BUFFER_SIZE 4
#define MAX_TEXTURES 128

namespace Helix {

#pragma region HelperFunctions

static VkBool32
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
               VkDebugUtilsMessageTypeFlagsEXT message_type,
               const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
               void *user_data);

static bool
select_physical_device(VkInstance instance, VkPhysicalDevice &_physical_device,
                       VkPhysicalDeviceProperties *device_properties,
                       QueueFamilyIndices &queue_family_indices,
                       VkSurfaceKHR surface);

static void query_swapchain_support(VkPhysicalDevice physical_device,
                                    VkSurfaceKHR surface,
                                    VulkanSwapchain &swapchain);

u32 find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties,
                     VkPhysicalDevice physical_device);

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

  buffers.init(allocator, 10);
  pipelines.init(allocator, 10);
  descriptor_set_layouts.init(allocator, 10);
  descriptor_sets.init(allocator, 10);
  images.init(allocator, 10);
  image_views.init(allocator, 10);
  samplers.init(allocator, 10);

  bindless_textures_to_update.init(allocator, 10);
  string_buffer.init(allocator, hkilo(15));

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
#if defined(VULKAN_EXTRA_VALIDATION)
  const VkValidationFeatureEnableEXT features_requested[] = {
      VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};
  VkValidationFeaturesEXT features = {};
  features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
  features.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_create_info;
  features.enabledValidationFeatureCount = ArraySize(features_requested);
  features.pEnabledValidationFeatures = features_requested;
  create_info.pNext = &features;
#else
  create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debug_create_info;
#endif // VULKAN_EXTRA_VALIDATION

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

  Array<cstring> device_extensions{};
  device_extensions.init(stack_allocator, 1, 1);
  device_extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  // Create Physical Device
  if (!select_physical_device(vk_instance, vk_physical_device,
                              &vk_physical_device_properties,
                              queue_family_indices, vk_surface)) {
    HERROR("Failed to create physical device!");
    return false;
  }

  // Create Logical Device
  Array<VkDeviceQueueCreateInfo> queue_create_infos{};
  queue_create_infos.init(stack_allocator, 1);
  f32 queue_priority = 1.f;

  VkDeviceQueueCreateInfo main_queue_info{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  main_queue_info.queueFamilyIndex = queue_family_indices.graphics_family_index;
  main_queue_info.queueCount = 1;
  main_queue_info.pQueuePriorities = &queue_priority;
  queue_create_infos.push(main_queue_info);

  if (queue_family_indices.graphics_family_index !=
      queue_family_indices.transfer_family_index) {
    VkDeviceQueueCreateInfo queue_create_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex =
        queue_family_indices.transfer_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push(queue_create_info);
  }

  VkPhysicalDeviceFeatures device_features{};
  // TODO: Check if this is available
  device_features.samplerAnisotropy = VK_TRUE;
  device_features.fillModeNonSolid = VK_TRUE;
  device_features.geometryShader = VK_TRUE;

  VkDeviceCreateInfo device_create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_create_info.pQueueCreateInfos = queue_create_infos.data;
  device_create_info.queueCreateInfoCount = queue_create_infos.size;
  device_create_info.pEnabledFeatures = &device_features;
  device_create_info.enabledExtensionCount = device_extensions.size;
  device_create_info.ppEnabledExtensionNames = device_extensions.data;

  // Timeline semaphores
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_feature{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  timeline_feature.timelineSemaphore = VK_TRUE;

  // Enable Bindless descriptors
  VkPhysicalDeviceDescriptorIndexingFeatures bindless_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  bindless_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  bindless_features.runtimeDescriptorArray = VK_TRUE;
  // bindless_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
  bindless_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  bindless_features.descriptorBindingPartiallyBound = VK_TRUE;
  bindless_features.pNext = &timeline_feature;

  // Enable Dynamic Rendering and Synchronization 2
  VkPhysicalDeviceVulkan13Features features13 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features13.pNext = &bindless_features;

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

  //  Get the function pointers to Debug Utils functions.
  if (debug_utils_extension_present) {
    pfnSetDebugUtilsObjectNameEXT =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
            vk_device, "vkSetDebugUtilsObjectNameEXT");
    pfnCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            vk_device, "vkCmdBeginDebugUtilsLabelEXT");
    pfnCmdInsertDebugUtilsLabelEXT =
        (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            vk_device, "vkCmdInsertDebugUtilsLabelEXT");
    pfnCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            vk_device, "vkCmdEndDebugUtilsLabelEXT");

    HASSERT(pfnSetDebugUtilsObjectNameEXT);
    HASSERT(pfnCmdBeginDebugUtilsLabelEXT);
    HASSERT(pfnCmdInsertDebugUtilsLabelEXT);
    HASSERT(pfnCmdEndDebugUtilsLabelEXT);
  }

  vkGetDeviceQueue(vk_device, queue_family_indices.graphics_family_index, 0,
                   &vk_graphics_queue);
  set_resource_name(VK_OBJECT_TYPE_QUEUE, (u64)vk_graphics_queue,
                    "Graphics_queue");
  vk_transfer_queue = vk_graphics_queue;
  if (queue_family_indices.graphics_family_index !=
      queue_family_indices.transfer_family_index) {
    vkGetDeviceQueue(vk_device, queue_family_indices.transfer_family_index, 0,
                     &vk_transfer_queue);

    set_resource_name(VK_OBJECT_TYPE_QUEUE, (u64)vk_transfer_queue,
                      "Transfer_queue");
  }

  queue_create_infos.shutdown();

  command_buffer_manager.init(this, queue_family_indices.graphics_family_index,
                              Platform::get_logical_processor_count(),
                              config->max_frames_in_flight,
                              "Graphics_CommandPool");
  transfer_command_buffer_manager.init(
      this, queue_family_indices.transfer_family_index,
      Platform::get_logical_processor_count(), 1, "Transfer_CommandPool");

  // Create swapchain
  create_swapchain();

  create_sync_objects(config->max_frames_in_flight);
  create_descriptor_pool(config->max_frames_in_flight);
  {
    SamplerCreation sampler_creation{};
    sampler_creation.name = "default_sampler";
    default_sampler = create_sampler(sampler_creation);
  }

  resource_deletion_queue.init(allocator, 10);

  frame_number = 0;
  HINFO("Vulkan Backend Initialized");

  return true;
}

bool VulkanBackend::shutdown() {
  vkDeviceWaitIdle(vk_device);

  destroy_swapchain();
  destroy_sampler(default_sampler);
  free_queued_resources();
  resource_deletion_queue.shutdown();

  for (u32 i = 0; i < 2; ++i) {
    vkDestroySemaphore(vk_device, image_available_semaphores[i],
                       vk_allocation_callbacks);
    vkDestroySemaphore(vk_device, render_finished_semaphores[i],
                       vk_allocation_callbacks);
  }

  vkDestroySemaphore(vk_device, vk_timeline_graphics_semaphore,
                     vk_allocation_callbacks);

  image_available_semaphores.shutdown();
  render_finished_semaphores.shutdown();

  vkDestroyDescriptorPool(vk_device, vk_descriptor_pool,
                          vk_allocation_callbacks);
  vkDestroyDescriptorPool(vk_device, vk_bindless_descriptor_pool,
                          vk_allocation_callbacks);
  vkDestroyDescriptorSetLayout(vk_device, vk_bindless_descriptor_layout,
                               vk_allocation_callbacks);

  command_buffer_manager.shutdown();
  transfer_command_buffer_manager.shutdown();

  pipelines.shutdown();
  descriptor_set_layouts.shutdown();
  descriptor_sets.shutdown();
  buffers.shutdown();
  images.shutdown();
  image_views.shutdown();
  samplers.shutdown();

  bindless_textures_to_update.shutdown();
  vmaDestroyAllocator(vma_allocator);

  vkDestroyDevice(vk_device, vk_allocation_callbacks);
  vkDestroySurfaceKHR(vk_instance, vk_surface, vk_allocation_callbacks);
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      vk_instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(vk_instance, vk_debug_utils_messenger, vk_allocation_callbacks);
  }
  vkDestroyInstance(vk_instance, vk_allocation_callbacks);

  string_buffer.shutdown();

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
  create_swapchain();
}

bool VulkanBackend::begin_frame(RenderPacket *packet) {
  u64 wait_value = frame_number < max_frames_in_flight ? 0 : frame_number;
  VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wait_info.semaphoreCount = 1;
  wait_info.pSemaphores = &vk_timeline_graphics_semaphore;
  wait_info.pValues = &wait_value;
  vkWaitSemaphores(vk_device, &wait_info, UINT64_MAX);

  VkResult result =
      vkAcquireNextImageKHR(vk_device, swapchain.vk_handle, UINT64_MAX,
                            image_available_semaphores[packet->current_frame],
                            VK_NULL_HANDLE, &swapchain.current_image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    resize_swapchain();
    return false;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    HERROR("Failed to acquire swap chain image!");
  }

  update_uniform_buffer(packet);

  VulkanCommandBuffer *command_buffer =
      command_buffer_manager.get_command_buffer(packet->current_frame, 0,
                                                false);

  command_buffer->reset();
  command_buffer->begin();

  return true;
}

bool VulkanBackend::end_frame(RenderPacket *packet) {

  VulkanCommandBuffer *command_buffer =
      command_buffer_manager.get_command_buffer(packet->current_frame, 0,
                                                false);
  command_buffer->end_current_renderpass();

  // Transition to Present
  command_buffer->transition_image(
      &swapchain.images[swapchain.current_image_index],
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

  command_buffer->end();

  // Update Bindless Textures
  if (bindless_textures_to_update.size) {
    // Handle deferred writes to bindless textures.
    VkWriteDescriptorSet bindless_descriptor_writes[MAX_TEXTURES];
    VkDescriptorImageInfo bindless_image_info[MAX_TEXTURES];

    u32 current_write_index = 0;
    for (i32 it = bindless_textures_to_update.size - 1; it >= 0; it--) {
      TextureHandle texture_to_update = bindless_textures_to_update[it];
      {
        VulkanImageView *texture = access_image_view(texture_to_update);
        VkWriteDescriptorSet &descriptor_write =
            bindless_descriptor_writes[current_write_index];
        descriptor_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        descriptor_write.descriptorCount = 1;
        descriptor_write.dstArrayElement = texture_to_update.index;
        descriptor_write.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.dstSet = vk_bindless_descriptor_set;
        descriptor_write.dstBinding = 0;

        VulkanSampler *vk_default_sampler = access_sampler(default_sampler);
        VkDescriptorImageInfo &descriptor_image_info =
            bindless_image_info[current_write_index];

        // TODO: Texture( Views ) should have samplers
        descriptor_image_info.sampler = vk_default_sampler->vk_handle;

        descriptor_image_info.imageView = texture->vk_handle;
        descriptor_image_info.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_write.pImageInfo = &descriptor_image_info;

        bindless_textures_to_update.delete_swap(it);

        ++current_write_index;
      }
    }

    if (current_write_index) {
      vkUpdateDescriptorSets(vk_device, current_write_index,
                             bindless_descriptor_writes, 0, nullptr);
    }
  }

  // Submit
  VkCommandBufferSubmitInfo command_submit_info{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  command_submit_info.commandBuffer = command_buffer->vk_handle;

  VkSemaphoreSubmitInfo wait_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait_semaphore_submit_info.semaphore =
      image_available_semaphores[packet->current_frame];
  wait_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signal_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal_semaphore_submit_info.semaphore =
      render_finished_semaphores[packet->current_frame];
  signal_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphoreSubmitInfo signal_timeline_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal_timeline_semaphore_submit_info.semaphore =
      vk_timeline_graphics_semaphore;
  signal_timeline_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  signal_timeline_semaphore_submit_info.value =
      frame_number + max_frames_in_flight;

  VkSemaphoreSubmitInfo signal_semaphore_submit_infos[2] = {
      signal_semaphore_submit_info, signal_timeline_semaphore_submit_info};

  VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit_info.commandBufferInfoCount = 1;
  submit_info.pCommandBufferInfos = &command_submit_info;
  submit_info.waitSemaphoreInfoCount = 1;
  submit_info.pWaitSemaphoreInfos = &wait_semaphore_submit_info;
  submit_info.signalSemaphoreInfoCount =
      ArraySize(signal_semaphore_submit_infos);
  submit_info.pSignalSemaphoreInfos = signal_semaphore_submit_infos;

  VkSemaphore signal_semaphores[] = {
      render_finished_semaphores[packet->current_frame]};

  VK_CHECK(vkQueueSubmit2(vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

  VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;

  VkSwapchainKHR swapchains[] = {swapchain.vk_handle};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &swapchain.current_image_index;
  present_info.pResults = nullptr;

  VkResult result = vkQueuePresentKHR(vk_graphics_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      resize_frame) {
    resize_frame = false;
    resize_swapchain();
  } else if (result != VK_SUCCESS) {
    HERROR("Failed to present swap chain image!");
  }

  free_queued_resources();
  ++frame_number;

  return true;
}

void VulkanBackend::create_swapchain() {
  query_swapchain_support(vk_physical_device, vk_surface, swapchain);
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

  StackAllocator *stack_allocator = &MemoryService::instance()->stack_allocator;
  HASSERT(swapchain.image_count <= MAX_SWAPCHAIN_IMAGES);
  vkGetSwapchainImagesKHR(vk_device, swapchain.vk_handle,
                          &swapchain.image_count, nullptr);
  VkImage *vk_images = (VkImage *)halloca(
      sizeof(VkImage) * swapchain.image_count, stack_allocator);
  vkGetSwapchainImagesKHR(vk_device, swapchain.vk_handle,
                          &swapchain.image_count, vk_images);

  VulkanCommandBuffer *command_buffer =
      command_buffer_manager.get_command_buffer(0, 0, true);

  for (u32 i = 0; i < swapchain.image_count; ++i) {
    // Create VulkanImage resources for the swapchain images
    swapchain.images[i].vk_handle = vk_images[i];
    swapchain.images[i].current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchain.images[i].format = swapchain.vk_surface_format.format;
    swapchain.images[i].vk_extents = {swapchain.vk_extents.width,
                                      swapchain.vk_extents.height, 1};

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = swapchain.images[i].vk_handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = swapchain.vk_surface_format.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(vk_device, &view_info, vk_allocation_callbacks,
                               &swapchain.image_views[i].vk_handle));

    command_buffer->transition_image(
        &swapchain.images[i], swapchain.images[i].current_layout,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  }

  command_buffer->end();

  VkCommandBufferSubmitInfo command_submit_info{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  command_submit_info.commandBuffer = command_buffer->vk_handle;

  VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit_info.flags = 0;
  submit_info.commandBufferInfoCount = 1;
  submit_info.pCommandBufferInfos = &command_submit_info;

  vkQueueSubmit2(vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(vk_graphics_queue);

  TextureCreation tex_creation{};
  tex_creation.initial_data = nullptr;
  tex_creation.width = swapchain.vk_extents.width;
  tex_creation.height = swapchain.vk_extents.height;
  tex_creation.depth = 1;
  tex_creation.array_layer_count = 1;
  tex_creation.array_base_level = 0;
  tex_creation.mip_level_count = 1;
  tex_creation.mip_base_level = 0;
  tex_creation.usage =
      TextureUsage::Enum(TextureUsage::Depth | TextureUsage::Sampled);
  tex_creation.alias_image = {k_invalid_index, 0};
  tex_creation.format = TextureFormat::D32;
  tex_creation.type = TextureType::Texture2D;
  tex_creation.name = "depth";

  depth_handle = create_texture(tex_creation);

  HTRACE("Created swapchain successfully");
}

void VulkanBackend::destroy_swapchain() {
  for (u32 i = 0; i < swapchain.image_count; ++i) {
    vkDestroyImageView(vk_device, swapchain.image_views[i].vk_handle,
                       vk_allocation_callbacks);
    swapchain.images[i].vk_handle = VK_NULL_HANDLE;
    swapchain.image_views[i].vk_handle = VK_NULL_HANDLE;
  }

  VulkanImageView *depth_view = image_views.obtain(depth_handle);
  destroy_image_instant(depth_view->image);
  destroy_image_view_instant(depth_handle);

  vkDestroySwapchainKHR(vk_device, swapchain.vk_handle,
                        vk_allocation_callbacks);
  swapchain.vk_handle = VK_NULL_HANDLE;
}

void VulkanBackend::record_command_buffer(VulkanCommandBuffer *command_buffer,
                                          RenderPacket *packet,
                                          u32 current_frame) {
  command_buffer->push_marker("Frame");

  command_buffer->transition_image(
      &swapchain.images[swapchain.current_image_index],
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

  VulkanImageView *depth_view = image_views.obtain(depth_handle);
  VulkanImage *depth_image = images.obtain(depth_view->image);
  command_buffer->transition_image(
      depth_view->image, depth_image->current_layout,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);

  command_buffer->bind_renderpass(
      {swapchain.vk_extents.width, swapchain.vk_extents.height},
      swapchain.image_views[swapchain.current_image_index].vk_handle);

  // TODO: Renderpass and Framebuffer struct
  // TODO:
  VulkanPipeline *pipeline = access_pipeline({0, 0});
  command_buffer->bind_pipeline({0, 0});

  command_buffer->bind_viewport(swapchain.vk_extents);

  VkRect2D rect{};
  rect.offset = {0, 0};
  rect.extent = swapchain.vk_extents;
  command_buffer->bind_scissors(rect);

  // TODO:
  VulkanDescriptorSetLayout *dset_layout =
      access_descriptor_set_layout(pipeline->set_layouts[0]);
  VulkanDescriptorSet *dset =
      access_descriptor_set(dset_layout->allocated_sets[current_frame]);

  command_buffer->bind_descriptor_sets({0, 0}, vk_bindless_descriptor_set, 0);
  command_buffer->bind_descriptor_sets({0, 0}, dset->vk_handle, 1);

  for (u32 i = 0; i < packet->mesh_count; ++i) {

    Mesh &mesh = packet->meshes[i];
    command_buffer->bind_vertex_buffer(mesh.internal_vertex_buffer, 0, 1);

    for (u32 j = 0; j < mesh.draws.size; ++j) {
      MeshDraw &draw = mesh.draws[j];
      command_buffer->bind_index_buffer(draw.internal_index_buffer, 0,
                                        VK_INDEX_TYPE_UINT32);

      PBRMaterial &material =
          RendererFrontEnd::instance()->pbr_materials[draw.material_index];

      TextureResource *texture_resource =
          RendererFrontEnd::instance()->textures.obtain(
              material.albedo_texture_handle);
      command_buffer->push_constants(
          pipeline->vk_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(u32),
          &texture_resource->internal_handle.index);

      command_buffer->draw_indexed(draw.primitive_count, 1, 0, 0, 0);
    }
  }

  // NOTE: Imgui needs to use the renderpass
  // command_buffer->end_current_renderpass();

  command_buffer->pop_marker();
}

void VulkanBackend::create_sync_objects(u32 max_frames_in_flight) {

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  image_available_semaphores.init(allocator, max_frames_in_flight,
                                  max_frames_in_flight);
  render_finished_semaphores.init(allocator, max_frames_in_flight,
                                  max_frames_in_flight);

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  for (u32 i = 0; i < max_frames_in_flight; ++i) {
    VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                               vk_allocation_callbacks,
                               &image_available_semaphores[i]));
    VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                               vk_allocation_callbacks,
                               &render_finished_semaphores[i]));
  }

  VkSemaphoreTypeCreateInfo timeline_create_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timeline_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timeline_create_info.initialValue = 0;
  semaphore_info.pNext = &timeline_create_info;

  VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                             vk_allocation_callbacks,
                             &vk_timeline_graphics_semaphore));
}

SamplerHandle VulkanBackend::create_sampler(SamplerCreation &creation) {
  SamplerHandle handle = samplers.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain VulkanSampler");
    return handle;
  }
  VulkanSampler *sampler = access_sampler(handle);

  VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.minFilter = creation.min_filter;
  sampler_info.magFilter = creation.mag_filter;
  sampler_info.minLod = 0.f;
  sampler_info.maxLod = VK_LOD_CLAMP_NONE;
  sampler_info.addressModeU = creation.address_mode_u;
  sampler_info.addressModeV = creation.address_mode_v;
  sampler_info.addressModeW = creation.address_mode_w;
  sampler_info.mipmapMode = creation.mip_filter;
  sampler_info.anisotropyEnable = VK_TRUE;
  sampler_info.maxAnisotropy =
      vk_physical_device_properties.limits.maxSamplerAnisotropy;
  sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  sampler_info.unnormalizedCoordinates = VK_FALSE;
  sampler_info.compareEnable = VK_FALSE;
  sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;

  VK_CHECK(vkCreateSampler(vk_device, &sampler_info, vk_allocation_callbacks,
                           &sampler->vk_handle));
  set_resource_name(VK_OBJECT_TYPE_SAMPLER, (u64)sampler->vk_handle,
                    creation.name);
  return handle;
}

// Maybe add checks? MAYBE INLINE AS WELL
VulkanBuffer *VulkanBackend::access_buffer(BufferHandle handle) {
  return buffers.obtain(handle);
}

VulkanPipeline *VulkanBackend::access_pipeline(PipelineHandle handle) {
  return pipelines.obtain(handle);
}

VulkanDescriptorSetLayout *
VulkanBackend::access_descriptor_set_layout(DescriptorSetLayoutHandle handle) {
  return descriptor_set_layouts.obtain(handle);
}

VulkanDescriptorSet *
VulkanBackend::access_descriptor_set(DescriptorSetHandle handle) {
  return descriptor_sets.obtain(handle);
}

VulkanImage *VulkanBackend::access_image(TextureHandle handle) {
  return images.obtain(handle);
}

VulkanImageView *VulkanBackend::access_image_view(TextureHandle handle) {
  return image_views.obtain(handle);
}

VulkanSampler *VulkanBackend::access_sampler(SamplerHandle handle) {
  return samplers.obtain(handle);
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

  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size =
      creation.size < MIN_BUFFER_SIZE ? MIN_BUFFER_SIZE : creation.size;

  if (creation.usage_flags == BufferUsage::None) {
    HERROR("Creating a buffer with no usage flags");
  }

  buffer_info.usage = to_vk_buffer_usage_flags(creation.usage_flags);
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = creation.memory_access_flags & MemoryAccess::GPU_ONLY
                          ? VMA_MEMORY_USAGE_GPU_ONLY
                          : VMA_MEMORY_USAGE_AUTO;
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

  VulkanBuffer *buffer = access_buffer(handle);
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

  buffer->name = creation.name;
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

  VulkanPipeline *pipeline = access_pipeline(handle);
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

  Directory dir{};
  file_service->current_directory(&dir);
  file_service->change_directory(ASSETS_PATH "/Shaders/");

  ParseResult parse_result{};
  parse_result.push_constant.size = 0;
  parse_result.set_layouts.init(stack_allocator, 4);

  // Create shader spv and extract shader data from them
  for (u32 i = 0; i < creation.shader_count; ++i) {
    ShaderCreateInfo shader = creation.shader_create_infos[i];
    cstring shader_args = temp_string_buffer.append_use_f(
        " -fshader-stage=%s %s.glsl -o %s.spv --target-env=vulkan1.3 %s",
        to_compiler_stage(shader.stage), shader.filename, shader.filename,
        compiler_debug);
    HASSERT(process_execute(".", glsl_compiler_path, shader_args));

    // TODO: Maybe create a timestamp system for checking shaders.

    cstring binary_name =
        temp_string_buffer.append_use_f("%s.spv", shader.filename);
    FileReadResult shader_binary{};
    file_service->open_read_file_binary(binary_name, &shader_binary,
                                        stack_allocator);

    if (shader_binary.data == nullptr) {
      FileReadResult glsl_code{};
      file_service->open_read_file_binary(
          temp_string_buffer.append_use_f("%s.glsl", shader.filename),
          &glsl_code, stack_allocator);
      if (glsl_code.data)
        HTRACE("\n{}", glsl_code.data);
      HERROR("\n{}", process_get_output());
    }
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
      1; // TODO: For now assume only one vertex binding
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
  rasterizer.cullMode = to_vk_cull_mode_flags(creation.cull_mode);
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
  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.minDepthBounds = 0.0f;
  depth_stencil.maxDepthBounds = 1.0f;

  // Color Blend State
  // TODO: Make configurable
  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor =
      VK_BLEND_FACTOR_SRC_ALPHA; // Optional
  color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;                          // Optional
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;            // Optional
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
  color_blend_attachment.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;               // Optional
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

  VkPipelineColorBlendStateCreateInfo color_blending{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_CLEAR;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f; // Optional
  color_blending.blendConstants[1] = 0.0f; // Optional
  color_blending.blendConstants[2] = 0.0f; // Optional
  color_blending.blendConstants[3] = 0.0f; // Optional

  // Descriptor Set Layouts
  VkDescriptorSetLayout *layouts = (VkDescriptorSetLayout *)halloca(
      sizeof(VkDescriptorSetLayout) * (parse_result.set_layouts.size + 1),
      stack_allocator);

  pipeline->set_layouts = (DescriptorSetLayoutHandle *)halloca(
      sizeof(DescriptorSetLayoutHandle) * parse_result.set_layouts.size,
      allocator);

  pipeline->set_layout_count = parse_result.set_layouts.size;

  // Sort by set_index in ascending order
  if (pipeline->set_layout_count) {
    std::sort(&parse_result.set_layouts[0],
              &parse_result.set_layouts[0] + parse_result.set_layouts.size,
              [](const VulkanDescriptorSetLayout &a,
                 const VulkanDescriptorSetLayout &b) {
                return a.set_index < b.set_index;
              });
  }

  for (u32 i = 0; i < parse_result.set_layouts.size; ++i) {
    DescriptorSetLayoutHandle dset_layout_handle =
        descriptor_set_layouts.obtain_new();
    if (dset_layout_handle.index != k_invalid_index) {
      VulkanDescriptorSetLayout *dset_layout =
          access_descriptor_set_layout(dset_layout_handle);

      dset_layout->vk_bindings = parse_result.set_layouts[i].vk_bindings;
      dset_layout->set_index = parse_result.set_layouts[i].set_index;
      dset_layout->allocated_sets.init(allocator, 4);

      VkDescriptorSetLayoutCreateInfo layout_info{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      layout_info.bindingCount = dset_layout->vk_bindings.size;
      layout_info.pBindings = dset_layout->vk_bindings.data;

      VK_CHECK(vkCreateDescriptorSetLayout(vk_device, &layout_info,
                                           vk_allocation_callbacks,
                                           &dset_layout->vk_handle));
      // Adding 1 because of bindless set index at set == 0
      layouts[i + 1] = dset_layout->vk_handle;
      pipeline->set_layouts[i] = dset_layout_handle;
    }
  }

  layouts[0] = vk_bindless_descriptor_layout;

  // Pipeline Layout
  VkPipelineLayoutCreateInfo pipeline_layout_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_info.setLayoutCount = parse_result.set_layouts.size + 1;
  pipeline_layout_info.pSetLayouts = layouts;
  if (parse_result.push_constant.size != 0) {
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &parse_result.push_constant;
  }

  VK_CHECK(vkCreatePipelineLayout(vk_device, &pipeline_layout_info,
                                  vk_allocation_callbacks,
                                  &pipeline->vk_layout));
  // Dynamic Rendering
  VkPipelineRenderingCreateInfoKHR pipeline_create{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
  pipeline_create.pNext = VK_NULL_HANDLE;
  pipeline_create.colorAttachmentCount = 1;
  pipeline_create.pColorAttachmentFormats = &swapchain.vk_surface_format.format;
  pipeline_create.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
  pipeline_create.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

  // Pipeline Cache
  VkPipelineCache pipeline_cache{VK_NULL_HANDLE};
  VkPipelineCacheCreateInfo pipeline_cache_create_info{
      VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};

  cstring cache_path =
      temp_string_buffer.append_use_f("%s\\%s.cache", "Caches", creation.name);
  bool cache_exists = file_service->file_exists(cache_path);
  if (cache_exists) {
    FileReadResult read_result{};
    file_service->open_read_file_binary(cache_path, &read_result, allocator);
    VkPipelineCacheHeaderVersionOne *cache_header =
        (VkPipelineCacheHeaderVersionOne *)read_result.data;

    if (cache_header->deviceID == vk_physical_device_properties.deviceID &&
        cache_header->vendorID == vk_physical_device_properties.vendorID &&
        memcmp(cache_header->pipelineCacheUUID,
               vk_physical_device_properties.pipelineCacheUUID,
               VK_UUID_SIZE) == 0) {
      pipeline_cache_create_info.initialDataSize = read_result.size;
      pipeline_cache_create_info.pInitialData = read_result.data;
    } else {
      cache_exists = false;
    }

    VK_CHECK(vkCreatePipelineCache(vk_device, &pipeline_cache_create_info,
                                   vk_allocation_callbacks, &pipeline_cache));

    allocator->deallocate(read_result.data);

  } else {
    HDEBUG("Failed to find pipeline cache for: {}", creation.name);
    VK_CHECK(vkCreatePipelineCache(vk_device, &pipeline_cache_create_info,
                                   vk_allocation_callbacks, &pipeline_cache));
  }

  if (creation.pipeline_type == PipelineType::Graphics) {
    pipeline->bind_point = to_vk_bind_point(creation.pipeline_type);

    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = creation.shader_count;
    pipeline_info.pStages = vk_shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil; // Optional
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline->vk_layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.pNext = &pipeline_create;

    (vkCreateGraphicsPipelines(vk_device, pipeline_cache, 1, &pipeline_info,
                               vk_allocation_callbacks, &pipeline->vk_handle));
  } else {
    HASSERT_MSG(false, "Unknown pipeline type");
  }

  // Update Pipeline Cache
  if (!cache_exists) {
    size_t cache_data_size = 0;
    VK_CHECK(vkGetPipelineCacheData(vk_device, pipeline_cache, &cache_data_size,
                                    nullptr));

    void *cache_data = stack_allocator->allocate(cache_data_size, 64);
    VK_CHECK(vkGetPipelineCacheData(vk_device, pipeline_cache, &cache_data_size,
                                    cache_data));

    file_service->write_file_binary(cache_path, cache_data, cache_data_size);

    stack_allocator->deallocate(cache_data);
  }

  vkDestroyPipelineCache(vk_device, pipeline_cache, vk_allocation_callbacks);

  for (u32 i = 0; i < creation.shader_count; ++i) {
    vkDestroyShaderModule(vk_device, vk_shader_modules[i],
                          vk_allocation_callbacks);
  }

  pipeline->name = creation.name;
  set_resource_name(VK_OBJECT_TYPE_PIPELINE, (u64)pipeline->vk_handle,
                    creation.name);

  file_service->change_directory(dir.path);
  stack_allocator->free_marker(stack_marker);
  return handle;
}

TextureHandle VulkanBackend::create_texture(TextureCreation &creation) {
  creation.alias_image = create_image(creation);
  creation.name = string_buffer.append_use_f("%s_View", creation.name);
  return create_image_view(creation);
}

TextureHandle VulkanBackend::create_image(TextureCreation &creation) {
  HASSERT(creation.mip_level_count != 0);
  TextureHandle handle = images.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a Vulkan image Resource!");
    return handle;
  }

  VulkanImage *image = images.obtain(handle);

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.extent.width = creation.width;
  image_info.extent.height = creation.height;
  image_info.extent.depth = creation.depth;
  image_info.mipLevels = creation.mip_level_count;
  image_info.arrayLayers = creation.array_layer_count;
  image_info.format = to_vk_format(creation.format);
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;

  image_info.usage |= to_vk_image_usage_flags(creation.usage);

  // TODO: Texture aliasing
  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VK_CHECK(vmaCreateImage(vma_allocator, &image_info, &memory_info,
                          &image->vk_handle, &image->vma_allocation, nullptr));

  set_resource_name(VK_OBJECT_TYPE_IMAGE, (u64)image->vk_handle, creation.name);

  image->views_count = 0;
  image->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image->format = to_vk_format(creation.format);
  image->vk_extents = {creation.width, creation.height, creation.depth};
  image->mip_count = creation.mip_level_count;

  if (creation.initial_data) {
    VulkanBuffer staging_buffer{};
    // TODO: Make more configurable for different image formats right now it
    // assumes a 4 component format
    VkDeviceSize buffer_size = creation.width * creation.height * 4;

    vk_create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging_buffer);

    void *data;
    vmaMapMemory(vma_allocator, staging_buffer.vma_allocation, &data);
    memcpy(data, creation.initial_data, (size_t)buffer_size);
    vmaUnmapMemory(vma_allocator, staging_buffer.vma_allocation);

    VulkanCommandBuffer *transfer_command_buffer =
        transfer_command_buffer_manager.get_command_buffer(
            0, Platform::get_current_processor_id(), true);
    transfer_command_buffer->copy_buffer_to_image(
        handle, staging_buffer.vk_handle, buffer_size, vk_transfer_queue);

    VkSemaphore transfer_finish_semaphore;
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(vk_device, &semaphore_info, nullptr,
                      &transfer_finish_semaphore);

    // Release Ownership to graphics queue
    transfer_command_buffer->transition_image(
        handle, image->current_layout, image->current_layout,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        queue_family_indices.transfer_family_index,
        queue_family_indices.graphics_family_index);

    transfer_command_buffer->end();

    {
      VkCommandBufferSubmitInfo command_submit_info{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
      command_submit_info.commandBuffer = transfer_command_buffer->vk_handle;

      VkSemaphoreSubmitInfo semaphore_submit_info{
          VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      semaphore_submit_info.semaphore = transfer_finish_semaphore;
      semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;

      VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
      submit_info.commandBufferInfoCount = 1;
      submit_info.pCommandBufferInfos = &command_submit_info;
      submit_info.signalSemaphoreInfoCount = 1;
      submit_info.pSignalSemaphoreInfos = &semaphore_submit_info;
      vkQueueSubmit2(vk_transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    u64 wait_value = frame_number < max_frames_in_flight ? 0 : frame_number;
    VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &vk_timeline_graphics_semaphore;
    wait_info.pValues = &wait_value;
    vkWaitSemaphores(vk_device, &wait_info, UINT64_MAX);

    VulkanCommandBuffer *graphics_command_buffer =
        command_buffer_manager.get_command_buffer(
            frame_number % max_frames_in_flight,
            Platform::get_current_processor_id(), true);

    // Generate mipmaps
    if (creation.mip_level_count > 1) {
      // Check if image format supports linear blitting
      VkFormatProperties format_properties;
      vkGetPhysicalDeviceFormatProperties(vk_physical_device,
                                          to_vk_format(creation.format),
                                          &format_properties);
      // TODO: Print out the vulkan format
      HASSERT_MSG((format_properties.optimalTilingFeatures &
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),
                  "Image format does not support linear filtering");
      // Acquire ownerhip to graphics queue
      graphics_command_buffer->transition_image(
          handle, image->current_layout, image->current_layout,
          VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
          queue_family_indices.transfer_family_index,
          queue_family_indices.graphics_family_index);

      VkImageMemoryBarrier2 image_barrier{
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
      image_barrier.image = image->vk_handle;
      image_barrier.subresourceRange.aspectMask =
          has_depth_or_stencil(image->format) ? VK_IMAGE_ASPECT_DEPTH_BIT
                                              : VK_IMAGE_ASPECT_COLOR_BIT;
      image_barrier.subresourceRange.levelCount = 1;
      image_barrier.subresourceRange.baseArrayLayer = 0;
      image_barrier.subresourceRange.layerCount = 1;
      // Ownership should have been transfered to the graphics queue by now
      image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      // Initlal stage src and dest mask
      image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
      image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;

      i32 mip_width = creation.width;
      i32 mip_height = creation.height;

      for (u32 i = 1; i < creation.mip_level_count; ++i) {

        // Transition src image miplevel to transfer src optmimal
        image_barrier.subresourceRange.baseMipLevel = i - 1;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

        graphics_command_buffer->pipeline_barrier(&image_barrier, 1);

        // TODO: Move to VulkanCommandBuffer or make a util function
        VkImageBlit2 blit_region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.mipLevel = i - 1;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = 1;
        blit_region.srcOffsets[0] = {0, 0, 0};
        blit_region.srcOffsets[1] = {mip_width, mip_height, 1};

        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.mipLevel = i;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = 1;
        blit_region.dstOffsets[0] = {0, 0, 0};
        blit_region.dstOffsets[1] = {mip_width > 1 ? mip_width / 2 : 1,
                                     mip_height > 1 ? mip_height / 2 : 1, 1};

        VkBlitImageInfo2 blit2{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
        blit2.srcImage = image->vk_handle;
        blit2.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit2.dstImage = image->vk_handle;
        blit2.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit2.regionCount = 1;
        blit2.pRegions = &blit_region;
        blit2.filter = VK_FILTER_LINEAR;

        vkCmdBlitImage2(graphics_command_buffer->vk_handle, &blit2);

        // Transition src image mip to shader read layout
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

        graphics_command_buffer->pipeline_barrier(&image_barrier, 1);

        if (mip_width > 1)
          mip_width /= 2;
        if (mip_height > 1)
          mip_height /= 2;

        // Rest of the mips use these mask
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      }

      // Transition the last mip level to shader read layout
      image_barrier.subresourceRange.baseMipLevel =
          creation.mip_level_count - 1;
      image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      image_barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
      image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
      image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

      graphics_command_buffer->pipeline_barrier(&image_barrier, 1);
    } else {
      // Acquire ownerhip to graphics queue
      graphics_command_buffer->transition_image(
          handle, image->current_layout,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
          queue_family_indices.transfer_family_index,
          queue_family_indices.graphics_family_index);
    }

    graphics_command_buffer->end();

    {
      VkCommandBufferSubmitInfo command_submit_info{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
      command_submit_info.commandBuffer = graphics_command_buffer->vk_handle;

      VkSemaphoreSubmitInfo semaphore_submit_info{
          VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      semaphore_submit_info.semaphore = transfer_finish_semaphore;
      semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;

      VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
      submit_info.commandBufferInfoCount = 1;
      submit_info.pCommandBufferInfos = &command_submit_info;
      submit_info.waitSemaphoreInfoCount = 1;
      submit_info.pWaitSemaphoreInfos = &semaphore_submit_info;
      vkQueueSubmit2(vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    }
    vkQueueWaitIdle(vk_transfer_queue);
    vkQueueWaitIdle(vk_graphics_queue);

    vkDestroySemaphore(vk_device, transfer_finish_semaphore,
                       vk_allocation_callbacks);

    transfer_command_buffer->reset();
    graphics_command_buffer->reset();
    vmaDestroyBuffer(vma_allocator, staging_buffer.vk_handle,
                     staging_buffer.vma_allocation);
  }

  return handle;
}

TextureHandle VulkanBackend::create_image_view(TextureCreation &creation) {
  TextureHandle handle = image_views.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a Vulkan image view Resource!");
    return handle;
  }

  VulkanImage *vk_image = images.obtain(creation.alias_image);
  VulkanImageView *view = image_views.obtain(handle);
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = vk_image->vk_handle;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = to_vk_format(creation.format);
  view_info.subresourceRange.aspectMask = has_depth_or_stencil(creation.format)
                                              ? VK_IMAGE_ASPECT_DEPTH_BIT
                                              : VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.baseMipLevel = creation.mip_base_level;
  view_info.subresourceRange.levelCount = creation.mip_level_count;
  view_info.subresourceRange.baseArrayLayer = creation.array_base_level;
  view_info.subresourceRange.layerCount = creation.array_layer_count;

  VK_CHECK(vkCreateImageView(vk_device, &view_info, vk_allocation_callbacks,
                             &view->vk_handle));

  set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (u64)view->vk_handle,
                    creation.name);
  view->image = creation.alias_image;
  ++vk_image->views_count;

  bindless_textures_to_update.push(handle);

  return handle;
}

void VulkanBackend::destroy_buffer(BufferHandle handle) {
  if (handle.index == k_invalid_index) {
    HWARN("Attempting to free an invalid VulkanBuffer");
    return;
  }
  VulkanBuffer *buffer = access_buffer(handle);
  ResourceQueueObject q_object{VK_OBJECT_TYPE_BUFFER, handle, buffer->name};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_pipeline(PipelineHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanPipeline");
    return;
  }

  VulkanPipeline *pipeline = access_pipeline(handle);

  for (u32 i = 0; i < pipeline->set_layout_count; ++i) {
    destroy_descriptor_set_layout(pipeline->set_layouts[i]);
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_PIPELINE, handle, pipeline->name};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_texture(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid Texture");
    return;
  }

  destroy_image_view(handle);
}

void VulkanBackend::destroy_image(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanImage");
    return;
  }
  VulkanImage *image = images.obtain(handle);

  if (image->views_count != 0) {
    HWARN(
        "Attempting to free a VulkanImage that is still used by a VulkanView");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_IMAGE, handle};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_image_view(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HWARN("Attempting to free an invalid VulkanImageView");
    return;
  }
  VulkanImageView *view = image_views.obtain(handle);
  VulkanImage *image = images.obtain(view->image);
  --image->views_count;

  destroy_image(view->image);
  ResourceQueueObject q_object{VK_OBJECT_TYPE_IMAGE_VIEW, handle};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_descriptor_set_layout(
    DescriptorSetLayoutHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanDescriptorSetLayout");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, handle};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_sampler(SamplerHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanSampler");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_SAMPLER, handle};
  resource_deletion_queue.push(q_object);
}

void VulkanBackend::destroy_buffer_instant(BufferHandle handle) {
  if (handle.index == k_invalid_index) {
    HWARN("Attempting to free an invalid VulkanBuffer");
    return;
  }
  VulkanBuffer *buffer = access_buffer(handle);
  VmaAllocationInfo alloc_info{};
  alloc_info.pMappedData = nullptr;
  vmaGetAllocationInfo(vma_allocator, buffer->vma_allocation, &alloc_info);

  if (alloc_info.pMappedData)
    vmaUnmapMemory(vma_allocator, buffer->vma_allocation);

  vmaDestroyBuffer(vma_allocator, buffer->vk_handle, buffer->vma_allocation);
  buffer->name = nullptr;
  buffer->vk_handle = VK_NULL_HANDLE;
  buffer->vma_allocation = VK_NULL_HANDLE;
  buffer->mapped_data = nullptr;

  buffers.release(handle);
}

void VulkanBackend::destroy_pipeline_instant(PipelineHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanPipeline");
    return;
  }

  VulkanPipeline *pipeline = access_pipeline(handle);

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  hfree(pipeline->set_layouts, allocator);

  vkDestroyPipelineLayout(vk_device, pipeline->vk_layout,
                          vk_allocation_callbacks);

  vkDestroyPipeline(vk_device, pipeline->vk_handle, vk_allocation_callbacks);

  pipelines.release(handle);
}

void VulkanBackend::destroy_descriptor_set_layout_instant(
    DescriptorSetLayoutHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanDescriptorSetLayout");
    return;
  }
  VulkanDescriptorSetLayout *layout = access_descriptor_set_layout(handle);
  for (u32 i = 0; i < layout->allocated_sets.size; ++i) {
    descriptor_sets.release(layout->allocated_sets[i]);
  }
  layout->allocated_sets.shutdown();
  layout->vk_bindings.shutdown();
  vkDestroyDescriptorSetLayout(vk_device, layout->vk_handle,
                               vk_allocation_callbacks);

  descriptor_set_layouts.release(handle);
}

void VulkanBackend::destroy_image_instant(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanImage");
    return;
  }
  VulkanImage *image = images.obtain(handle);
  vmaDestroyImage(vma_allocator, image->vk_handle, image->vma_allocation);
  images.release(handle);
}

void VulkanBackend::destroy_image_view_instant(TextureHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanImageView");
    return;
  }
  VulkanImageView *image_view = image_views.obtain(handle);
  vkDestroyImageView(vk_device, image_view->vk_handle, vk_allocation_callbacks);
  image_views.release(handle);
}

void VulkanBackend::destroy_sampler_instant(SamplerHandle handle) {
  if (handle.index == k_invalid_index) {
    HERROR("Attempting to free an invalid VulkanSampler");
    return;
  }
  VulkanSampler *sampler = samplers.obtain(handle);
  vkDestroySampler(vk_device, sampler->vk_handle, vk_allocation_callbacks);
  samplers.release(handle);
}

void VulkanBackend::free_queued_resources() {
  if (resource_deletion_queue.size > 0) {
    vkDeviceWaitIdle(vk_device);
    for (i32 i = resource_deletion_queue.size - 1; i >= 0; --i) {
      ResourceQueueObject &queue_object = resource_deletion_queue[i];
      switch (queue_object.type) {
      case VK_OBJECT_TYPE_BUFFER:
        destroy_buffer_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        destroy_descriptor_set_layout_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_PIPELINE:
        destroy_pipeline_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_IMAGE:
        destroy_image_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_IMAGE_VIEW:
        destroy_image_view_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_SAMPLER:
        destroy_sampler_instant(queue_object.handle);
        break;
      default:
        HERROR("Trying to delete an unknown type");
        break;
      }
      resource_deletion_queue.pop();
    }
  }
}

bool VulkanBackend::update_shader_uniform_set(ShaderUniformSet &set,
                                              PipelineHandle pipeline_handle) {
  DescriptorSetHandle handle = descriptor_sets.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a VulkanDescriptorSet resource!");
    return false;
  }
  VulkanDescriptorSet *d_set = access_descriptor_set(handle);

  VulkanPipeline *pipeline = access_pipeline(pipeline_handle);
  if (pipeline_handle.index == k_invalid_index) {
    HERROR("Invalid VulkanPipeline resource!");
    return false;
  }

  VulkanDescriptorSetLayout *layout = access_descriptor_set_layout(
      pipeline->set_layouts[set.set_index -
                            1]); // TODO: Subtracting 1 here because set == 0 is
                                 // reserved for bindless

  VkDescriptorSetLayout vk_layout = layout->vk_handle;
  VkDescriptorSetAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = vk_descriptor_pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &vk_layout;

  VK_CHECK(vkAllocateDescriptorSets(vk_device, &alloc_info, &d_set->vk_handle));

  // TODO: Assuming a max number of bindings for a descriptor set
  VkWriteDescriptorSet descriptor_writes[10];
  VkDescriptorBufferInfo buffer_infos[10];
  VkDescriptorImageInfo image_infos[10];
  HASSERT(set.uniform_count <= 10);
  for (u32 i = 0; i < set.uniform_count; i++) {
    if (set.uniforms[i].resource_type == ResourceType::Buffer) {
      VkDescriptorBufferInfo &buffer_info = buffer_infos[i];
      VulkanBuffer *buffer =
          access_buffer(set.uniforms[i].internal_resource_handle);

      buffer_info.buffer = buffer->vk_handle;
      buffer_info.offset = set.uniforms[i].buffer_info.offset;
      buffer_info.range = set.uniforms[i].buffer_info.range;

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
    } else if (set.uniforms[i].resource_type == ResourceType::Texture) {
      VkDescriptorImageInfo &image_info = image_infos[i];
      VulkanImageView *image_view =
          access_image_view(set.uniforms[i].internal_resource_handle);

      image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_info.imageView = image_view->vk_handle;
      VulkanSampler *sampler = access_sampler(default_sampler);
      image_info.sampler = sampler->vk_handle;

      VkWriteDescriptorSet &descriptor_write = descriptor_writes[i];
      descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptor_write.pNext = nullptr;
      descriptor_write.pImageInfo = &image_info;
      descriptor_write.pTexelBufferView = nullptr;
      descriptor_write.dstSet = d_set->vk_handle;
      descriptor_write.dstBinding = set.uniforms[i].binding;
      descriptor_write.dstArrayElement = 0;
      descriptor_write.descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptor_write.descriptorCount = 1;
      descriptor_write.pBufferInfo = nullptr;

    } else {
      HERROR("Unkown descriptor type");
      return false;
    }
  }
  vkUpdateDescriptorSets(vk_device, set.uniform_count, descriptor_writes, 0,
                         nullptr);

  layout->allocated_sets.push(handle);

  return true;
}

void VulkanBackend::create_descriptor_pool(u32 max_frames_in_flight) {
  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_size.descriptorCount = (max_frames_in_flight);

  VkDescriptorPoolSize pool_sizes[] = {pool_size};

  VkDescriptorPoolCreateInfo pool_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.poolSizeCount = ArraySize(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  pool_info.maxSets = max_frames_in_flight;

  VK_CHECK(vkCreateDescriptorPool(
      vk_device, &pool_info, vk_allocation_callbacks, &vk_descriptor_pool));

  // Bindless
  VkDescriptorPoolSize bindless_poolsize{};
  bindless_poolsize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindless_poolsize.descriptorCount = MAX_TEXTURES;

  VkDescriptorPoolCreateInfo bindless_pool_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  bindless_pool_info.poolSizeCount = 1;
  bindless_pool_info.pPoolSizes = &bindless_poolsize;
  bindless_pool_info.maxSets = MAX_TEXTURES;
  bindless_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;

  VK_CHECK(vkCreateDescriptorPool(vk_device, &bindless_pool_info,
                                  vk_allocation_callbacks,
                                  &vk_bindless_descriptor_pool));

  VkDescriptorSetLayoutBinding combined_sampler_binding;
  combined_sampler_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  combined_sampler_binding.descriptorCount = MAX_TEXTURES;
  combined_sampler_binding.binding = 0;
  combined_sampler_binding.stageFlags = VK_SHADER_STAGE_ALL;
  combined_sampler_binding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 1;
  layout_info.pBindings = &combined_sampler_binding;
  layout_info.flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

  VkDescriptorBindingFlags bindless_flags =
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo extended_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      nullptr};
  extended_info.bindingCount = 1;
  extended_info.pBindingFlags = &bindless_flags;

  layout_info.pNext = &extended_info;

  VK_CHECK(vkCreateDescriptorSetLayout(vk_device, &layout_info,
                                       vk_allocation_callbacks,
                                       &vk_bindless_descriptor_layout));

  // Allocate the descriptor set //////////////////////////
  VkDescriptorSetAllocateInfo alloc_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc_info.descriptorPool = vk_bindless_descriptor_pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &vk_bindless_descriptor_layout;

  VK_CHECK(vkAllocateDescriptorSets(vk_device, &alloc_info,
                                    &vk_bindless_descriptor_set));
}

void VulkanBackend::render_frame(RenderPacket *packet) {

  VulkanCommandBuffer *command_buffer =
      command_buffer_manager.get_command_buffer(packet->current_frame, 0,
                                                false);

  record_command_buffer(command_buffer, packet, packet->current_frame);
}

void VulkanBackend::update_uniform_buffer(RenderPacket *packet) {

  UniformBufferObject ubo{};
  ubo.model = glm::scale(glm::mat4(1.f), glm::vec3(0.01f));
  ubo.view = packet->camera->get_view();
  ubo.proj = packet->camera->get_projection();

  VulkanBuffer *uniform_buffer = access_buffer(packet->scene_data_buffer);

  memcpy(uniform_buffer->mapped_data, &ubo, sizeof(ubo));
}

void VulkanBackend::print_gpu_stats() {
  VmaBudget budgets[VK_MAX_MEMORY_HEAPS];

  // Retrieve the budget information
  VkPhysicalDeviceMemoryProperties mem_props;
  vkGetPhysicalDeviceMemoryProperties(vk_physical_device, &mem_props);
  vmaGetHeapBudgets(vma_allocator, budgets);

  // Iterate over each memory heap to access usage and budget information
  for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
    HTRACE("Heap {}: Usage = {:.2f} MB, Budget = {:.2f} MB", i,
           budgets[i].usage / (1024.0 * 1024.0),
           budgets[i].budget / (1024.0 * 1024.0));
  }
  // char *statsString = nullptr;
  // vmaBuildStatsString(vma_allocator, &statsString,
  //                     VK_FALSE); // VK_TRUE = detailed
  // printf("%s\n", statsString);

  // vmaFreeStatsString(vma_allocator, statsString);
}

void VulkanBackend::upload_buffer_data(void *data, VkBuffer dst_buffer,
                                       u32 buffer_size) {
  VulkanBuffer staging_buffer{};
  vk_create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging_buffer);

  vmaMapMemory(vma_allocator, staging_buffer.vma_allocation,
               &staging_buffer.mapped_data);
  memcpy(staging_buffer.mapped_data, data, (size_t)buffer_size);
  vmaUnmapMemory(vma_allocator, staging_buffer.vma_allocation);

  VulkanCommandBuffer *command_buffer =
      transfer_command_buffer_manager.get_command_buffer(0, 0, false);
  command_buffer->copy_buffer_to_buffer(dst_buffer, staging_buffer.vk_handle,
                                        buffer_size, vk_transfer_queue);

  vmaDestroyBuffer(vma_allocator, staging_buffer.vk_handle,
                   staging_buffer.vma_allocation);
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

static bool
select_physical_device(VkInstance instance, VkPhysicalDevice &_physical_device,
                       VkPhysicalDeviceProperties *device_properties,
                       QueueFamilyIndices &queue_family_indices,
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
    vkGetPhysicalDeviceProperties(device, device_properties);

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
          queue_family_indices.graphics_family_index == UINT32_MAX) {
        queue_family_indices.graphics_family_index = idx;
        continue;
      }

      if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
        queue_family_indices.transfer_family_index = idx;
        continue;
      }
    }
    if (queue_family_indices.is_complete()) {
      found_suitable_device = true;
      _physical_device = device;
      HTRACE("Suitable device found: {}", device_properties->deviceName);
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
        glm::clamp(extents.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    swapchain.vk_extents.height =
        glm::clamp(extents.height, capabilities.minImageExtent.height,
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
