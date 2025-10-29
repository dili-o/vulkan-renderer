#include "VkGpuDevice.hpp"
#include "Containers/ResourcePool.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
#include "Core/String.hpp"
#include "Platform/File.hpp"
#include "Platform/Platform.hpp"
#include "Platform/Process.hpp"
#include "Renderer/GPUResourceTypes.hpp"
#include "Renderer/GPUResources.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/Vulkan/SpirvParser.hpp"
#include "Renderer/Vulkan/VkContext.hpp"
#include "Renderer/Vulkan/VulkanUtils.hpp"

#ifdef _DEBUG
#define VULKAN_DEBUG_REPORT
#define VULKAN_EXTRA_VALIDATION
#endif // _DEBUG

// TODO: Make configurable
static u32 max_frames_in_flight = 2;

// TODO: Create Surface Function
namespace Helix {
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

  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;

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
          queue_family_indices.graphics_family_index == UINT32_MAX &&
          queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        queue_family_indices.graphics_family_index = idx;
        continue;
      }

      if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT &&
          queue_family_indices.compute_family_index == UINT32_MAX) {
        queue_family_indices.compute_family_index = idx;
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
  return found_suitable_device;
}

static void query_swapchain_support(VkPhysicalDevice physical_device,
                                    VkSurfaceKHR surface,
                                    VulkanSwapchain &swapchain,
                                    VkExtent2D &swapchain_extents) {
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;

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

  // TODO: Make configurable
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
    // TODO: Make configurable
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
    swapchain_extents = capabilities.currentExtent;
  } else {
    Platform *platform = Platform::instance();
    VkExtent2D extents = {(u32)platform->width, (u32)platform->height};

    swapchain_extents.width =
        glm::clamp(extents.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    swapchain_extents.height =
        glm::clamp(extents.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }

  // TODO: Make configurable
  u32 image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  swapchain.image_count = image_count;
}

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

GpuDevice *create_vulkan_device() {
  void *memory = halloca(sizeof(VkGpuDevice),
                         &MemoryService::instance()->system_allocator);
  VkGpuDevice *device = new (memory) VkGpuDevice();

  VkResult res = volkInitialize();
  if (res != VK_SUCCESS) {
    HERROR("Volk failed to initialize");
    free(device);
    return nullptr;
  }

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;

  VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.apiVersion = VK_API_VERSION_1_3;
  app_info.pApplicationName = "Helix"; // TODO: App Name
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "Helix Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create_info.pApplicationInfo = &app_info;

  Array<cstring> instance_extensions{};
  instance_extensions.init(stack_allocator, 1);

  u32 platform_extension_count = 0;
  const char *const *platform_extensions =
      Platform::instance()->get_vulkan_extension_names(
          &platform_extension_count);

  for (u32 i = 0; i < platform_extension_count; ++i) {
    instance_extensions.push(platform_extensions[i]);
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
      instance_extensions.push(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      break;
    }
  }
#endif
  create_info.enabledExtensionCount = instance_extensions.size;
  create_info.ppEnabledExtensionNames = instance_extensions.data;

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
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
      VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT};
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

  VK_CHECK(vkCreateInstance(&create_info, device->vk_allocation_callbacks,
                            &device->vk_instance));

  volkLoadInstance(device->vk_instance);

  validation_layer_names.shutdown();
  instance_extensions.shutdown();

#ifdef VULKAN_DEBUG_REPORT
#pragma region Vulkan_Debugger
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      device->vk_instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    if (func(device->vk_instance, &debug_create_info,
             device->vk_allocation_callbacks,
             &device->vk_debug_utils_messenger) != VK_SUCCESS) {
      HERROR("Failed to set up debug messenger!");
    }
  } else {
    HERROR("Failed to set up debug messenger!");
  }
