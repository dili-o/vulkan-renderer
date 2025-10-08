#include "VkGpuDevice.hpp"
#include "Core/Assert.hpp"
#include "Core/Memory.hpp"
#include "Core/Profiler.hpp"
#include "Platform/Platform.hpp"
#include "Renderer/RendererBackend.hpp"
#include "Renderer/RendererTypes.hpp"
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
  if (!select_physical_device(device->vk_instance, device->vk_physical_device,
                              &device->vk_physical_device_properties,
                              device->queue_family_indices,
                              device->vk_surface)) {
    HERROR("Failed to create physical device!");
    free(device);
    return nullptr;
  }

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

  // Init Resource Pools
  // TODO: Make configurable
  device->images.init(allocator, 1000);
  // TODO: Make configurable
  device->image_views.init(allocator, 1000);
  // TODO: Make configurable
  device->render_passes.init(allocator, 10);

  // TODO: Remove
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex =
      device->queue_family_indices.graphics_family_index;
  VK_CHECK(vkCreateCommandPool(device->vk_device, &pool_info,
                               device->vk_allocation_callbacks,
                               &device->vk_command_pool));
  device->set_resource_name(VK_OBJECT_TYPE_COMMAND_POOL,
                            (u64)device->vk_command_pool,
                            "VkGpuDevice_CommandPool");

  device->command_buffer.init(device->vk_command_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY, device);

  // TODO: Make this configurable
  device->vk_image_available_semaphores.init(allocator, 2, 2);

  VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (u32 i = 0; i < device->vk_image_available_semaphores.size; ++i) {

    VK_CHECK(vkCreateSemaphore(device->vk_device, &semaphore_info,
                               device->vk_allocation_callbacks,
                               &device->vk_image_available_semaphores[i]));
    // TODO:
    // set_resource_name(
    //     VK_OBJECT_TYPE_SEMAPHORE, (u64)image_available_semaphores[i],
    //     string_buffer.append_use_f("image_available_semaphore_%d", i));
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

  device->frame_number = 0;

  HINFO("Vulkan Backend Initialized");

  return device;
}

void destroy_vulkan_device(VkGpuDevice *device) {
  vkDeviceWaitIdle(device->vk_device);

  device->destroy_swapchain();
  for (u32 i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
    device->images.release(device->swapchain.images[i]);
    device->image_views.release(device->swapchain.image_views[i]);
  }

  device->images.shutdown();
  device->image_views.shutdown();
  device->render_passes.shutdown();

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

  device->vk_image_available_semaphores.shutdown();
  device->vk_render_finished_semaphores.shutdown();

  // TODO: Remove
  vkDestroyCommandPool(device->vk_device, device->vk_command_pool,
                       device->vk_allocation_callbacks);

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
    // set_resource_name(
    //     VK_OBJECT_TYPE_SEMAPHORE, (u64)vk_render_finished_semaphores[i],
    //     string_buffer.append_use_f("RenderFinished_Semaphore%d", i));
  }
  return count;
}

void VkGpuDevice::process_display_changes() {}

void VkGpuDevice::create_buffer() {}

void VkGpuDevice::create_texture() {}

void VkGpuDevice::create_pipeline() {}

RenderPassHandle
VkGpuDevice::create_render_pass(const RenderPassCreation &creation) {
  RenderPassHandle handle = render_passes.obtain_new();
  if (handle.index == k_invalid_index) {
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
void VkGpuDevice::destroy_render_pass(RenderPassHandle handle) {
  render_passes.release(handle);
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

  submit_info.waitSemaphoreInfoCount = 1;
  submit_info.pWaitSemaphoreInfos = &wait_semaphore_submit_info;

  submit_info.signalSemaphoreInfoCount =
      ArraySize(signal_semaphore_submit_infos);
  submit_info.pSignalSemaphoreInfos = signal_semaphore_submit_infos;

  VK_CHECK(
      vkQueueSubmit2(vk_context->vk_queue, 1, &submit_info, VK_NULL_HANDLE));

  // Clear ready buffer count
  vk_context->ready_buffer_count = 0;

  if (receipt) {
    VkWorkReceipt *vk_receipt = (VkWorkReceipt *)receipt;
    vk_receipt->vk_timeline_semaphore = vk_context->timeline_semaphore;
    vk_receipt->wait_value = vk_context->signal_value;
  }
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

void VkGpuDevice::resize_backbuffers() { resize_frame = true; }

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

//
// VkContext
//
void VkContext::begin(u32 cbuffer_index_) {
  command_buffers[cbuffer_index_].begin();

  cbuffer_index = cbuffer_index_;
};

void VkContext::end(u32 cbuffer_index) {
  command_buffers[cbuffer_index].end();
  ready_buffers_index[ready_buffer_count++] = cbuffer_index;
};

void VkContext::resource_barrier(const BarrierDescription *barrier) {
  if (barrier->resource_type == ResourceType::Texture) {
#define SRC_INDEX 0
#define DST_INDEX 1
    VkPipelineStageFlags2 stages[2];
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout dst_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 *src_stage = stages;
    VkPipelineStageFlags2 *dst_stage = stages + 1;

    if (barrier->dst_state == ResourceState::RenderTarget) {
      dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      stages[DST_INDEX] = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (barrier->dst_state == ResourceState::Present) {
      dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      dst_stage = stages + SRC_INDEX;
    }

    if (barrier->src_state == ResourceState::Present) {
      src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      src_stage = stages + DST_INDEX;
    } else if (barrier->src_state == ResourceState::RenderTarget) {
      src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      stages[SRC_INDEX] = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    VulkanImage *image = device->access_image(barrier->resource_handle);
    command_buffers[cbuffer_index].transition_image(
        image, src_layout, dst_layout, *src_stage, *dst_stage);
  } else {
    HASSERT_MSG(false, "Implement Buffer Resource Barriers");
  }
};

// Graphics
void VkContext::bind_pipeline() {};
void VkContext::bind_vertex_buffer() {};
void VkContext::bind_index_buffer() {};
void VkContext::draw() {};

void VkContext::bind_renderpass(RenderPassHandle handle) {
  command_buffers[cbuffer_index].bind_renderpass(handle);
}

void VkContext::end_current_pass() {
  command_buffers[cbuffer_index].end_current_renderpass();
}

// Compute
void VkContext::dispatch(u32 x, u32 y, u32 z) {};
// Transfer
void VkContext::data_to_buffer() {};
void VkContext::buffer_to_buffer() {};
void VkContext::buffer_to_texture() {};

//
// VkWorkReceipt
//

} // namespace Helix