#endif

  // Surface
  if (!Platform::instance()->create_vulkan_surface(device)) {
    HERROR("Failed to create surface!");
    return nullptr;
  }

  Array<cstring> device_extensions{};
  device_extensions.init(stack_allocator, 2);
  device_extensions.push(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  // Create Physical Device
  device->vk_physical_device_properties2.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  device->vk_physical_device_vulkan11_properties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;

  device->vk_physical_device_properties2.pNext =
      &device->vk_physical_device_vulkan11_properties;
  if (!select_physical_device(
          device->vk_instance, device->vk_physical_device,
          &device->vk_physical_device_properties2.properties,
          device->queue_family_indices, device->vk_surface)) {
    HERROR("Failed to create physical device!");
    free(device);
    return nullptr;
  }
  vkGetPhysicalDeviceProperties2(device->vk_physical_device,
                                 &device->vk_physical_device_properties2);

  // Create Logical Device
  Array<VkDeviceQueueCreateInfo> queue_create_infos{};
  queue_create_infos.init(stack_allocator, 1);
  f32 queue_priority = 1.f;

  VkDeviceQueueCreateInfo main_queue_info{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  main_queue_info.queueFamilyIndex =
      device->queue_family_indices.graphics_family_index;
  main_queue_info.queueCount = 1;
  main_queue_info.pQueuePriorities = &queue_priority;
  queue_create_infos.push(main_queue_info);

  if (device->queue_family_indices.graphics_family_index !=
      device->queue_family_indices.compute_family_index) {
    VkDeviceQueueCreateInfo queue_create_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex =
        device->queue_family_indices.compute_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push(queue_create_info);
  }

  if (device->queue_family_indices.graphics_family_index !=
      device->queue_family_indices.transfer_family_index) {
    VkDeviceQueueCreateInfo queue_create_info{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create_info.queueFamilyIndex =
        device->queue_family_indices.transfer_family_index;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push(queue_create_info);
  }

  // Check if all features are supported on the GPU
  VkPhysicalDeviceFeatures2 supported_features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

  VkPhysicalDeviceVulkan11Features supported_features11{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  VkPhysicalDeviceVulkan12Features supported_features12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features supported_features13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  supported_features.pNext = &supported_features11;
  supported_features11.pNext = &supported_features12;
  supported_features12.pNext = &supported_features13;

  vkGetPhysicalDeviceFeatures2(device->vk_physical_device, &supported_features);
  // VkPhysicalDeviceFeatures2
  HASSERT(supported_features.features.samplerAnisotropy == VK_TRUE);
  HASSERT(supported_features.features.fillModeNonSolid == VK_TRUE);
  HASSERT(supported_features.features.geometryShader == VK_TRUE);
  HASSERT(supported_features.features.drawIndirectFirstInstance == VK_TRUE);
#ifdef VULKAN_EXTRA_VALIDATION
  HASSERT(supported_features.features.fragmentStoresAndAtomics == VK_TRUE);
  HASSERT(supported_features.features.vertexPipelineStoresAndAtomics ==
          VK_TRUE);
  HASSERT(supported_features.features.shaderInt64 == VK_TRUE);
#endif
  // VkPhysicalDeviceVulkan11Features
  HASSERT(supported_features11.shaderDrawParameters == VK_TRUE);
  // VkPhysicalDeviceVulkan12Features
  HASSERT(supported_features12.bufferDeviceAddress == VK_TRUE);
  HASSERT(supported_features12.shaderSampledImageArrayNonUniformIndexing ==
          VK_TRUE);
  HASSERT(supported_features12.runtimeDescriptorArray == VK_TRUE);
  HASSERT(supported_features12.descriptorBindingSampledImageUpdateAfterBind ==
          VK_TRUE);
  HASSERT(supported_features12.descriptorBindingPartiallyBound == VK_TRUE);
  HASSERT(supported_features12.timelineSemaphore == VK_TRUE);
  HASSERT(supported_features12.drawIndirectCount == VK_TRUE);
#ifdef VULKAN_EXTRA_VALIDATION
  HASSERT(supported_features12.vulkanMemoryModel == VK_TRUE);
  HASSERT(supported_features12.vulkanMemoryModelDeviceScope == VK_TRUE);
  HASSERT(supported_features12.storageBuffer8BitAccess == VK_TRUE);
#endif
  // VkPhysicalDeviceVulkan13Features
  HASSERT(supported_features13.dynamicRendering == VK_TRUE);
  HASSERT(supported_features13.synchronization2 == VK_TRUE);

  VkPhysicalDeviceFeatures device_features{};
  device_features.samplerAnisotropy = VK_TRUE;
  device_features.fillModeNonSolid = VK_TRUE;
  device_features.geometryShader = VK_TRUE;
  // TODO: Temporary Fix for the error I get on VULKAN_EXTRA_VALIDATION, might
  // be a bug with validation layers
  device_features.drawIndirectFirstInstance = VK_TRUE;
#ifdef VULKAN_EXTRA_VALIDATION
  device_features.fragmentStoresAndAtomics = VK_TRUE;
  device_features.vertexPipelineStoresAndAtomics = VK_TRUE;
  device_features.shaderInt64 = VK_TRUE;
#endif
  VkDeviceCreateInfo device_create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_create_info.pQueueCreateInfos = queue_create_infos.data;
  device_create_info.queueCreateInfoCount = queue_create_infos.size;
  device_create_info.pEnabledFeatures = &device_features;
  device_create_info.enabledExtensionCount = device_extensions.size;
  device_create_info.ppEnabledExtensionNames = device_extensions.data;

  VkPhysicalDeviceVulkan11Features features11 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  features11.shaderDrawParameters = VK_TRUE;
  // Enable Bindless descriptors, Buffer Device Address, Timeline Semaphore
  VkPhysicalDeviceVulkan12Features features12 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  features12.bufferDeviceAddress = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  features12.runtimeDescriptorArray = VK_TRUE;
  features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  features12.descriptorBindingPartiallyBound = VK_TRUE;
  features12.timelineSemaphore = VK_TRUE;
  features12.drawIndirectCount = VK_TRUE;
#ifdef VULKAN_EXTRA_VALIDATION
  features12.vulkanMemoryModel = VK_TRUE;
  features12.vulkanMemoryModelDeviceScope = VK_TRUE;
  features12.storageBuffer8BitAccess = VK_TRUE;
#endif
  // features12.descriptorBindingVariableDescriptorCount   = VK_TRUE;
  features12.pNext = &features11;

  // Enable Dynamic Rendering and Synchronization 2
  VkPhysicalDeviceVulkan13Features features13 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features13.pNext = &features12;

  device_create_info.pNext = &features13;

  VK_CHECK(vkCreateDevice(device->vk_physical_device, &device_create_info,
                          device->vk_allocation_callbacks, &device->vk_device));
  volkLoadDevice(device->vk_device);
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
  allocator_create_info.physicalDevice = device->vk_physical_device;
  allocator_create_info.device = device->vk_device;
  allocator_create_info.instance = device->vk_instance;
  allocator_create_info.pVulkanFunctions = &vma_vulkan_functions;
  allocator_create_info.flags =
      VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT |
      VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  VK_CHECK(vmaCreateAllocator(&allocator_create_info, &device->vma_allocator));

  //  Get the function pointers to Debug Utils functions.
  if (debug_utils_extension_present) {
    device->pfnSetDebugUtilsObjectNameEXT =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
            device->vk_device, "vkSetDebugUtilsObjectNameEXT");
    device->pfnCmdBeginDebugUtilsLabelEXT =
        (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            device->vk_device, "vkCmdBeginDebugUtilsLabelEXT");
    device->pfnCmdInsertDebugUtilsLabelEXT =
        (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            device->vk_device, "vkCmdInsertDebugUtilsLabelEXT");
    device->pfnCmdEndDebugUtilsLabelEXT =
        (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(
            device->vk_device, "vkCmdEndDebugUtilsLabelEXT");

    HASSERT(device->pfnSetDebugUtilsObjectNameEXT);
    HASSERT(device->pfnCmdBeginDebugUtilsLabelEXT);
    HASSERT(device->pfnCmdInsertDebugUtilsLabelEXT);
    HASSERT(device->pfnCmdEndDebugUtilsLabelEXT);
  }

  vkGetDeviceQueue(device->vk_device,
                   device->queue_family_indices.graphics_family_index, 0,
                   &device->vk_graphics_queue);
  device->set_resource_name(VK_OBJECT_TYPE_QUEUE,
                            (u64)device->vk_graphics_queue, "Graphics_Queue");
  if (device->queue_family_indices.graphics_family_index !=
      device->queue_family_indices.compute_family_index) {
    vkGetDeviceQueue(device->vk_device,
                     device->queue_family_indices.compute_family_index, 0,
                     &device->vk_compute_queue);
    device->set_resource_name(VK_OBJECT_TYPE_QUEUE,
                              (u64)device->vk_compute_queue, "Compute_queue");
  }
  device->vk_transfer_queue = device->vk_graphics_queue;
  if (device->queue_family_indices.graphics_family_index !=
      device->queue_family_indices.transfer_family_index) {
    vkGetDeviceQueue(device->vk_device,
                     device->queue_family_indices.transfer_family_index, 0,
                     &device->vk_transfer_queue);
    device->set_resource_name(VK_OBJECT_TYPE_QUEUE,
                              (u64)device->vk_transfer_queue, "Transfer_queue");
  }
  queue_create_infos.shutdown();

  device->string_buffer.init(allocator, hkilo(15));
  // Init Resource Pools
  // TODO: Make configurable
  device->buffers.init(allocator, 50);
  device->images.init(allocator, 1000);
  device->image_views.init(allocator, 1000);
  device->samplers.init(allocator, 10);
  device->descriptor_sets.init(allocator, 15);
  device->descriptor_set_layouts.init(allocator, 15);
  device->pipelines.init(allocator, 10);
  device->render_passes.init(allocator, 10);

  device->resource_deletion_queue.init(allocator, 10);

  // TODO: Make this configurable
  device->vk_image_available_semaphores.init(allocator, 2, 2);

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (u32 i = 0; i < device->vk_image_available_semaphores.size; ++i) {

    VK_CHECK(vkCreateSemaphore(device->vk_device, &semaphore_info,
                               device->vk_allocation_callbacks,
                               &device->vk_image_available_semaphores[i]));
    device->set_resource_name(
        VK_OBJECT_TYPE_SEMAPHORE, (u64)device->vk_image_available_semaphores[i],
        device->string_buffer.append_use_f("image_available_semaphore_%d", i));
  }

  VkSemaphoreTypeCreateInfo timeline_create_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timeline_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timeline_create_info.initialValue = 0;
  semaphore_info.pNext = &timeline_create_info;

  VK_CHECK(vkCreateSemaphore(device->vk_device, &semaphore_info,
                             device->vk_allocation_callbacks,
                             &device->vk_timeline_semaphore));
  device->set_resource_name(VK_OBJECT_TYPE_SEMAPHORE,
                            (u64)device->vk_timeline_semaphore,
                            "TimelineSemaphore");

  VkDescriptorPoolSize bindless_poolsize{};
  bindless_poolsize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindless_poolsize.descriptorCount = MAX_TEXTURES;

  VkDescriptorPoolCreateInfo bindless_pool_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  bindless_pool_info.poolSizeCount = 1;
  bindless_pool_info.pPoolSizes = &bindless_poolsize;
  bindless_pool_info.maxSets = 1;
  bindless_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

  VK_CHECK(vkCreateDescriptorPool(device->vk_device, &bindless_pool_info,
                                  device->vk_allocation_callbacks,
                                  &device->vk_bindless_pool));

  VkDescriptorPoolSize poolsize{};
  poolsize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolsize.descriptorCount = 3;
  VkDescriptorPoolCreateInfo pool_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &poolsize;
  pool_info.maxSets = 3;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  VK_CHECK(vkCreateDescriptorPool(device->vk_device, &bindless_pool_info,
                                  device->vk_allocation_callbacks,
                                  &device->vk_descriptor_pool));

  device->frame_number = 0;

  HINFO("Vulkan Backend Initialized");

  return device;
}

void destroy_vulkan_device(VkGpuDevice *device) {
  vkDeviceWaitIdle(device->vk_device);

  vkDestroyDescriptorPool(device->vk_device, device->vk_bindless_pool,
                          device->vk_allocation_callbacks);
  vkDestroyDescriptorPool(device->vk_device, device->vk_descriptor_pool,
                          device->vk_allocation_callbacks);

  device->destroy_swapchain();
  for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
    device->images.release(device->swapchain.images[i]);
    device->image_views.release(device->swapchain.image_views[i]);
  }

  for (VkSemaphore semaphore : device->vk_image_available_semaphores) {
    vkDestroySemaphore(device->vk_device, semaphore,
                       device->vk_allocation_callbacks);
  }

  for (VkSemaphore semaphore : device->vk_render_finished_semaphores) {
    vkDestroySemaphore(device->vk_device, semaphore,
                       device->vk_allocation_callbacks);
  }

  vkDestroySemaphore(device->vk_device, device->vk_timeline_semaphore,
                     device->vk_allocation_callbacks);

  device->free_queued_resources();

  device->vk_image_available_semaphores.shutdown();
  device->vk_render_finished_semaphores.shutdown();

  device->buffers.shutdown();
  device->images.shutdown();
  device->image_views.shutdown();
  device->samplers.shutdown();
  device->descriptor_sets.shutdown();
  device->descriptor_set_layouts.shutdown();
  device->pipelines.shutdown();
  device->render_passes.shutdown();

  device->resource_deletion_queue.shutdown();
  device->string_buffer.shutdown();

  vmaDestroyAllocator(device->vma_allocator);

  vkDestroyDevice(device->vk_device, device->vk_allocation_callbacks);
  vkDestroySurfaceKHR(device->vk_instance, device->vk_surface,
                      device->vk_allocation_callbacks);
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      device->vk_instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(device->vk_instance, device->vk_debug_utils_messenger,
         device->vk_allocation_callbacks);
  }
  vkDestroyInstance(device->vk_instance, device->vk_allocation_callbacks);

  HINFO("Vulkan Backend shutdown");
}

u32 VkGpuDevice::create_backbuffers(u32 width, u32 height, u32 count) {
  for (u32 i = 0; i < count; ++i) {
    swapchain.images[i] = images.obtain_new();
    swapchain.image_views[i] = image_views.obtain_new();
  }

  create_swapchain();

  vk_render_finished_semaphores.init(
      &MemoryService::instance()->system_allocator, count, count);
  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (u32 i = 0; i < vk_render_finished_semaphores.size; ++i) {

    VK_CHECK(vkCreateSemaphore(vk_device, &semaphore_info,
                               vk_allocation_callbacks,
                               &vk_render_finished_semaphores[i]));
    // TODO:
    set_resource_name(
        VK_OBJECT_TYPE_SEMAPHORE, (u64)vk_render_finished_semaphores[i],
        string_buffer.append_use_f("RenderFinished_Semaphore%d", i));
  }
  return count;
}

void VkGpuDevice::process_display_changes() {}

BufferHandle VkGpuDevice::create_buffer(const BufferCreation &creation) {
  BufferHandle handle = buffers.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Vulkan Buffer Resource!");
    return handle;
  }

  if (creation.usage_flags == BufferUsage::None) {
    HERROR("Creating a buffer with no usage flags");
  }

  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size =
      (creation.size == MAX_ALLOCATION_SIZE)
          ? vk_physical_device_vulkan11_properties.maxMemoryAllocationSize / 2
          : creation.size;
  buffer_info.usage = to_vk_buffer_usage_flags(creation.usage_flags);
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = creation.memory_access_flags & MemoryAccess::GPU_ONLY
                          ? VMA_MEMORY_USAGE_GPU_ONLY
                          : VMA_MEMORY_USAGE_AUTO;
  memory_info.requiredFlags =
      to_vk_mem_property_flags(creation.memory_access_flags);
  memory_info.flags =
      creation.mapped
          ? VMA_ALLOCATION_CREATE_MAPPED_BIT |
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
          : 0;

  VulkanBuffer *buffer = access_buffer(handle);
  VmaAllocationInfo alloc_info{};

  VK_CHECK(vmaCreateBuffer(vma_allocator, &buffer_info, &memory_info,
                           &buffer->vk_handle, &buffer->vma_allocation,
                           &alloc_info));
  buffer->name = creation.name;
  buffer->memory_access = creation.memory_access_flags;
  buffer->usage = creation.usage_flags;
  buffer->mapped_data = nullptr;

  if (creation.mapped) {
    vmaMapMemory(vma_allocator, buffer->vma_allocation, &buffer->mapped_data);
  }

  if (creation.usage_flags & BufferUsage::ShaderAddress) {
    VkBufferDeviceAddressInfo address_info{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address_info.buffer = buffer->vk_handle;
    buffer->device_address = vkGetBufferDeviceAddress(vk_device, &address_info);
  }

  set_resource_name(VK_OBJECT_TYPE_BUFFER, (u64)buffer->vk_handle,
                    creation.name);
  vmaSetAllocationName(vma_allocator, buffer->vma_allocation, creation.name);

  return handle;
}

TextureHandle VkGpuDevice::create_texture(const TextureCreation &creation) {
  return create_image_view(creation, create_image(creation));
}

VkImageHandle VkGpuDevice::create_image(const TextureCreation &creation) {
  HASSERT(creation.mip_level_count != 0);
  TextureHandle handle = images.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Vulkan image Resource!");
    return handle;
  }
  VulkanImage *image = images.obtain(handle);

  VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
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
  vmaSetAllocationName(vma_allocator, image->vma_allocation, creation.name);

  image->vk_usage = image_info.usage;
  image->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image->vk_format = to_vk_format(creation.format);
  image->vk_extents = {creation.width, creation.height, creation.depth};
  image->mip_count = creation.mip_level_count;
  image->name = creation.name;

  return handle;
}

VkImageViewHandle
VkGpuDevice::create_image_view(const TextureCreation &creation,
                               VkImageHandle image) {
  TextureHandle handle = image_views.obtain_new();
  if (handle.index == k_invalid_index) {
    HERROR("Failed to obtain a Vulkan image view Resource!");
    return handle;
  }

  VulkanImage *vk_image = images.obtain(image);
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
  cstring name = string_buffer.append_use_f("%s_View", creation.name);
  set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (u64)view->vk_handle, name);
  view->image = image;
  view->name = name;
  view->base_array_level = creation.array_base_level;
  view->base_mip_level = creation.mip_base_level;

  if (creation.usage & TextureUsage::Sampled) {
    view->sampler = creation.sampler;
  }

  return handle;
}

SamplerHandle VkGpuDevice::create_sampler(const SamplerCreation &creation) {
  SamplerHandle handle = samplers.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Vulkan Sampler!");
    return handle;
  }

  VulkanSampler *sampler = access_sampler(handle);

  VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.minFilter = to_vk_filter(creation.min_filter);
  sampler_info.magFilter = to_vk_filter(creation.mag_filter);
  sampler_info.minLod = 0.f;
  sampler_info.maxLod = VK_LOD_CLAMP_NONE;
  sampler_info.addressModeU =
      to_vk_sampler_address_mode(creation.address_mode_u);
  sampler_info.addressModeV =
      to_vk_sampler_address_mode(creation.address_mode_v);
  sampler_info.addressModeW =
      to_vk_sampler_address_mode(creation.address_mode_w);
  sampler_info.mipmapMode = to_vk_sampler_mipmap_mode(creation.mip_filter);
  sampler_info.anisotropyEnable = VK_FALSE;
  sampler_info.maxAnisotropy = 1.f;
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

BindingSetLayoutHandle VkGpuDevice::create_binding_set_layout(
    const BindingSetLayoutCreation &creation) {
  BindingSetLayoutHandle handle = descriptor_set_layouts.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Vulkan Descriptor Set Layout!");
    return handle;
  }

  VulkanDescriptorSetLayout *layout = descriptor_set_layouts.obtain(handle);
  layout->name = creation.name;
  layout->is_bindless = creation.is_bindless;

  VkDescriptorSetLayoutBinding bindings[MAX_BINDING_PER_SET];
  for (u32 i = 0; i < creation.binding_count; ++i) {
    const BindingInfo &binding_info = creation.binding_infos[i];
    VkDescriptorSetLayoutBinding &binding = bindings[i];
    binding.descriptorType = to_vk_descriptor_type(binding_info.type);
    binding.descriptorCount = binding_info.resource_count;
    binding.binding = binding_info.binding;
    binding.stageFlags = to_vk_shader_stage(binding_info.stage);
    binding.pImmutableSamplers = nullptr;
  }

  VkDescriptorSetLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = creation.binding_count;
  layout_info.pBindings = bindings;
  layout_info.flags =
      creation.is_bindless
          ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
          : 0;

  VkDescriptorBindingFlags bindless_flags =
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo extended_info{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      nullptr};
  extended_info.bindingCount = 1;
  extended_info.pBindingFlags = &bindless_flags;

  layout_info.pNext = creation.is_bindless ? &extended_info : nullptr;

  VK_CHECK(vkCreateDescriptorSetLayout(
      vk_device, &layout_info, vk_allocation_callbacks, &layout->vk_handle));

  set_resource_name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    (u64)layout->vk_handle, creation.name);

  return handle;
}

BindingSetHandle
VkGpuDevice::create_binding_set(const BindingSetCreation &creation) {
  BindingSetHandle handle = descriptor_sets.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Vulkan Descriptor Set!");
    return handle;
  }

  VulkanDescriptorSet *set = descriptor_sets.obtain(handle);
  set->set_layout = creation.layout;
  set->name = creation.name;

  VulkanDescriptorSetLayout *layout =
      access_descriptor_set_layout(creation.layout);

  VkDescriptorSetLayout vk_layout = layout->vk_handle;
  VkDescriptorSetAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool =
      layout->is_bindless ? vk_bindless_pool : vk_descriptor_pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &vk_layout;

  VK_CHECK(vkAllocateDescriptorSets(vk_device, &alloc_info, &set->vk_handle));

  set_resource_name(VK_OBJECT_TYPE_DESCRIPTOR_SET, (u64)set->vk_handle,
                    creation.name);
  return handle;
}

PipelineHandle VkGpuDevice::create_pipeline(const PipelineCreation &creation) {
  PipelineHandle handle = pipelines.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain VulkanPipeline");
    return handle;
  }

  VulkanPipeline *pipeline = access_pipeline(handle);
  pipeline->vk_handle = VK_NULL_HANDLE;

  HeapAllocator *allocator = &MemoryService::instance()->system_allocator;
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;

  // Parse shaders
  StringBuffer temp_string_buffer{};
  temp_string_buffer.init(stack_allocator, hkilo(1));

  char *vulkan_sdk_path = temp_string_buffer.reserve(512);
  FileService::expand_enviroment_variable("%VULKAN_SDK%", vulkan_sdk_path, 512);
  cstring glsl_compiler_path = temp_string_buffer.append_use_f(
      "%s\\Bin\\glslangValidator.exe", vulkan_sdk_path);
#ifdef SHADER_DEBUG_SYMBOLS
  cstring compiler_debug = "-gVS";
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
  FileService::current_directory(&dir);
  FileService::change_directory(ASSETS_PATH "/Shaders/");

  ParseResult parse_result{};
  parse_result.push_constant.size = 0;

  // Create shader spv and extract shader data from them
  for (u32 i = 0; i < creation.shader_count; ++i) {
    ShaderCreateInfo shader = creation.shader_create_infos[i];
    cstring shader_args = temp_string_buffer.append_use_f(
        " -V -S %s %s.glsl -o %s.spv --target-env vulkan1.3 %s -D_GLSL",
        to_compiler_stage(shader.stage), shader.filename, shader.filename,
        compiler_debug);
    HASSERT(process_execute(".", glsl_compiler_path, shader_args));

    // TODO: Maybe create a timestamp system for checking shaders.
    cstring binary_name =
        temp_string_buffer.append_use_f("%s.spv", shader.filename);
    FileReadResult shader_binary{};
    FileService::open_read_file_binary(binary_name, &shader_binary,
                                       stack_allocator);

    if (shader_binary.data == nullptr) {
      FileReadResult glsl_code{};
      FileService::open_read_file_binary(
          temp_string_buffer.append_use_f("%s.glsl", shader.filename),
          &glsl_code, stack_allocator);
      if (glsl_code.data)
        HTRACE("\n{}", glsl_code.data);
      HERROR("\n{}", process_get_output());
    }

    FileService::delete_file(binary_name);

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
    pipeline_stage_info.stage =
        (VkShaderStageFlagBits)to_vk_shader_stage(shader.stage);
    pipeline_stage_info.module = vk_shader_modules[i];
    pipeline_stage_info.pName = "main";
    pipeline_stage_info.pSpecializationInfo = nullptr;
    pipeline_stage_info.flags = 0;
    pipeline_stage_info.pNext = nullptr;

    parse_binary((u32 *)shader_binary.data, shader_binary.size, parse_result);
  }

  temp_string_buffer.clear();

  // Descriptor Set Layouts
  // TODO: MAgic numbers
  VkDescriptorSetLayout layouts[5];
  HASSERT(creation.set_layout_count <= 5);
  for (u32 i = 0; i < creation.set_layout_count; ++i) {
    layouts[i] =
        access_descriptor_set_layout(creation.set_layouts[i])->vk_handle;
  }

  // Pipeline Layout
  VkPipelineLayoutCreateInfo pipeline_layout_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_info.setLayoutCount = creation.set_layout_count;
  pipeline_layout_info.pSetLayouts = layouts;
  if (parse_result.push_constant.size != 0) {
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &parse_result.push_constant;
  }

  VK_CHECK(vkCreatePipelineLayout(vk_device, &pipeline_layout_info,
                                  vk_allocation_callbacks,
                                  &pipeline->vk_layout));

  // Pipeline Cache
  VkPipelineCache pipeline_cache{VK_NULL_HANDLE};
  VkPipelineCacheCreateInfo pipeline_cache_create_info{
      VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};

  cstring cache_path =
      temp_string_buffer.append_use_f("%s\\%s.cache", "Caches", creation.name);
  bool cache_exists = FileService::file_exists(cache_path);
  if (cache_exists) {
    FileReadResult read_result{};
    FileService::open_read_file_binary(cache_path, &read_result, allocator);
    VkPipelineCacheHeaderVersionOne *cache_header =
        (VkPipelineCacheHeaderVersionOne *)read_result.data;

    if (cache_header->deviceID ==
            vk_physical_device_properties2.properties.deviceID &&
        cache_header->vendorID ==
            vk_physical_device_properties2.properties.vendorID &&
        memcmp(cache_header->pipelineCacheUUID,
               vk_physical_device_properties2.properties.pipelineCacheUUID,
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
        parse_result.vertex_attribute_count
            ? 1
            : 0; // TODO: For now assume only one vertex binding
    vertex_input_state.pVertexBindingDescriptions =
        &parse_result.vertex_binding;
    vertex_input_state.vertexAttributeDescriptionCount =
        parse_result.vertex_attribute_count;
    vertex_input_state.pVertexAttributeDescriptions =
        parse_result.vertex_attributes;

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = to_vk_primitive_topology(creation.primitive_type);
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

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
    depth_stencil.depthTestEnable =
        creation.enable_depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable =
        creation.enable_depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = to_vk_compare_op(creation.compare_op);
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.minDepthBounds = 0.0f;
    depth_stencil.maxDepthBounds = 1.0f;
    depth_stencil.stencilTestEnable = VK_FALSE;

    // Color Blend State
    // TODO: Make configurable
    RenderPass *render_pass = access_render_pass(creation.render_pass);
    Array<VkPipelineColorBlendAttachmentState> color_blend_attachments = {};
    color_blend_attachments.init(stack_allocator,
                                 render_pass->num_colour_attachments,
                                 render_pass->num_colour_attachments);
    for (u32 i = 0; i < render_pass->num_colour_attachments; ++i) {
      color_blend_attachments[i].colorWriteMask =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      color_blend_attachments[i].blendEnable = VK_TRUE;
      // (render_pass->colour_attachments[i].format ==
      // TextureFormat::R32_UINT)
      //     ? VK_FALSE
      //     : VK_TRUE; // TODO: Hard coded
      color_blend_attachments[i].srcColorBlendFactor =
          VK_BLEND_FACTOR_SRC_ALPHA;
      color_blend_attachments[i].dstColorBlendFactor =
          VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      color_blend_attachments[i].colorBlendOp = VK_BLEND_OP_ADD;
      color_blend_attachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      color_blend_attachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      color_blend_attachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_CLEAR;
    color_blending.attachmentCount = color_blend_attachments.size;
    color_blending.pAttachments = color_blend_attachments.data;
    // Dynamic Rendering
    VkFormat color_formats[MAX_COLOR_ATTACHMENTS];
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    if (is_handle_valid(render_pass->depth_attachment.texture_handle)) {
      VulkanImageView *depth_view =
          access_image_view(render_pass->depth_attachment.texture_handle);
      VulkanImage *depth_image = access_image(depth_view->image);
      depth_format = depth_image->vk_format;
    }
    for (u32 i = 0; i < render_pass->num_colour_attachments; i++) {
      VulkanImage *image =
          access_image(render_pass->colour_attachments[i].texture_handle);
      color_formats[i] = image->vk_format;
    }

    VkPipelineRenderingCreateInfo pipeline_rendering_create{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    pipeline_rendering_create.pNext = VK_NULL_HANDLE;
    pipeline_rendering_create.colorAttachmentCount =
        render_pass->num_colour_attachments;
    pipeline_rendering_create.pColorAttachmentFormats = color_formats;
    pipeline_rendering_create.depthAttachmentFormat = depth_format;
    pipeline_rendering_create.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline_info.stageCount = creation.shader_count;
    pipeline_info.pStages = vk_shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline->vk_layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.pNext = &pipeline_rendering_create;

    (vkCreateGraphicsPipelines(vk_device, pipeline_cache, 1, &pipeline_info,
                               vk_allocation_callbacks, &pipeline->vk_handle));
  } else if (creation.pipeline_type == PipelineType::Compute) {
    VkComputePipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.layout = pipeline->vk_layout;
    pipeline_info.stage = vk_shader_stages[0];

    vkCreateComputePipelines(vk_device, pipeline_cache, 1, &pipeline_info,
                             vk_allocation_callbacks, &pipeline->vk_handle);
  } else {
    HASSERT_MSG(false, "Unknown pipeline type");
  }

  pipeline->bind_point = to_vk_bind_point(creation.pipeline_type);
  // Update Pipeline Cache
  if (!cache_exists) {
    size_t cache_data_size = 0;
    VK_CHECK(vkGetPipelineCacheData(vk_device, pipeline_cache, &cache_data_size,
                                    nullptr));

    void *cache_data = stack_allocator->allocate(cache_data_size, 64);
    VK_CHECK(vkGetPipelineCacheData(vk_device, pipeline_cache, &cache_data_size,
                                    cache_data));

    FileService::write_file_binary(cache_path, cache_data, cache_data_size);

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

  FileService::change_directory(dir.path);
  return handle;
}

RenderPassHandle
VkGpuDevice::create_render_pass(const RenderPassCreation &creation) {
  RenderPassHandle handle = render_passes.obtain_new();
  if (!is_handle_valid(handle)) {
    HERROR("Failed to obtain a Render Pass!");
    return handle;
  }

  RenderPass *pass = render_passes.obtain(handle);
  pass->depth_attachment = creation.depth_attachment;
  pass->num_colour_attachments = creation.num_colour_attachments;
  memcpy(pass->colour_attachments, creation.colour_attachments,
         sizeof(AttachmentInfo) * creation.num_colour_attachments);

  return handle;
}

void VkGpuDevice::destroy_buffer(BufferHandle handle) {
  if (!is_handle_valid(handle)) {
    HWARN("Attempting to free an invalid VulkanBuffer");
    return;
  }
  ResourceQueueObject q_object{VK_OBJECT_TYPE_BUFFER, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_texture(TextureHandle handle) {
  if (!is_handle_valid(handle)) {
    HWARN("Attempting to free an invalid Texture");
    return;
  }

  VulkanImageView *view = access_image_view(handle);
  destroy_image(view->image);
  destroy_image_view(handle);
}

void VkGpuDevice::destroy_image(VkImageHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanImage");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_IMAGE, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_image_view(VkImageViewHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanImageView");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_IMAGE_VIEW, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_sampler(SamplerHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanSampler");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_SAMPLER, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_binding_set(BindingSetHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanDescriptorSet");
    return;
  }
  descriptor_sets.release(handle);
}

void VkGpuDevice::destroy_binding_set_layout(BindingSetLayoutHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanDescriptorSetLayout");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_pipeline(PipelineHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanPipeline");
    return;
  }

  ResourceQueueObject q_object{VK_OBJECT_TYPE_PIPELINE, handle};
  resource_deletion_queue.push(q_object);
}

void VkGpuDevice::destroy_render_pass(RenderPassHandle handle) {
  render_passes.release(handle);
}

void VkGpuDevice::resize_texture(TextureHandle handle, u32 width, u32 height) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to resize an invalid Texture");
    return;
  }

  VulkanImageView *view = access_image_view(handle);
  VulkanImage *image = access_image(view->image);

  vmaDestroyImage(vma_allocator, image->vk_handle, image->vma_allocation);
  vkDestroyImageView(vk_device, view->vk_handle, vk_allocation_callbacks);

  VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.extent.width = width;
  image_info.extent.height = height;
  image_info.extent.depth = image->vk_extents.depth;
  image_info.mipLevels = image->mip_count;
  image_info.arrayLayers = 1;
  image_info.format = image->vk_format;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;

  image_info.usage = image->vk_usage;

  VmaAllocationCreateInfo memory_info{};
  memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VK_CHECK(vmaCreateImage(vma_allocator, &image_info, &memory_info,
                          &image->vk_handle, &image->vma_allocation, nullptr));

  set_resource_name(VK_OBJECT_TYPE_IMAGE, (u64)image->vk_handle, image->name);
  vmaSetAllocationName(vma_allocator, image->vma_allocation, image->name);

  image->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image->vk_extents = {width, height, image->vk_extents.depth};
  image->mip_count = image->mip_count;

  VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = image->vk_handle;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = image->vk_format;
  view_info.subresourceRange.aspectMask = has_depth_or_stencil(image->vk_format)
                                              ? VK_IMAGE_ASPECT_DEPTH_BIT
                                              : VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.baseMipLevel = view->base_mip_level;
  view_info.subresourceRange.levelCount = image->mip_count;
  view_info.subresourceRange.baseArrayLayer = view->base_array_level;
  view_info.subresourceRange.layerCount = 1;

  VK_CHECK(vkCreateImageView(vk_device, &view_info, vk_allocation_callbacks,
                             &view->vk_handle));
  set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (u64)view->vk_handle,
                    view->name);
}

void VkGpuDevice::destroy_buffer_instant(BufferHandle handle) {
  if (!is_handle_valid(handle)) {
    HWARN("Attempting to free an invalid VulkanBuffer");
    return;
  }
  VulkanBuffer *buffer = access_buffer(handle);
  if (!buffer)
    return;

  VmaAllocationInfo alloc_info{};
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

void VkGpuDevice::destroy_image_instant(VkImageHandle handle) {
  VulkanImage *image = images.obtain(handle);
  if (!image)
    return;

  vmaDestroyImage(vma_allocator, image->vk_handle, image->vma_allocation);
  images.release(handle);
}

void VkGpuDevice::destroy_image_view_instant(VkImageViewHandle handle) {
  VulkanImageView *image_view = image_views.obtain(handle);
  if (!image_view)
    return;

  if (image_view) {
    vkDestroyImageView(vk_device, image_view->vk_handle,
                       vk_allocation_callbacks);
    image_views.release(handle);
  }
}

void VkGpuDevice::destroy_sampler_instant(SamplerHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanSampler");
    return;
  }

  VulkanSampler *sampler = access_sampler(handle);
  vkDestroySampler(vk_device, sampler->vk_handle, vk_allocation_callbacks);
  samplers.release(handle);
}

void VkGpuDevice::destroy_binding_set_layout_instant(
    BindingSetLayoutHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanDescriptorSetLayout");
    return;
  }

  VulkanDescriptorSetLayout *layout = access_descriptor_set_layout(handle);
  vkDestroyDescriptorSetLayout(vk_device, layout->vk_handle,
                               vk_allocation_callbacks);
  descriptor_set_layouts.release(handle);
}

void VkGpuDevice::destroy_pipeline_instant(PipelineHandle handle) {
  if (!is_handle_valid(handle)) {
    HERROR("Attempting to free an invalid VulkanPipeline");
    return;
  }

  VulkanPipeline *pipeline = access_pipeline(handle);
  if (!pipeline)
    return;

  vkDestroyPipelineLayout(vk_device, pipeline->vk_layout,
                          vk_allocation_callbacks);
  vkDestroyPipeline(vk_device, pipeline->vk_handle, vk_allocation_callbacks);

  pipelines.release(handle);
}

// TODO: Implement other objects
void VkGpuDevice::free_queued_resources() {
  if (resource_deletion_queue.size > 0) {
    vkDeviceWaitIdle(vk_device);
    for (i32 i = resource_deletion_queue.size - 1; i >= 0; --i) {
      ResourceQueueObject &queue_object = resource_deletion_queue[i];
      switch (queue_object.type) {
      case VK_OBJECT_TYPE_BUFFER:
        destroy_buffer_instant(queue_object.handle);
        break;
      case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        destroy_binding_set_layout_instant(queue_object.handle);
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

Context *VkGpuDevice::create_context(ContextType::Enum type) {
  void *memory =
      halloca(sizeof(VkContext), &MemoryService::instance()->system_allocator);
  VkContext *context = new (memory) VkContext();
  context->device = this;
  context->wait_semaphore = VK_NULL_HANDLE;
  context->signal_semaphore = VK_NULL_HANDLE;
  context->timeline_semaphore = VK_NULL_HANDLE;
  context->signal_value = 0;

  u32 queue_index;
  switch (type) {
  case ContextType::Graphics: {
    context->vk_queue = vk_graphics_queue;
    queue_index = queue_family_indices.graphics_family_index;
    break;
  }
  case ContextType::Compute: {
    context->vk_queue = vk_compute_queue;
    queue_index = queue_family_indices.compute_family_index;
    break;
  }
  case ContextType::Transfer: {
    context->vk_queue = vk_transfer_queue;
    queue_index = queue_family_indices.transfer_family_index;
    break;
  }
  default: {
    HERROR("Failed to create Context: Unkown ContextType!");
    MemoryService::instance()->system_allocator.deallocate(memory);
    context = nullptr;
  }
  }

  if (context) {
    VkCommandPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_index;
    VK_CHECK(vkCreateCommandPool(vk_device, &pool_info, vk_allocation_callbacks,
                                 &context->vk_command_pool));
    set_resource_name(VK_OBJECT_TYPE_COMMAND_POOL,
                      (u64)context->vk_command_pool, "VkContext_CommandPool");

    for (u32 i = 0; i < 2; ++i) {
      context->command_buffers[i].init(context->vk_command_pool,
                                       VK_COMMAND_BUFFER_LEVEL_PRIMARY, this);
    }
  }

  return context;
}

void VkGpuDevice::destroy_context(Context *context) {
  if (!context)
    return;

  VkContext *vk_context = (VkContext *)context;

  vkQueueWaitIdle(vk_context->vk_queue);

  vkDestroyCommandPool(vk_device, vk_context->vk_command_pool,
                       vk_allocation_callbacks);
  MemoryService::instance()->system_allocator.deallocate(context);
}

WorkReceipt *VkGpuDevice::create_receipt() {
  void *memory = halloca(sizeof(VkWorkReceipt),
                         &MemoryService::instance()->system_allocator);
  VkWorkReceipt *receipt = new (memory) VkWorkReceipt();
  receipt->wait_value = 0;
  receipt->vk_timeline_semaphore = VK_NULL_HANDLE;

  return receipt;
}

PipelineInfo VkGpuDevice::access_pipeline_view(PipelineHandle handle) {
  PipelineInfo info{};
  VulkanPipeline *pipeline = access_pipeline(handle);
  info.name = pipeline->name;
  return info;
}

void VkGpuDevice::destroy_receipt(WorkReceipt *receipt) {
  if (!receipt)
    return;

  MemoryService::instance()->system_allocator.deallocate(receipt);
}

u32 VkGpuDevice::get_next_image_index(Context *context,
                                      u32 current_frame_in_flight) {
  VkResult result = vkAcquireNextImageKHR(
      vk_device, swapchain.vk_handle, UINT64_MAX,
      vk_image_available_semaphores[current_frame_in_flight], VK_NULL_HANDLE,
      &swapchain.current_image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    resize_swapchain();
    return -1; // TODO: Maybe or maybe return UINT_MAX
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    HERROR("Failed to acquire swap chain image!");
    return -1; // TODO: Maybe or maybe return UINT_MAX
  }

  VkContext *vk_context = (VkContext *)context;
  vk_context->wait_semaphore =
      vk_image_available_semaphores[current_frame_in_flight];
  vk_context->signal_semaphore =
      vk_render_finished_semaphores[swapchain.current_image_index];
  vk_context->timeline_semaphore = vk_timeline_semaphore;
  vk_context->signal_value = frame_number + max_frames_in_flight;

  return swapchain.current_image_index;
}

TextureHandle VkGpuDevice::get_backbuffer_texture(u32 index) {
  return swapchain.image_views[index];
}

bool VkGpuDevice::update_binding_set(BindingSetHandle set,
                                     BindingSetUpdateInfo *update_infos,
                                     u32 update_count) {
  VulkanDescriptorSet *d_set = access_descriptor_set(set);

  VkWriteDescriptorSet descriptor_writes[MAX_BINDING_PER_SET];
  VkDescriptorBufferInfo buffer_infos[MAX_BINDING_PER_SET];
  VkDescriptorImageInfo image_infos[MAX_BINDING_PER_SET];
  for (u32 i = 0; i < update_count; i++) {
    BindingSetUpdateInfo &update_info = update_infos[i];
    if (update_info.resource_type == ResourceType::Texture) {
      VulkanImageView *image_view =
          access_image_view(update_info.resource_handle);
      VulkanSampler *sampler = access_sampler(image_view->sampler);

      VkDescriptorImageInfo &image_info = image_infos[i];
      image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image_info.imageView = image_view->vk_handle;
      image_info.sampler = sampler->vk_handle;

      VkWriteDescriptorSet &descriptor_write = descriptor_writes[i];
      descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptor_write.pNext = nullptr;
      descriptor_write.pImageInfo = &image_info;
      descriptor_write.pTexelBufferView = nullptr;
      descriptor_write.dstSet = d_set->vk_handle;
      descriptor_write.dstBinding = update_info.binding;
      descriptor_write.dstArrayElement = update_info.resource_index;
      descriptor_write.descriptorType =
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptor_write.descriptorCount = 1;
      descriptor_write.pBufferInfo = nullptr;
    } else {
      HERROR("Unkown descriptor type");
      return false;
    }
  }

  vkUpdateDescriptorSets(vk_device, update_count, descriptor_writes, 0,
                         nullptr);
  return true;
}

void VkGpuDevice::submit_work(Context *context_, WorkReceipt *receipt) {
  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;
  VkContext *vk_context = (VkContext *)context_;
  // Submit
  VkCommandBufferSubmitInfo *command_submit_infos =
      (VkCommandBufferSubmitInfo *)halloca(sizeof(VkCommandBufferSubmitInfo) *
                                               vk_context->ready_buffer_count,
                                           stack_allocator);

  for (u32 i = 0; i < vk_context->ready_buffer_count; ++i) {
    VkCommandBufferSubmitInfo *command_submit_info = &command_submit_infos[i];
    command_submit_info->sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit_info->pNext = nullptr;
    command_submit_info->commandBuffer =
        vk_context->command_buffers[vk_context->ready_buffers_index[i]]
            .vk_handle;
    command_submit_info->deviceMask = 0;
  }

  bool has_wait_semaphore = vk_context->wait_semaphore != VK_NULL_HANDLE;
  bool has_signal_semphore = vk_context->signal_semaphore != VK_NULL_HANDLE;

  VkSemaphoreSubmitInfo wait_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait_semaphore_submit_info.semaphore = vk_context->wait_semaphore;
  wait_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signal_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal_semaphore_submit_info.semaphore = vk_context->signal_semaphore;
  signal_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signal_timeline_semaphore_submit_info{
      VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal_timeline_semaphore_submit_info.semaphore =
      vk_context->timeline_semaphore;
  signal_timeline_semaphore_submit_info.stageMask =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  signal_timeline_semaphore_submit_info.value = vk_context->signal_value;

  VkSemaphoreSubmitInfo signal_semaphore_submit_infos[2] = {
      signal_semaphore_submit_info, signal_timeline_semaphore_submit_info};

  VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit_info.commandBufferInfoCount = vk_context->ready_buffer_count;
  submit_info.pCommandBufferInfos = command_submit_infos;

  submit_info.waitSemaphoreInfoCount = has_wait_semaphore ? 1 : 0;
  submit_info.pWaitSemaphoreInfos =
      has_wait_semaphore ? &wait_semaphore_submit_info : nullptr;

  submit_info.signalSemaphoreInfoCount =
      has_signal_semphore ? ArraySize(signal_semaphore_submit_infos) : 0;
  submit_info.pSignalSemaphoreInfos =
      has_signal_semphore ? signal_semaphore_submit_infos : nullptr;

  VK_CHECK(
      vkQueueSubmit2(vk_context->vk_queue, 1, &submit_info, VK_NULL_HANDLE));

  // Clear ready buffer count
  vk_context->ready_buffer_count = 0;

  if (receipt) {
    VkWorkReceipt *vk_receipt = (VkWorkReceipt *)receipt;
    vk_receipt->vk_timeline_semaphore = vk_context->timeline_semaphore;
    vk_receipt->wait_value = vk_context->signal_value;
  }

  vk_context->wait_semaphore = VK_NULL_HANDLE;
  vk_context->signal_semaphore = VK_NULL_HANDLE;
  vk_context->timeline_semaphore = VK_NULL_HANDLE;
}

void VkGpuDevice::wait_on_work(WorkReceipt *receipt) {
  HASSERT(receipt);
  VkWorkReceipt *vk_receipt = (VkWorkReceipt *)receipt;

  if (vk_receipt->vk_timeline_semaphore == VK_NULL_HANDLE)
    return;
  VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wait_info.semaphoreCount = 1;
  wait_info.pSemaphores = &vk_receipt->vk_timeline_semaphore;
  wait_info.pValues = &vk_receipt->wait_value;

  vkWaitSemaphores(vk_device, &wait_info, UINT64_MAX);
}

void VkGpuDevice::present_to_display() {
  VkSemaphore wait_semaphores[] = {
      vk_render_finished_semaphores[swapchain.current_image_index]};

  VkPresentInfoKHR present_info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = wait_semaphores;

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
}

void VkGpuDevice::set_render_pass_texture(RenderPassHandle handle,
                                          TextureHandle texture_handle,
                                          bool is_depth, u32 index) {
  RenderPass *render_pass = access_render_pass(handle);
  if (!render_pass)
    return;

  if (is_depth) {
    render_pass->depth_attachment.texture_handle = texture_handle;
  } else {
    render_pass->colour_attachments[index].texture_handle = texture_handle;
  }
}

void *VkGpuDevice::get_buffer_map(BufferHandle handle) {
  VulkanBuffer *buffer = access_buffer(handle);
  return buffer->mapped_data;
}

void VkGpuDevice::resize_backbuffers() { resize_swapchain(); }

void VkGpuDevice::set_resource_name(VkObjectType type, u64 handle,
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

void VkGpuDevice::create_swapchain() {
  VkExtent2D swapchain_extents{};
  query_swapchain_support(vk_physical_device, vk_surface, swapchain,
                          swapchain_extents);
  VkSurfaceCapabilitiesKHR surface_capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, vk_surface,
                                            &surface_capabilities);

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = vk_surface;
  create_info.minImageCount = swapchain.image_count;
  create_info.imageFormat = swapchain.vk_surface_format.format;
  create_info.imageColorSpace = swapchain.vk_surface_format.colorSpace;
  create_info.imageExtent = swapchain_extents;
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

  ScopedAllocator scope_allocator(&MemoryService::instance()->stack_allocator);
  StackAllocator *stack_allocator = scope_allocator.allocator;
  HASSERT(swapchain.image_count <= MAX_SWAPCHAIN_IMAGES);
  vkGetSwapchainImagesKHR(vk_device, swapchain.vk_handle,
                          &swapchain.image_count, nullptr);
  VkImage *vk_images = (VkImage *)halloca(
      sizeof(VkImage) * swapchain.image_count, stack_allocator);
  vkGetSwapchainImagesKHR(vk_device, swapchain.vk_handle,
                          &swapchain.image_count, vk_images);

  for (u32 i = 0; i < swapchain.image_count; ++i) {
    // Create VulkanImage resources for the swapchain images
    VulkanImage *image = access_image(swapchain.images[i]);
    image->vk_handle = vk_images[i];
    image->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->vk_format = swapchain.vk_surface_format.format;
    image->vk_extents = {swapchain_extents.width, swapchain_extents.height, 1};
    image->mip_count = 1;

    VulkanImageView *image_view = access_image_view(swapchain.image_views[i]);
    image_view->image = swapchain.images[i];

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image->vk_handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = swapchain.vk_surface_format.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(vk_device, &view_info, vk_allocation_callbacks,
                               &image_view->vk_handle));
    set_resource_name(VK_OBJECT_TYPE_IMAGE_VIEW, (u64)image_view->vk_handle,
                      "SwapchainImageView");

    // command_buffer.transition_image(
    //     swapchain.images[i], VK_IMAGE_LAYOUT_UNDEFINED,
    //     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    //     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  }

  // command_buffer.end();

  // VkCommandBufferSubmitInfo command_submit_info{
  //     VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  // command_submit_info.commandBuffer = command_buffer.vk_handle;
  //
  // VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  // submit_info.flags = 0;
  // submit_info.commandBufferInfoCount = 1;
  // submit_info.pCommandBufferInfos = &command_submit_info;
  //
  // vkQueueSubmit2(vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
  // vkQueueWaitIdle(vk_graphics_queue);

  HTRACE("Created swapchain {}x{} successfully", swapchain_extents.width,
         swapchain_extents.height);
}

void VkGpuDevice::destroy_swapchain() {
  for (u32 i = 0; i < swapchain.image_count; ++i) {
    VulkanImage *image = access_image(swapchain.images[i]);
    VulkanImageView *image_view = access_image_view(swapchain.image_views[i]);
    vkDestroyImageView(vk_device, image_view->vk_handle,
                       vk_allocation_callbacks);
    image->vk_handle = VK_NULL_HANDLE;
    image_view->vk_handle = VK_NULL_HANDLE;
  }

  vkDestroySwapchainKHR(vk_device, swapchain.vk_handle,
                        vk_allocation_callbacks);
  swapchain.vk_handle = VK_NULL_HANDLE;
}

void VkGpuDevice::resize_swapchain() {
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

} // namespace Helix
