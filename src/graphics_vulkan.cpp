/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define VK_NO_PROTOTYPES
#include "volk.h"

#ifndef VK_USE_PLATFORM_WIN32_KHR
    #ifdef _WIN32
        #define VK_USE_PLATFORM_WIN32_KHR
    #endif
#endif
#ifndef VK_USE_PLATFORM_METAL_EXT
    #ifdef __APPLE__
        #define VK_USE_PLATFORM_METAL_EXT
    #endif
#endif

#include "vk_mem_alloc.h"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include "model.h"

#include <shaderc/shaderc.h>

/* Constants */
static const uint32_t MIN_SWAPCHAIN_IMAGES = 2;
static const float CLEAR_COLOR[4] = {0.01f, 0.01f, 0.033f, 1.0f};

/* 4.2. Instances */
static VkInstance instance;

/* 5. Devices and Queues */
static VkPhysicalDevice *physicalDevices;
static uint32_t graphicsQueueFamily;

/* 5.2.1. Device Creation */
static VkDevice device;

/* VmaAllocator Struct */
static VmaAllocator allocator;

/* 5.3.2. Queue Creation */
static VkQueue queue;

/* 6. Command Buffers */
static VkCommandBuffer *commandBuffers;

/* 6.2. Command Pools */
static VkCommandPool *commandPools;

/* 7.3. Fences */
static VkFence *fences;

/* 7.4. Semaphores */
static VkSemaphore acquireSemaphore;
static VkSemaphore releaseSemaphore;

/* 8. Render Pass */
static VkRenderPass renderPass;

/* 8.3. Framebuffers */
static VkFramebuffer *framebuffers;

/* 9. Shaders */
static Shader vertShader;
static Shader fragShader;

/* Shaderc compiler for runtime GLSL→SPIR-V compilation */
static shaderc_compiler_t g_shaderc_compiler;

/* 10. Pipelines */
static VkPipelineLayout pipelineLayout;
static VkPipeline graphicsPipeline;
static VkPipeline currentPipeline = VK_NULL_HANDLE;
static int inPass;

typedef struct {
	VkPipeline pipeline;
	VkShaderModule vertShader;
	VkShaderModule fragShader;
	VertexFormat vertexFormat;
	RasterState rasterState;
} GPUPipeline;

/* 12.2. Images */
static VkFormat depthFormat = VK_FORMAT_UNDEFINED;
static VkImage depthImage = VK_NULL_HANDLE;
static VkImageView depthImageView = VK_NULL_HANDLE;
static VmaAllocation depthAllocation = VK_NULL_HANDLE;

/* 12.5. Image Views */
static VkImageView *swapchainImageViews;

/* 14. Resource Descriptors */
#define MAX_BINDLESS_RESOURCES 16384
static VkDescriptorPool bindlessDescriptorPool;
static VkDescriptorSetLayout bindlessDescriptorSetLayout;
static VkDescriptorSet bindlessDescriptorSet;

/* UBO descriptor set (set 1) — bindless uniform buffers for transform + material data */
#define MAX_UBO_SLOTS 16
static VkDescriptorPool uboDescriptorPool;
static VkDescriptorSetLayout uboDescriptorSetLayout;
static VkDescriptorSet uboDescriptorSet;

/* Global uniform buffer for transform matrices (matches Metal's g_uniformBuffer) */
#define UNIFORM_BUFFER_SIZE 4096
static GPUBuffer *g_uniformBuffer = NULL;

/* 34.2. WSI Surface */
static VkSurfaceKHR surface;

/* 34.10. WSI Swapchain */
static int w, h;
static VkSwapchainKHR swapchain;
static uint32_t swapchainImageCount;
static VkImage *swapchainImages;
static uint32_t imageIndex;
static VkSurfaceFormatKHR swapchainSurfaceFormat = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-instances */
static void graphics_createinstance()
{
    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };

    const char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    const char **enabledLayerNames = NULL;
    uint32_t enabledLayerCount = 0;

#ifdef _DEBUG
    // Check if validation layer is available
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    
    if (layerCount > 0) {
        VkLayerProperties* availableLayers = (VkLayerProperties*)malloc(layerCount * sizeof(VkLayerProperties));
        if (availableLayers) {
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
            
            bool validationLayerFound = false;
            for (uint32_t i = 0; i < layerCount; i++) {
                if (strcmp(availableLayers[i].layerName, validationLayerName) == 0) {
                    validationLayerFound = true;
                    break;
                }
            }
            
            if (validationLayerFound) {
                enabledLayerNames = &validationLayerName;
                enabledLayerCount = 1;
                printf("Validation layer enabled\n");
            } else {
                printf("Validation layer not available\n");
            }
            
            free(availableLayers);
            availableLayers = NULL;
        }
    }
#endif

    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to initialize Vulkan loader: %d\n", result);
        exit(EXIT_FAILURE);
    }

    // Check for portability enumeration extension (required for MoltenVK).
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    if (!availableExtensions) {
        fprintf(stderr, "Failed to allocate memory for extension properties\n");
        exit(EXIT_FAILURE);
    }
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, availableExtensions);
    
    bool portabilityEnumerationAvailable = false;
    for (uint32_t i = 0; i < extensionCount; i++) {
        if (strcmp(availableExtensions[i].extensionName, "VK_KHR_portability_enumeration") == 0) {
            portabilityEnumerationAvailable = true;
            break;
        }
    }
    free(availableExtensions);
    availableExtensions = NULL;
    
    // Set up extensions list
    uint32_t enabledExtensionCount = 2;
    const char* enabledExtensions[3] = { "VK_KHR_surface" };
    
    if (portabilityEnumerationAvailable) {
        enabledExtensions[enabledExtensionCount++] = "VK_KHR_portability_enumeration";
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    enabledExtensions[1] = "VK_KHR_android_surface";
#elif defined(VK_USE_PLATFORM_METAL_EXT)
    enabledExtensions[1] = "VK_EXT_metal_surface";
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    enabledExtensions[1] = "VK_KHR_wayland_surface";
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    enabledExtensions[1] = "VK_KHR_win32_surface";
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    enabledExtensions[1] = "VK_KHR_xcb_surface";
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    enabledExtensions[1] = "VK_KHR_xlib_surface";
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
    enabledExtensions[1] = "VK_KHR_display";
#elif defined(_WIN32) || defined(WIN32)
    // Fallback for Windows when VK_USE_PLATFORM_WIN32_KHR is not defined
    enabledExtensions[1] = "VK_KHR_win32_surface";
#elif defined(__APPLE__)
    // Fallback for macOS when VK_USE_PLATFORM_METAL_EXT is not defined
    enabledExtensions[1] = "VK_EXT_metal_surface";
#else
    #error Platform not supported
#endif

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkApplicationInfo */
    app.apiVersion = VK_API_VERSION_1_2;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkInstanceCreateInfo */
    createInfo.pApplicationInfo        = &app;
    createInfo.enabledLayerCount       = enabledLayerCount;
    createInfo.ppEnabledLayerNames     = enabledLayerNames;
    createInfo.enabledExtensionCount   = enabledExtensionCount;
    createInfo.ppEnabledExtensionNames = enabledExtensions;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#vkCreateInstance */
    result = vkCreateInstance(&createInfo, NULL, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance: %d\n", result);
        exit(EXIT_FAILURE);
    }
    volkLoadInstance(instance);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-physical-device-enumeration */
static void graphics_enumeratephysicaldevices()
{
    uint32_t physicalDeviceCount;

    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
    
    // Validate device count
    if (physicalDeviceCount == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        exit(EXIT_FAILURE);
    }
    if (physicalDeviceCount > 64) { // Reasonable upper bound
        fprintf(stderr, "Too many physical devices: %u\n", physicalDeviceCount);
        exit(EXIT_FAILURE);
    }
    
    physicalDevices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
    if (!physicalDevices) {
        fprintf(stderr, "Failed to allocate memory for physical devices\n");
        exit(EXIT_FAILURE);
    }
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetPhysicalDeviceQueueFamilyProperties */
static uint32_t graphics_findqueuefamily(VkPhysicalDevice physDevice)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, NULL);
    
    if (queueFamilyCount == 0) {
        fprintf(stderr, "No queue families found\n");
        exit(EXIT_FAILURE);
    }
    
    VkQueueFamilyProperties *queueFamilies = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    if (!queueFamilies) {
        fprintf(stderr, "Failed to allocate memory for queue families\n");
        exit(EXIT_FAILURE);
    }
    
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, queueFamilies);
    
    uint32_t selectedFamily = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        // Look for a queue family that supports graphics operations
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            // Also check if it supports presentation to our surface
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physDevice, i, surface, &presentSupport);
            
            if (presentSupport) {
                selectedFamily = i;
                break;
            }
        }
    }
    
    free(queueFamilies);
    queueFamilies = NULL;
    
    if (selectedFamily == UINT32_MAX) {
        fprintf(stderr, "Failed to find suitable queue family\n");
        exit(EXIT_FAILURE);
    }
    
    return selectedFamily;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkGetPhysicalDeviceSurfaceFormatsKHR */
static VkSurfaceFormatKHR graphics_choosesurfaceformat(VkPhysicalDevice physDevice, VkSurfaceKHR surface)
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, NULL);
    
    if (formatCount == 0) {
        fprintf(stderr, "No surface formats available\n");
        exit(EXIT_FAILURE);
    }
    
    VkSurfaceFormatKHR *availableFormats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    if (!availableFormats) {
        fprintf(stderr, "Failed to allocate memory for surface formats\n");
        exit(EXIT_FAILURE);
    }
    
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &formatCount, availableFormats);
    
    // Prefer BGRA8 SRGB
    VkSurfaceFormatKHR preferredFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    
    for (uint32_t i = 0; i < formatCount; i++) {
        if (availableFormats[i].format == preferredFormat.format && 
            availableFormats[i].colorSpace == preferredFormat.colorSpace) {
            VkSurfaceFormatKHR result = availableFormats[i];
            free(availableFormats);
            availableFormats = NULL;
            return result;
        }
    }
    
    // If preferred format not available, try RGBA8
    VkSurfaceFormatKHR fallbackFormat = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (uint32_t i = 0; i < formatCount; i++) {
        if (availableFormats[i].format == fallbackFormat.format && 
            availableFormats[i].colorSpace == fallbackFormat.colorSpace) {
            VkSurfaceFormatKHR result = availableFormats[i];
            free(availableFormats);
            availableFormats = NULL;
            return result;
        }
    }
    
    // Fall back to first available format
    VkSurfaceFormatKHR result = availableFormats[0];
    free(availableFormats);
    availableFormats = NULL;
    return result;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetPhysicalDeviceFormatProperties */
static VkFormat graphics_finddepthformat(VkPhysicalDevice physDevice)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physDevice, candidates[i], &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }

    fprintf(stderr, "Failed to find supported depth format\n");
    exit(EXIT_FAILURE);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-device-creation */
static void graphics_createdevice()
{
    VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    float queuePriority = 1.0f;
    const char *enabledExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkResult result;

    /* Enable Vulkan 1.2 features for bindless rendering */
    VkPhysicalDeviceVulkan12Features vulkan12Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vulkan12Features.descriptorIndexing = VK_TRUE;
    vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12Features.runtimeDescriptorArray = VK_TRUE;
    vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;

    // Find suitable queue family first
    graphicsQueueFamily = graphics_findqueuefamily(physicalDevices[0]);

    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#VkDeviceCreateInfo */
    createInfo.queueCreateInfoCount    = 1;
    createInfo.pQueueCreateInfos       = &queueCreateInfo;
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = &enabledExtensionNames;
    createInfo.pNext                   = &vulkan12Features;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkCreateDevice */
    result = vkCreateDevice(physicalDevices[0], &createInfo, NULL, &device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan device: %d\n", result);
        exit(EXIT_FAILURE);
    }
    volkLoadDevice(device);
}

/* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#quick_start_initialization */
static void graphics_createallocator()
{
    VmaAllocatorCreateInfo allocatorCreateInfo = { 0 };
    VmaVulkanFunctions vulkanFunctions;

    /* Required when using VMA_DYNAMIC_VULKAN_FUNCTIONS. */
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    /* Required when using VMA_DYNAMIC_VULKAN_FUNCTIONS. */
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
#if VMA_DEDICATED_ALLOCATION || VMA_VULKAN_VERSION >= 1001000
    /* Fetch "vkGetBufferMemoryRequirements2" on Vulkan >= 1.1, fetch "vkGetBufferMemoryRequirements2KHR" when using VK_KHR_dedicated_allocation extension. */
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    /* Fetch "vkGetImageMemoryRequirements2" on Vulkan >= 1.1, fetch "vkGetImageMemoryRequirements2KHR" when using VK_KHR_dedicated_allocation extension. */
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
#endif
#if VMA_BIND_MEMORY2 || VMA_VULKAN_VERSION >= 1001000
    /* Fetch "vkBindBufferMemory2" on Vulkan >= 1.1, fetch "vkBindBufferMemory2KHR" when using VK_KHR_bind_memory2 extension. */
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    /* Fetch "vkBindImageMemory2" on Vulkan >= 1.1, fetch "vkBindImageMemory2KHR" when using VK_KHR_bind_memory2 extension. */
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
#endif
#if VMA_MEMORY_BUDGET || VMA_VULKAN_VERSION >= 1001000
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
#endif
#if VMA_VULKAN_VERSION >= 1003000
    /// Fetch from "vkGetDeviceBufferMemoryRequirements" on Vulkan >= 1.3, but you can also fetch it from "vkGetDeviceBufferMemoryRequirementsKHR" if you enabled extension VK_KHR_maintenance4.
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
    /// Fetch from "vkGetDeviceImageMemoryRequirements" on Vulkan >= 1.3, but you can also fetch it from "vkGetDeviceImageMemoryRequirementsKHR" if you enabled extension VK_KHR_maintenance4.
    vulkanFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;
#endif

    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorCreateInfo.physicalDevice   = physicalDevices[0];
    allocatorCreateInfo.device           = device;
    allocatorCreateInfo.instance         = instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult result = vmaCreateAllocator(&allocatorCreateInfo, &allocator);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create VMA allocator: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetDeviceQueue */
static void graphics_getqueue()
{
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &queue);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-pools */
static void graphics_createcommandpools()
{
    VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    size_t i;
    VkResult result;

    // Validate swapchain image count before allocation
    if (swapchainImageCount == 0 || swapchainImageCount > 16) {
        fprintf(stderr, "Invalid swapchain image count for command pools: %u\n", swapchainImageCount);
        exit(EXIT_FAILURE);
    }

    commandPools = (VkCommandPool *)malloc(sizeof(VkCommandPool) * swapchainImageCount);
    if (!commandPools) {
        fprintf(stderr, "Failed to allocate memory for command pools\n");
        exit(EXIT_FAILURE);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandPoolCreateInfo */
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (i = 0; i < swapchainImageCount; i++)
    {
        result = vkCreateCommandPool(device, &createInfo, NULL, &commandPools[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create command pool %zu: %d\n", i, result);
            // Clean up previously created command pools
            for (size_t j = 0; j < i; j++) {
                vkDestroyCommandPool(device, commandPools[j], NULL);
            }
            free(commandPools);
            commandPools = NULL;
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffer-allocation */
static void graphics_allocatecommandbuffers()
{
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    size_t i;
    VkResult result;

    // Validate swapchain image count before allocation
    if (swapchainImageCount == 0 || swapchainImageCount > 16) {
        fprintf(stderr, "Invalid swapchain image count for command buffers: %u\n", swapchainImageCount);
        exit(EXIT_FAILURE);
    }

    commandBuffers = (VkCommandBuffer *)malloc(sizeof(VkCommandBuffer) * swapchainImageCount);
    if (!commandBuffers) {
        fprintf(stderr, "Failed to allocate memory for command buffers\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < swapchainImageCount; i++)
    {
        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandBufferAllocateInfo */
        allocateInfo.commandPool        = commandPools[i];
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkAllocateCommandBuffers */
        result = vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffers[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to allocate command buffer %zu: %d\n", i, result);
            // Clean up previously allocated command buffers
            for (size_t j = 0; j < i; j++) {
                vkFreeCommandBuffers(device, commandPools[j], 1, &commandBuffers[j]);
            }
            free(commandBuffers);
            commandBuffers = NULL;
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-fences */
static void graphics_createfences()
{
    VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    size_t i;
    VkResult result;

    fences = (VkFence *)malloc(sizeof(VkFence) * swapchainImageCount);
    if (!fences) {
        fprintf(stderr, "Failed to allocate memory for fences\n");
        exit(EXIT_FAILURE);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#VkFenceCreateInfo */
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateFence */
    for (i = 0; i < swapchainImageCount; i++)
    {
        result = vkCreateFence(device, &createInfo, NULL, &fences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create fence %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-semaphores */
static void graphics_createsemaphores()
{
    VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkResult result;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateSemaphore */
    result = vkCreateSemaphore(device, &createInfo, NULL, &acquireSemaphore);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create acquire semaphore: %d\n", result);
        exit(EXIT_FAILURE);
    }
    result = vkCreateSemaphore(device, &createInfo, NULL, &releaseSemaphore);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create release semaphore: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets */
static void graphics_createbindlessdescriptors()
{
    VkDescriptorSetLayoutBinding binding = {};
    VkDescriptorBindingFlags bindingFlags;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize poolSize = {};
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    uint32_t variableDescriptorCount;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorCountInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkResult result;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets-setlayout */
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = MAX_BINDLESS_RESOURCES;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                   VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
                   VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    bindingFlagsInfo.bindingCount = 1;
    bindingFlagsInfo.pBindingFlags = &bindingFlags;

    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    layoutInfo.pNext = &bindingFlagsInfo;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCreateDescriptorSetLayout */
    result = vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &bindlessDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create bindless descriptor set layout: %d\n", result);
        exit(EXIT_FAILURE);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets-pools */
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_BINDLESS_RESOURCES;

    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCreateDescriptorPool */
    result = vkCreateDescriptorPool(device, &poolInfo, NULL, &bindlessDescriptorPool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create bindless descriptor pool: %d\n", result);
        exit(EXIT_FAILURE);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets-allocation */
    variableDescriptorCount = MAX_BINDLESS_RESOURCES;

    variableDescriptorCountInfo.descriptorSetCount = 1;
    variableDescriptorCountInfo.pDescriptorCounts = &variableDescriptorCount;

    allocInfo.descriptorPool = bindlessDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &bindlessDescriptorSetLayout;
    allocInfo.pNext = &variableDescriptorCountInfo;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkAllocateDescriptorSets */
    result = vkAllocateDescriptorSets(device, &allocInfo, &bindlessDescriptorSet);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate bindless descriptor set: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

/* Create UBO descriptor set (set 1) with MAX_UBO_SLOTS uniform buffer descriptors.
 * Each slot can be bound to a VkBuffer via vkUpdateDescriptorSets.
 * Used for transforms (slot 0), material data (slot 1), etc. */
static void graphics_createubodescriptors()
{
    VkDescriptorSetLayoutBinding binding = {};
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize poolSize = {};
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkResult result;

    /* One binding with MAX_UBO_SLOTS descriptors for uniform buffers */
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = MAX_UBO_SLOTS;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    result = vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &uboDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create UBO descriptor set layout: %d\n", result);
        exit(EXIT_FAILURE);
    }

    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_UBO_SLOTS;

    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    result = vkCreateDescriptorPool(device, &poolInfo, NULL, &uboDescriptorPool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create UBO descriptor pool: %d\n", result);
        exit(EXIT_FAILURE);
    }

    allocInfo.descriptorPool = uboDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uboDescriptorSetLayout;

    result = vkAllocateDescriptorSets(device, &allocInfo, &uboDescriptorSet);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to allocate UBO descriptor set: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-creation */
static void graphics_createrenderpass()
{
    VkRenderPassCreateInfo  createInfo     = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkAttachmentDescription attachments[2] = { {0}, {0} };
    VkSubpassDescription    subpass        = { 0 };
    VkAttachmentReference   colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference   depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDependency     dependencies[2] = { {0}, {0} };

    /* Color attachment (index 0) */
    attachments[0].format            = swapchainSurfaceFormat.format;
    attachments[0].samples           = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp           = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp     = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    /* Depth attachment (index 1) */
    attachments[1].format            = depthFormat;
    attachments[1].samples           = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp     = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout       = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    subpass.pipelineBindPoint         = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount      = 1;
    subpass.pColorAttachments         = &colorReference;
    subpass.pDepthStencilAttachment   = &depthReference;

    /* Dependency for color attachment */
    dependencies[0].srcSubpass        = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass        = 0;
    dependencies[0].srcStageMask      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask     = 0;
    dependencies[0].dstAccessMask     = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    /* Dependency for depth attachment */
    dependencies[1].srcSubpass        = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass        = 0;
    dependencies[1].srcStageMask      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask      = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask     = 0;
    dependencies[1].dstAccessMask     = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    createInfo.attachmentCount   = 2;
    createInfo.pAttachments      = attachments;
    createInfo.subpassCount      = 1;
    createInfo.pSubpasses        = &subpass;
    createInfo.dependencyCount   = 2;
    createInfo.pDependencies     = dependencies;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkCreateRenderPass */
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, NULL);
        renderPass = VK_NULL_HANDLE;
    }

    VkResult result = vkCreateRenderPass(device, &createInfo, NULL, &renderPass);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create render pass: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#_framebuffers */
static void graphics_createframebuffers()
{
    VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    size_t i;
    VkResult result;

    framebuffers = (VkFramebuffer *)malloc(sizeof(VkFramebuffer) * swapchainImageCount);
    if (!framebuffers) {
        fprintf(stderr, "Failed to allocate memory for framebuffers\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < swapchainImageCount; i++)
    {
        VkImageView attachments[] = { swapchainImageViews[i], depthImageView };

        createInfo.renderPass      = renderPass;
        createInfo.attachmentCount = 2;
        createInfo.pAttachments    = attachments;
        createInfo.width           = w;
        createInfo.height          = h;
        createInfo.layers          = 1;

        result = vkCreateFramebuffer(device, &createInfo, NULL, &framebuffers[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create framebuffer %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#shader-modules */
static void graphics_createshaders()
{
    char   *vertSource;
    size_t  vertSize;
    char   *fragSource;
    size_t  fragSize;

    vertSize   = filesystem_fileread((void **)&vertSource, "shaders/triangle.vert");
    fragSize   = filesystem_fileread((void **)&fragSource, "shaders/triangle.frag");
    vertShader = graphics_createshader(SHADER_STAGE_VERTEX, vertSource, vertSize, NULL, 0);
    fragShader = graphics_createshader(SHADER_STAGE_FRAGMENT, fragSource, fragSize, NULL, 0);
    free(fragSource);
    fragSource = NULL;
    free(vertSource);
    vertSource = NULL;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html#pipelines-graphics */
static void graphics_creategraphicspipeline()
{
    VkGraphicsPipelineCreateInfo                  createInfo               = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo               vertShaderStage          = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo               fragShaderStage          = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo               stages[2];
    VkPipelineVertexInputStateCreateInfo          vertexInput              = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo        inputAssembly            = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo             viewport                 = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo        rasterization            = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo          multisample              = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendStateCreateInfo           colorBlend               = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState           colorBlendAttachment     = { 0 };
    VkPipelineDynamicStateCreateInfo              dynamicState             = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    const VkDynamicState                          states[]                 = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineLayoutCreateInfo                    pipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[5] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, tangent);
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = offsetof(Vertex, bitangent);
    attributeDescriptions[4].binding = 0;
    attributeDescriptions[4].location = 4;
    attributeDescriptions[4].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[4].offset = offsetof(Vertex, texCoords);

    vertShaderStage.stage                       = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStage.module                      = (VkShaderModule)vertShader;
    vertShaderStage.pName                       = "main";

    fragShaderStage.stage                       = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStage.module                      = (VkShaderModule)fragShader;
    fragShaderStage.pName                       = "main";

    stages[0]                                   = vertShaderStage;
    stages[1]                                   = fragShaderStage;

    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions    = attributeDescriptions;

    inputAssembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount                      = 1;
    viewport.scissorCount                       = 1;

    rasterization.cullMode                      = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace                     = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth                     = 1.0f;

    multisample.rasterizationSamples            = VK_SAMPLE_COUNT_1_BIT;

    colorBlendAttachment.colorWriteMask         = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable            = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor    = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp           = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor    = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp           = VK_BLEND_OP_ADD;

    colorBlend.attachmentCount                  = 1;
    colorBlend.pAttachments                     = &colorBlendAttachment;

    dynamicState.dynamicStateCount              = 2;
    dynamicState.pDynamicStates                 = states;

    VkDescriptorSetLayout setLayouts[2] = { bindlessDescriptorSetLayout, uboDescriptorSetLayout };

    pipelineLayoutCreateInfo.setLayoutCount = 2;
    pipelineLayoutCreateInfo.pSetLayouts = setLayouts;

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    }

    VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline layout: %d\n", result);
        exit(EXIT_FAILURE);
    }

    createInfo.stageCount                       = 2;
    createInfo.pStages                          = stages;
    createInfo.pVertexInputState                = &vertexInput;
    createInfo.pInputAssemblyState              = &inputAssembly;
    createInfo.pViewportState                   = &viewport;
    createInfo.pRasterizationState              = &rasterization;
    createInfo.pMultisampleState                = &multisample;
    createInfo.pColorBlendState                 = &colorBlend;
    createInfo.pDynamicState                    = &dynamicState;
    createInfo.layout                           = pipelineLayout;
    createInfo.renderPass                       = renderPass;

    if (graphicsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, graphicsPipeline, NULL);
        graphicsPipeline = VK_NULL_HANDLE;
    }

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create graphics pipeline: %d\n", result);
        exit(EXIT_FAILURE);
    }

    graphics_destroyshader(vertShader);
    graphics_destroyshader(fragShader);
    vertShader = VK_NULL_HANDLE;
    fragShader = VK_NULL_HANDLE;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_surface */
static void graphics_createsurface()
{
    if (!window_vulkan_createsurface(instance, &surface)) {
        fprintf(stderr, "Failed to create Vulkan surface\n");
        exit(EXIT_FAILURE);
    }
}

static void graphics_destroyframebuffers();
static void graphics_destroyimageviews();
static void graphics_destroydepthresources();
static void graphics_destroyfences();
static void graphics_freecommandbuffers();
static void graphics_destroycommandpools();
static void graphics_destroysemaphores();

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_swapchain */
static void graphics_createswapchain()
{
    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkExtent2D imageExtent;
    VkSwapchainKHR oldSwapchain;

    window_getwindowsizeinpixels(&w, &h);

    // Select surface format if not already done
    if (swapchainSurfaceFormat.format == VK_FORMAT_UNDEFINED) {
        swapchainSurfaceFormat = graphics_choosesurfaceformat(physicalDevices[0], surface);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#VkSwapchainCreateInfoKHR */
    imageExtent.width  = w;
    imageExtent.height = h;

    oldSwapchain = swapchain;

    createInfo.surface          = surface;
    createInfo.minImageCount    = MIN_SWAPCHAIN_IMAGES;
    createInfo.imageFormat      = swapchainSurfaceFormat.format;
    createInfo.imageColorSpace  = swapchainSurfaceFormat.colorSpace;
    createInfo.imageExtent      = imageExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode      = VK_PRESENT_MODE_IMMEDIATE_KHR;
    createInfo.clipped          = VK_TRUE;
    createInfo.oldSwapchain     = oldSwapchain;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkCreateSwapchainKHR */
    VkResult result = vkCreateSwapchainKHR(device, &createInfo, NULL, &swapchain);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create swapchain: %d\n", result);
        exit(EXIT_FAILURE);
    }

    if (oldSwapchain != VK_NULL_HANDLE)
    {
        graphics_destroyimageviews();
        graphics_destroyfences();
        graphics_freecommandbuffers();
        graphics_destroycommandpools();
        graphics_destroysemaphores();

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkDestroySwapchainKHR */
        vkDestroySwapchainKHR(device, oldSwapchain, NULL);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkGetSwapchainImagesKHR */
static void graphics_getswapchainimages()
{
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, NULL);
    
    // Validate swapchain image count
    if (swapchainImageCount == 0) {
        fprintf(stderr, "No swapchain images available\n");
        exit(EXIT_FAILURE);
    }
    if (swapchainImageCount > 16) {
        fprintf(stderr, "Too many swapchain images: %u\n", swapchainImageCount);
        exit(EXIT_FAILURE);
    }
    
    swapchainImages = (VkImage *)malloc(sizeof(VkImage) * swapchainImageCount);
    if (!swapchainImages) {
        fprintf(stderr, "Failed to allocate memory for swapchain images\n");
        exit(EXIT_FAILURE);
    }
    
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-images */
static void graphics_createdepthresources()
{
    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkResult result;

    /* Select depth format once */
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = graphics_finddepthformat(physicalDevices[0]);
    }

    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)w;
    imageInfo.extent.height = (uint32_t)h;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = 0;

    result = vmaCreateImage(allocator, &imageInfo, &allocInfo, &depthImage, &depthAllocation, NULL);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image: %d\n", result);
        exit(EXIT_FAILURE);
    }

    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device, &viewInfo, NULL, &depthImageView);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create depth image view: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

static void graphics_destroydepthresources()
{
    vkQueueWaitIdle(queue);

    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, NULL);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
        depthAllocation = VK_NULL_HANDLE;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-image-views */
static void graphics_createimageviews()
{
    VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    size_t i;
    VkResult result;

    createInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format                      = swapchainSurfaceFormat.format;
    createInfo.components.r                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.layerCount = 1;

    swapchainImageViews = (VkImageView *)malloc(sizeof(VkImageView) * swapchainImageCount);
    if (!swapchainImageViews) {
        fprintf(stderr, "Failed to allocate memory for swapchain image views\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < swapchainImageCount; i++)
    {
        createInfo.image = swapchainImages[i];

        result = vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create image view %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkDestroyFramebuffer */
static void graphics_destroyframebuffers()
{
    size_t i;

    vkQueueWaitIdle(queue);

    if (framebuffers) {
        for (i = swapchainImageCount; i-- > 0;)
        {
            vkDestroyFramebuffer(device, framebuffers[i], NULL);
        }
        free(framebuffers);
        framebuffers = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#vkDestroyImageView */
static void graphics_destroyimageviews()
{
    size_t i;

    if (swapchainImageViews) {
        for (i = swapchainImageCount; i-- > 0;)
        {
            vkDestroyImageView(device, swapchainImageViews[i], NULL);
        }
        free(swapchainImageViews);
        swapchainImageViews = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkDestroyFence */
static void graphics_destroyfences()
{
    size_t i;

    if (fences) {
        for (i = swapchainImageCount; i-- > 0;)
        {
            vkDestroyFence(device, fences[i], NULL);
        }
        free(fences);
        fences = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkFreeCommandBuffers */
static void graphics_freecommandbuffers()
{
    size_t i;

    if (commandBuffers) {
        for (i = swapchainImageCount; i-- > 0;)
        {
            vkFreeCommandBuffers(device, commandPools[i], 1, &commandBuffers[i]);
        }
        free(commandBuffers);
        commandBuffers = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkDestroyCommandPool */
static void graphics_destroycommandpools()
{
    size_t i;

    if (commandPools) {
        for (i = swapchainImageCount; i-- > 0;)
        {
            vkDestroyCommandPool(device, commandPools[i], NULL);
        }
        free(commandPools);
        commandPools = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkDestroySemaphore */
static void graphics_destroysemaphores()
{
    if (releaseSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, releaseSemaphore, NULL);
        releaseSemaphore = VK_NULL_HANDLE;
    }
    if (acquireSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, acquireSemaphore, NULL);
        acquireSemaphore = VK_NULL_HANDLE;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkAcquireNextImageKHR */
static VkResult graphics_acquirenextimage()
{
    VkResult res;

    res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res != VK_SUCCESS)
    {
        return res;
    }
    if (fences[imageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(device, 1, &fences[imageIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fences[imageIndex]);
    }
    if (commandPools[imageIndex] != VK_NULL_HANDLE)
    {
        vkResetCommandPool(device, commandPools[imageIndex], 0);
    }
    return VK_SUCCESS;
}

void graphics_init()
{
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_createinstance();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_enumeratephysicaldevices();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createsurface();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_createdevice();
    /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html */
    graphics_createallocator();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_getqueue();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html */
    graphics_createsemaphores();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html */
    graphics_createbindlessdescriptors();
    /* UBO descriptor set for uniform buffers */
    graphics_createubodescriptors();
    /* Create global uniform buffer for transform matrices */
    g_uniformBuffer = (GPUBuffer *)graphics_createuniformbuffer(UNIFORM_BUFFER_SIZE);
    /* Shaderc compiler for runtime GLSL→SPIR-V compilation */
    g_shaderc_compiler = shaderc_compiler_initialize();
    if (!g_shaderc_compiler) {
        fprintf(stderr, "Failed to initialize shaderc compiler\n");
        exit(EXIT_FAILURE);
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html */
    graphics_createshaders();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createswapchain();
    graphics_getswapchainimages();
    /* Create depth image resources */
    graphics_createdepthresources();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createrenderpass();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html */
    graphics_creategraphicspipeline();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html */
    graphics_createcommandpools();
    graphics_allocatecommandbuffers();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html */
    graphics_createfences();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html */
    graphics_createimageviews();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createframebuffers();
    atexit(graphics_shutdown);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#vkCreateShaderModule */
Shader graphics_createshader(ShaderStage stage, const char *source, size_t size,
                             const char **defines, size_t defineCount)
{
    /* Map ShaderStage to shaderc_shader_kind */
    shaderc_shader_kind kind;
    switch (stage) {
        case SHADER_STAGE_VERTEX:
            kind = shaderc_glsl_vertex_shader;
            break;
        case SHADER_STAGE_FRAGMENT:
            kind = shaderc_glsl_fragment_shader;
            break;
        default:
            fprintf(stderr, "Unknown shader stage: %d\n", stage);
            exit(EXIT_FAILURE);
    }

    /* Set up compilation options */
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!options) {
        fprintf(stderr, "Failed to create shaderc compile options\n");
        exit(EXIT_FAILURE);
    }

    shaderc_compile_options_set_target_env(
        options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    shaderc_compile_options_set_optimization_level(
        options, shaderc_optimization_level_performance);

    /* Add macro definitions */
    for (size_t i = 0; i < defineCount; i++) {
        const char *def = defines[i];
        if (!def) continue;

        /* Split "NAME=VALUE" at the first '=' */
        const char *eq = strchr(def, '=');
        if (eq) {
            size_t nameLen = (size_t)(eq - def);
            shaderc_compile_options_add_macro_definition(
                options, def, nameLen, eq + 1, strlen(eq + 1));
        } else {
            shaderc_compile_options_add_macro_definition(
                options, def, strlen(def), NULL, 0);
        }
    }

    /* Compile GLSL to SPIR-V */
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        g_shaderc_compiler, source, size, kind, "shader", "main", options);

    shaderc_compile_options_release(options);

    if (shaderc_result_get_compilation_status(result) !=
        shaderc_compilation_status_success)
    {
        fprintf(stderr, "Shader compilation failed:\n%s\n",
                shaderc_result_get_error_message(result));
        shaderc_result_release(result);
        exit(EXIT_FAILURE);
    }

    /* Create VkShaderModule from compiled SPIR-V */
    VkShaderModule shaderModule;
    VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = shaderc_result_get_length(result);
    createInfo.pCode    = (const uint32_t *)shaderc_result_get_bytes(result);

    VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);
    if (vkResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module: %d\n", vkResult);
        shaderc_result_release(result);
        exit(EXIT_FAILURE);
    }

    shaderc_result_release(result);
    return shaderModule;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#vkDestroyShaderModule */
void graphics_destroyshader(Shader shader)
{
    vkDestroyShaderModule(device, (VkShaderModule)shader, NULL);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
int graphics_isminimized()
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevices[0], surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width  == 0 &&
        surfaceCapabilities.currentExtent.height == 0)
    {
        return 1;
    }

    return 0;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-recording */
void graphics_predraw()
{
    /* 6.4. Command Buffer Recording */
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    /* 8.4. Render Pass Commands */
    VkRenderPassBeginInfo renderPassBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    /* 19.3. Clear Values */
    VkClearValue clearValues[2];
    clearValues[0].color.float32[0] = CLEAR_COLOR[0];
    clearValues[0].color.float32[1] = CLEAR_COLOR[1];
    clearValues[0].color.float32[2] = CLEAR_COLOR[2];
    clearValues[0].color.float32[3] = CLEAR_COLOR[3];
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    /* 27.9. Controlling the Viewport */
    VkViewport viewport = { 0 };

    /* 29.2. Scissor Test */
    VkRect2D scissor = { 0 };

    /* 34.10. WSI Swapchain */
    VkResult res;

    if (graphics_isminimized())
    {
        return;
    }

    res = graphics_acquirenextimage();

    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        graphics_resize();
        res = graphics_acquirenextimage();
    }

    if (res != VK_SUCCESS)
    {
        vkQueueWaitIdle(queue);
        return;
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-recording */
    vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

    renderPassBegin.renderPass               = renderPass;
    renderPassBegin.framebuffer              = framebuffers[imageIndex];
    renderPassBegin.renderArea.extent.width  = w;
    renderPassBegin.renderArea.extent.height = h;
    renderPassBegin.clearValueCount          = 2;
    renderPassBegin.pClearValues             = clearValues;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-commands */
    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCmdBindDescriptorSets */
    vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 0, 1, &bindlessDescriptorSet, 0, NULL);

    /* Bind UBO descriptor set at set 1 */
    vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 1, 1, &uboDescriptorSet, 0, NULL);

    viewport.width    = w;
    viewport.height   = h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap27.html#vertexpostproc-viewport */
    vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &viewport);

    scissor.extent.width  = w;
    scissor.extent.height = h;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap29.html#fragops-scissor */
    vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &scissor);

    inPass = 1;
}

typedef struct {
    VkBuffer *vertexBuffers;
    VkBuffer *indexBuffers;
    VmaAllocation *vertexAllocations;
    VmaAllocation *indexAllocations;
    uint32_t *indexCounts;
    uint32_t meshCount;
} GPUModel;

#define MAX_MATERIAL_FLOATS    32
#define MAX_MATERIAL_VEC3S     16
#define MAX_MATERIAL_TEXTURES   8

typedef struct {
    char name[64];
    float value;
} MaterialFloat;

typedef struct {
    char name[64];
    float value[3];
} MaterialVec3;

typedef struct {
    char name[64];
    Texture texture;
} MaterialTexture;

typedef struct {
    Shader shader;
    MaterialFloat floats[MAX_MATERIAL_FLOATS];
    MaterialVec3 vec3s[MAX_MATERIAL_VEC3S];
    MaterialTexture textures[MAX_MATERIAL_TEXTURES];
    size_t floatCount;
    size_t vec3Count;
    size_t textureCount;
    float mat4[16];
    int hasMat4;
    Buffer uniformBuffer;  /* GPU buffer for packed material data */
    int dirty;
} GPUMaterial;

typedef struct {
    VkBuffer buffer;
    VmaAllocation allocation;
    size_t size;
} GPUBuffer;

typedef struct {
    VkImage image;
    VkImageView view;
    VkSampler sampler;
    VmaAllocation allocation;
    int width;
    int height;
} GPUTexture;

typedef struct {
    RasterState state;
} GPURenderPass;

static GPUMaterial *currentMaterial;

static void graphics_copy_name(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static VkCommandPool graphics_get_upload_pool()
{
    if (!commandPools || swapchainImageCount == 0) {
        return VK_NULL_HANDLE;
    }

    return commandPools[0];
}

static VkCommandBuffer graphics_begin_one_time_commands()
{
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandPool pool = graphics_get_upload_pool();

    if (pool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    allocateInfo.commandPool = pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

static void graphics_end_one_time_commands(VkCommandBuffer commandBuffer)
{
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandPool pool = graphics_get_upload_pool();

    if (commandBuffer == VK_NULL_HANDLE || pool == VK_NULL_HANDLE) {
        return;
    }

    vkEndCommandBuffer(commandBuffer);

    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
}

static void graphics_transition_image(VkCommandBuffer commandBuffer,
                                      VkImage image,
                                      VkImageLayout oldLayout,
                                      VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        /* Uploading a brand-new texture: no previous contents to worry about. */
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        /* Upload complete — make texture visible to shaders. */
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        /* Updating a live texture — flush shader reads before transfer write. */
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        /* First use of a color attachment — no prior contents. */
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        /* Preparing a color image for presentation. */
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        /* First use of a depth/stencil attachment. */
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        /* Transitioning from render target to upload target (e.g. mipmap generation). */
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        /* Upload complete — make texture usable as a color attachment. */
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        /* Initial layout for an image that will only be read by shaders. */
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer,
                         srcStage, dstStage,
                         0, 0, NULL, 0, NULL, 1, &barrier);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-buffers */
static GPUModel *graphics_createmodelgpu(Model *modelData)
{
    if (!modelData) {
        return NULL;
    }

    GPUModel *gpuModel = (GPUModel *)malloc(sizeof(GPUModel));
    if (!gpuModel) {
        fprintf(stderr, "Failed to allocate graphics model\n");
        return NULL;
    }

    gpuModel->meshCount = modelData->meshCount;

    gpuModel->vertexBuffers     = (VkBuffer *)     malloc(sizeof(VkBuffer)      * gpuModel->meshCount);
    gpuModel->indexBuffers      = (VkBuffer *)     malloc(sizeof(VkBuffer)      * gpuModel->meshCount);
    gpuModel->vertexAllocations = (VmaAllocation *)malloc(sizeof(VmaAllocation) * gpuModel->meshCount);
    gpuModel->indexAllocations  = (VmaAllocation *)malloc(sizeof(VmaAllocation) * gpuModel->meshCount);
    gpuModel->indexCounts       = (uint32_t *)     malloc(sizeof(uint32_t)      * gpuModel->meshCount);

    if (!gpuModel->vertexBuffers || !gpuModel->indexBuffers || 
        !gpuModel->vertexAllocations || !gpuModel->indexAllocations || 
        !gpuModel->indexCounts) {
        fprintf(stderr, "Failed to allocate buffer arrays\n");
        free(gpuModel->vertexBuffers);
        free(gpuModel->indexBuffers);
        free(gpuModel->vertexAllocations);
        free(gpuModel->indexAllocations);
        free(gpuModel->indexCounts);
        free(gpuModel);
        return NULL;
    }

    for (uint32_t i = 0; i < gpuModel->meshCount; i++) {
        Mesh *mesh = &modelData->meshes[i];
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        VmaAllocationCreateInfo allocInfo = { 0 };
        VkResult result;

        bufferInfo.size        = mesh->vertexCount * sizeof(Vertex);
        bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#gac72ee55598617e8eecca384e746bab51 */
        result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, 
                                &gpuModel->vertexBuffers[i], &gpuModel->vertexAllocations[i], NULL);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create vertex buffer for mesh %d: %d\n", i, result);
            goto cleanup;
        }

        void *mappedData;
        result = vmaMapMemory(allocator, gpuModel->vertexAllocations[i], &mappedData);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to map vertex buffer for mesh %d: %d\n", i, result);
            goto cleanup;
        }
        memcpy(mappedData, mesh->vertices, bufferInfo.size);
        vmaUnmapMemory(allocator, gpuModel->vertexAllocations[i]);

        bufferInfo.size  = mesh->indexCount * sizeof(uint32_t);
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#gac72ee55598617e8eecca384e746bab51 */
        result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, 
                                &gpuModel->indexBuffers[i], &gpuModel->indexAllocations[i], NULL);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create index buffer for mesh %d: %d\n", i, result);
            goto cleanup;
        }

        result = vmaMapMemory(allocator, gpuModel->indexAllocations[i], &mappedData);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to map index buffer for mesh %d: %d\n", i, result);
            goto cleanup;
        }
        memcpy(mappedData, mesh->indices, bufferInfo.size);
        vmaUnmapMemory(allocator, gpuModel->indexAllocations[i]);

        gpuModel->indexCounts[i] = mesh->indexCount;
    }

    model_destroy(modelData);
    return gpuModel;

cleanup:
    for (uint32_t j = 0; j < gpuModel->meshCount; j++) {
        if (gpuModel->vertexBuffers[j] != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuModel->vertexBuffers[j], gpuModel->vertexAllocations[j]);
        }
        if (gpuModel->indexBuffers[j] != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuModel->indexBuffers[j], gpuModel->indexAllocations[j]);
        }
    }
    free(gpuModel->indexCounts);
    free(gpuModel->vertexBuffers);
    free(gpuModel->indexBuffers);
    free(gpuModel->vertexAllocations);
    free(gpuModel->indexAllocations);
    free(gpuModel);
    return NULL;
}

/* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#ga0386f883b5c5a093975b6303569735aa */
static void graphics_destroymodelgpu(GPUModel *gpuModel)
{
    if (!gpuModel) {
        return;
    }

    vkQueueWaitIdle(queue);

    for (uint32_t i = 0; i < gpuModel->meshCount; i++) {
        if (gpuModel->vertexBuffers[i] != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuModel->vertexBuffers[i], gpuModel->vertexAllocations[i]);
        }
        if (gpuModel->indexBuffers[i] != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, gpuModel->indexBuffers[i], gpuModel->indexAllocations[i]);
        }
    }

    free(gpuModel->indexCounts);
    free(gpuModel->vertexBuffers);
    free(gpuModel->indexBuffers);
    free(gpuModel->vertexAllocations);
    free(gpuModel->indexAllocations);
    free(gpuModel);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html */
Model *graphics_loadmodel(const char *filepath)
{
    if (!filepath) {
        fprintf(stderr, "Invalid model filepath\n");
        return NULL;
    }

    Model *modelData = model_load(filepath);
    if (!modelData) {
        fprintf(stderr, "Failed to load model: %s\n", filepath);
        return NULL;
    }

    GPUModel *gpuModel = graphics_createmodelgpu(modelData);
    if (!gpuModel) {
        fprintf(stderr, "Failed to create GPU buffers for model: %s\n", filepath);
        model_destroy(modelData);
        return NULL;
    }

    return (Model *)gpuModel;
}

/* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#ga0386f883b5c5a093975b6303569735aa */
void graphics_destroymodel(Model *model)
{
    if (!model) {
        return;
    }

    graphics_destroymodelgpu((GPUModel *)model);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#drawing */
void graphics_drawmodel(Model *model, Material mat, const float *transform4x4)
{
    if (mat) {
        graphics_setmaterial(mat);
    }

    if (!model || graphics_isminimized()) {
        return;
    }

    GPUModel *gpuModel = (GPUModel *)model;
    if (gpuModel->meshCount == 0) {
        return;
    }

    /* Upload transform matrix to global uniform buffer and bind at slot 0 */
    if (transform4x4 && g_uniformBuffer) {
        graphics_updatebuffer((Buffer)g_uniformBuffer, transform4x4, 64);
        graphics_binduniformbuffer((Buffer)g_uniformBuffer, 0);
    }

    // Bind default 3D pipeline if no custom pipeline is bound
    if (currentPipeline != graphicsPipeline) {
        vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        currentPipeline = graphicsPipeline;
    }

    VkDeviceSize offsets[] = {0};

    for (uint32_t i = 0; i < gpuModel->meshCount; i++) {
        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap22.html#vkCmdBindVertexBuffers */
        vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &gpuModel->vertexBuffers[i], offsets);

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdBindIndexBuffer */
        vkCmdBindIndexBuffer(commandBuffers[imageIndex], gpuModel->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdDrawIndexed */
        vkCmdDrawIndexed(commandBuffers[imageIndex], gpuModel->indexCounts[i], 1, 0, 0, 0);
    }
}

void graphics_draw_instanced(Model *model,
                             Material mat,
                             const float *transforms4x4,
                             size_t count)
{
    if (mat) {
        graphics_setmaterial(mat);
    }

    if (!model || graphics_isminimized()) {
        return;
    }

    GPUModel *gpuModel = (GPUModel *)model;
    if (gpuModel->meshCount == 0) {
        return;
    }

    /* Upload first transform to global uniform buffer and bind at slot 0 */
    if (transforms4x4 && g_uniformBuffer) {
        graphics_updatebuffer((Buffer)g_uniformBuffer, transforms4x4, 64);
        graphics_binduniformbuffer((Buffer)g_uniformBuffer, 0);
    }

    // Bind default 3D pipeline if no custom pipeline is bound
    if (currentPipeline != graphicsPipeline) {
        vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        currentPipeline = graphicsPipeline;
    }

    VkDeviceSize offsets[] = {0};
    uint32_t instanceCount = count == 0 ? 1 : (uint32_t)count;

    for (uint32_t i = 0; i < gpuModel->meshCount; i++) {
        vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &gpuModel->vertexBuffers[i], offsets);
        vkCmdBindIndexBuffer(commandBuffers[imageIndex], gpuModel->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffers[imageIndex], gpuModel->indexCounts[i], instanceCount, 0, 0, 0);
    }
}

void graphics_draw_buffers(Buffer vertexBuffer,
                           Buffer indexBuffer,
                           size_t indexCount,
                           Material mat,
                           const float *transform4x4)
{
    GPUBuffer *vertex = (GPUBuffer *)vertexBuffer;
    GPUBuffer *index = (GPUBuffer *)indexBuffer;
    VkDeviceSize offsets[] = {0};

    if (mat) {
        graphics_setmaterial(mat);
    }
    if (!vertex || !index || indexCount == 0 || graphics_isminimized()) {
        return;
    }

    /* Upload transform matrix to global uniform buffer and bind at slot 0 */
    if (transform4x4 && g_uniformBuffer) {
        graphics_updatebuffer((Buffer)g_uniformBuffer, transform4x4, 64);
        graphics_binduniformbuffer((Buffer)g_uniformBuffer, 0);
    }

    vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &vertex->buffer, offsets);
    vkCmdBindIndexBuffer(commandBuffers[imageIndex], index->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffers[imageIndex], (uint32_t)indexCount, 1, 0, 0, 0);
}

Material graphics_creatematerial(Shader shader)
{
    GPUMaterial *material = (GPUMaterial *)calloc(1, sizeof(GPUMaterial));
    if (!material) {
        return NULL;
    }

    material->shader = shader;
    material->uniformBuffer = graphics_createuniformbuffer(1024);
    material->dirty = 1;
    return material;
}

void graphics_destroymaterial(Material mat)
{
    if (!mat) {
        return;
    }

    GPUMaterial *m = (GPUMaterial *)mat;
    if (m->uniformBuffer) graphics_destroybuffer(m->uniformBuffer);
    free(mat);
}

void graphics_material_set_texture(Material mat, const char *name, Texture tex)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    if (!material || !name || !tex) {
        return;
    }

    for (size_t i = 0; i < material->textureCount; i++) {
        if (strcmp(material->textures[i].name, name) == 0) {
            material->textures[i].texture = tex;
            return;
        }
    }

    if (material->textureCount < MAX_MATERIAL_TEXTURES) {
        MaterialTexture *entry = &material->textures[material->textureCount++];
        graphics_copy_name(entry->name, sizeof(entry->name), name);
        entry->texture = tex;
    }
}

void graphics_material_set_float(Material mat, const char *name, float value)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    if (!material || !name) {
        return;
    }

    for (size_t i = 0; i < material->floatCount; i++) {
        if (strcmp(material->floats[i].name, name) == 0) {
            material->floats[i].value = value;
            material->dirty = 1;
            return;
        }
    }

    if (material->floatCount < MAX_MATERIAL_FLOATS) {
        MaterialFloat *entry = &material->floats[material->floatCount++];
        graphics_copy_name(entry->name, sizeof(entry->name), name);
        entry->value = value;
        material->dirty = 1;
    }
}

void graphics_material_set_vec3(Material mat, const char *name,
                                float x, float y, float z)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    if (!material || !name) {
        return;
    }

    for (size_t i = 0; i < material->vec3Count; i++) {
        if (strcmp(material->vec3s[i].name, name) == 0) {
            material->vec3s[i].value[0] = x;
            material->vec3s[i].value[1] = y;
            material->vec3s[i].value[2] = z;
            material->dirty = 1;
            return;
        }
    }

    if (material->vec3Count < MAX_MATERIAL_VEC3S) {
        MaterialVec3 *entry = &material->vec3s[material->vec3Count++];
        graphics_copy_name(entry->name, sizeof(entry->name), name);
        entry->value[0] = x;
        entry->value[1] = y;
        entry->value[2] = z;
        material->dirty = 1;
    }
}

void graphics_material_set_mat4(Material mat, const float *matrix4x4)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    if (!material || !matrix4x4) {
        return;
    }

    memcpy(material->mat4, matrix4x4, sizeof(material->mat4));
    material->hasMat4 = 1;
    material->dirty = 1;
}

/* Pack material uniforms into the GPU buffer.
 * Layout: [floats (tight)] [vec3s (padded to vec4)] [mat4]
 * Same layout as Metal backend's metal_material_pack(). */
static void vulkan_material_pack(GPUMaterial *m) {
    if (!m || !m->dirty || !m->uniformBuffer) return;

    GPUBuffer *ub = (GPUBuffer *)m->uniformBuffer;
    void *mappedData;
    VkResult result = vmaMapMemory(allocator, ub->allocation, &mappedData);
    if (result != VK_SUCCESS) return;

    float *dst = (float *)mappedData;
    size_t offset = 0;

    /* Write floats — tightly packed */
    for (size_t i = 0; i < m->floatCount && i < MAX_MATERIAL_FLOATS; i++) {
        dst[offset++] = m->floats[i].value;
    }
    for (size_t i = m->floatCount; i < MAX_MATERIAL_FLOATS; i++) {
        dst[offset++] = 0.0f;
    }

    /* Write vec3s — each padded to vec4 */
    for (size_t i = 0; i < m->vec3Count && i < MAX_MATERIAL_VEC3S; i++) {
        dst[offset++] = m->vec3s[i].value[0];
        dst[offset++] = m->vec3s[i].value[1];
        dst[offset++] = m->vec3s[i].value[2];
        dst[offset++] = 0.0f;
    }
    for (size_t i = m->vec3Count; i < MAX_MATERIAL_VEC3S; i++) {
        dst[offset++] = 0.0f;
        dst[offset++] = 0.0f;
        dst[offset++] = 0.0f;
        dst[offset++] = 0.0f;
    }

    /* Write mat4 if set */
    if (m->hasMat4) {
        memcpy(&dst[offset], m->mat4, 16 * sizeof(float));
    } else {
        memset(&dst[offset], 0, 16 * sizeof(float));
    }

    vmaUnmapMemory(allocator, ub->allocation);
    m->dirty = 0;
}

void graphics_setmaterial(Material mat)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    currentMaterial = material;

    if (!material) {
        return;
    }

    /* Pack dirty uniforms into GPU buffer and bind at UBO slot 1 */
    vulkan_material_pack(material);
    if (material->uniformBuffer) {
        graphics_binduniformbuffer(material->uniformBuffer, 1);
    }

    /* Bind textures */
    for (size_t i = 0; i < material->textureCount; i++) {
        graphics_bindtexture(material->textures[i].texture, (unsigned)i);
    }
}

Buffer graphics_createvertexbuffer(const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)calloc(1, sizeof(GPUBuffer));
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VkResult result;

    if (!buffer || size == 0) {
        free(buffer);
        return NULL;
    }

    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer->buffer, &buffer->allocation, NULL);
    if (result != VK_SUCCESS) {
        free(buffer);
        return NULL;
    }

    buffer->size = size;

    if (data) {
        graphics_updatebuffer(buffer, data, size);
    }

    return buffer;
}

Buffer graphics_createindexbuffer(const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)calloc(1, sizeof(GPUBuffer));
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VkResult result;

    if (!buffer || size == 0) {
        free(buffer);
        return NULL;
    }

    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer->buffer, &buffer->allocation, NULL);
    if (result != VK_SUCCESS) {
        free(buffer);
        return NULL;
    }

    buffer->size = size;

    if (data) {
        graphics_updatebuffer(buffer, data, size);
    }

    return buffer;
}

Buffer graphics_createuniformbuffer(size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)calloc(1, sizeof(GPUBuffer));
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VkResult result;

    if (!buffer || size == 0) {
        free(buffer);
        return NULL;
    }

    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer->buffer, &buffer->allocation, NULL);
    if (result != VK_SUCCESS) {
        free(buffer);
        return NULL;
    }

    buffer->size = size;
    return buffer;
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    void *mappedData;
    VkResult result;
    size_t copySize;

    if (!buffer || !data || size == 0) {
        return;
    }

    copySize = size < buffer->size ? size : buffer->size;
    result = vmaMapMemory(allocator, buffer->allocation, &mappedData);
    if (result != VK_SUCCESS) {
        return;
    }

    memcpy(mappedData, data, copySize);
    vmaUnmapMemory(allocator, buffer->allocation);
}

void graphics_destroybuffer(Buffer buf)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    if (!buffer) {
        return;
    }

    if (buffer->buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer->buffer, buffer->allocation);
    }

    free(buffer);
}

void graphics_binduniformbuffer(Buffer buf, unsigned slot)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkDescriptorBufferInfo bufferInfo;

    if (!buffer || slot >= MAX_UBO_SLOTS) {
        return;
    }

    bufferInfo.buffer = buffer->buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = buffer->size;

    write.dstSet = uboDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

Texture graphics_createtexture(Texture src)
{
    unsigned char pixel[4] = { 255, 255, 255, 255 };

    if (src) {
        return src;
    }

    return graphics_createtexture_rgba(1, 1, pixel);
}

Texture graphics_createtexture_rgba(int width,
                                    int height,
                                    const unsigned char *pixels)
{
    GPUTexture *texture;
    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer;
    VkBufferImageCopy region = { 0 };
    VkResult result;
    size_t pixelSize;
    void *mappedData = NULL;

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    texture = (GPUTexture *)calloc(1, sizeof(GPUTexture));
    if (!texture) {
        return NULL;
    }

    texture->width = width;
    texture->height = height;

    pixelSize = (size_t)width * (size_t)height * 4;
    bufferInfo.size = pixelSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, NULL);
    if (result != VK_SUCCESS) {
        free(texture);
        return NULL;
    }

    if (vmaMapMemory(allocator, stagingAllocation, &mappedData) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        free(texture);
        return NULL;
    }

    if (pixels) {
        memcpy(mappedData, pixels, pixelSize);
    } else {
        memset(mappedData, 0, pixelSize);
    }
    vmaUnmapMemory(allocator, stagingAllocation);

    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = (uint32_t)width;
    imageInfo.extent.height = (uint32_t)height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = 0;

    result = vmaCreateImage(allocator, &imageInfo, &allocInfo, &texture->image, &texture->allocation, NULL);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        free(texture);
        return NULL;
    }

    commandBuffer = graphics_begin_one_time_commands();
    if (commandBuffer == VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, texture->image, texture->allocation);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        free(texture);
        return NULL;
    }

    graphics_transition_image(commandBuffer, texture->image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    graphics_transition_image(commandBuffer, texture->image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    graphics_end_one_time_commands(commandBuffer);

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    viewInfo.image = texture->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device, &viewInfo, NULL, &texture->view);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(allocator, texture->image, texture->allocation);
        free(texture);
        return NULL;
    }

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxAnisotropy = 1.0f;

    result = vkCreateSampler(device, &samplerInfo, NULL, &texture->sampler);
    if (result != VK_SUCCESS) {
        vkDestroyImageView(device, texture->view, NULL);
        vmaDestroyImage(allocator, texture->image, texture->allocation);
        free(texture);
        return NULL;
    }

    return texture;
}

void graphics_updatetexture(Texture tex,
                            int x, int y,
                            int width, int height,
                            const unsigned char *pixels)
{
    GPUTexture *texture = (GPUTexture *)tex;
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer;
    VkBufferImageCopy region = { 0 };
    VkResult result;
    size_t pixelSize;
    void *mappedData = NULL;

    if (!texture || !pixels || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + width > texture->width || y + height > texture->height) {
        return;
    }

    pixelSize = (size_t)width * (size_t)height * 4;
    bufferInfo.size = pixelSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, NULL);
    if (result != VK_SUCCESS) {
        return;
    }

    if (vmaMapMemory(allocator, stagingAllocation, &mappedData) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return;
    }

    memcpy(mappedData, pixels, pixelSize);
    vmaUnmapMemory(allocator, stagingAllocation);

    commandBuffer = graphics_begin_one_time_commands();
    if (commandBuffer == VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return;
    }

    graphics_transition_image(commandBuffer, texture->image,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    graphics_transition_image(commandBuffer, texture->image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    graphics_end_one_time_commands(commandBuffer);

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void graphics_destroytexture(Texture tex)
{
    GPUTexture *texture = (GPUTexture *)tex;
    if (!texture) {
        return;
    }

    if (texture->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, texture->sampler, NULL);
    }
    if (texture->view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, texture->view, NULL);
    }
    if (texture->image != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, texture->image, texture->allocation);
    }

    free(texture);
}

void graphics_bindtexture(Texture tex, unsigned slot)
{
    GPUTexture *texture = (GPUTexture *)tex;
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkDescriptorImageInfo imageInfo;

    if (!texture || slot >= MAX_BINDLESS_RESOURCES) {
        return;
    }

    imageInfo.sampler = texture->sampler;
    imageInfo.imageView = texture->view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    write.dstSet = bindlessDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
}

RenderPass graphics_createpass(const char *name, RasterState state)
{
    GPURenderPass *pass = (GPURenderPass *)calloc(1, sizeof(GPURenderPass));
    if (!pass) {
        return NULL;
    }

    (void)name;
    pass->state = state;
    return pass;
}

void graphics_beginpass(RenderPass pass)
{
    (void)pass;

    if (!inPass) {
        graphics_predraw();
    }
}

void graphics_endpass(RenderPass pass)
{
    (void)pass;

    if (inPass) {
        graphics_postdraw();
    }
}

Shader graphics_get_shader_variant(Shader base,
                                   const char **defines,
                                   size_t defineCount)
{
    (void)defines;
    (void)defineCount;
    return base;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-submission */
void graphics_postdraw()
{
    /* 6.5. Command Buffer Submission */
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    /* 7.1.2. Pipeline Stages */
    VkPipelineStageFlags waitStage = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    if (graphics_isminimized())
    {
        return;
    }

    inPass = 0;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkCmdEndRenderPass */
    vkCmdEndRenderPass(commandBuffers[imageIndex]);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkEndCommandBuffer */
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &commandBuffers[imageIndex];
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &acquireSemaphore;
    submit.pWaitDstStageMask    = &waitStage;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &releaseSemaphore;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkQueueSubmit */
    vkQueueSubmit(queue, 1, &submit, fences[imageIndex]);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkQueuePresentKHR */
void graphics_present()
{
    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    VkResult res;

    if (graphics_isminimized())
    {
        return;
    }

    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain;
    presentInfo.pImageIndices      = &imageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &releaseSemaphore;

    res = vkQueuePresentKHR(queue, &presentInfo);

    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        graphics_resize();
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
void graphics_resize()
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;

    if (device == VK_NULL_HANDLE)
    {
        return;
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevices[0], surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width  == w &&
        surfaceCapabilities.currentExtent.height == h)
    {
        return;
    }

    vkDeviceWaitIdle(device);

    graphics_destroydepthresources();
    graphics_destroyframebuffers();
    graphics_createswapchain();
    graphics_createsemaphores();
    graphics_getswapchainimages();
    graphics_createdepthresources();
    graphics_createshaders();
    graphics_createrenderpass();
    graphics_creategraphicspipeline();
    graphics_createcommandpools();
    graphics_allocatecommandbuffers();
    graphics_createfences();
    graphics_createimageviews();
    graphics_createframebuffers();
}

void graphics_setshader(Shader _vertShader, Shader _fragShader)
{
    vertShader = _vertShader;
    fragShader = _fragShader;
    graphics_creategraphicspipeline();
}

void graphics_shutdown(void)
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        graphics_destroydepthresources();
        graphics_destroyframebuffers();
        graphics_destroyimageviews();

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, NULL);
        }
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, NULL);
        }
        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, graphicsPipeline, NULL);
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, NULL);
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, NULL);
        }
        if (bindlessDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, bindlessDescriptorSetLayout, NULL);
        }
        if (bindlessDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, bindlessDescriptorPool, NULL);
        }
        if (uboDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, NULL);
        }
        if (uboDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, uboDescriptorPool, NULL);
        }

        if (g_uniformBuffer) {
            graphics_destroybuffer((Buffer)g_uniformBuffer);
            g_uniformBuffer = NULL;
        }

        graphics_destroysemaphores();
        graphics_destroyfences();
        graphics_freecommandbuffers();
        graphics_destroycommandpools();

        if (allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator);
        }
        
        vkDestroyDevice(device, NULL);
    }

    if (swapchainImages) {
        free(swapchainImages);
        swapchainImages = NULL;
    }

    if (physicalDevices) {
        free(physicalDevices);
        physicalDevices = NULL;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, NULL);
    }
}

// Helper function to get vertex input state for a format
static void graphics_get_vertex_input_state(VertexFormat format,
                                             VkVertexInputBindingDescription *bindingDesc,
                                             VkVertexInputAttributeDescription *attrDescs,
                                             uint32_t *attrCount)
{
    bindingDesc->binding = 0;
    bindingDesc->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    switch (format) {
        case VERTEX_FORMAT_FULL:
            bindingDesc->stride = sizeof(Vertex);
            *attrCount = 5;
            
            // Position
            attrDescs[0].binding = 0;
            attrDescs[0].location = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = offsetof(Vertex, position);
            
            // Normal
            attrDescs[1].binding = 0;
            attrDescs[1].location = 1;
            attrDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[1].offset = offsetof(Vertex, normal);
            
            // Tangent
            attrDescs[2].binding = 0;
            attrDescs[2].location = 2;
            attrDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[2].offset = offsetof(Vertex, tangent);
            
            // Bitangent
            attrDescs[3].binding = 0;
            attrDescs[3].location = 3;
            attrDescs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[3].offset = offsetof(Vertex, bitangent);
            
            // TexCoords
            attrDescs[4].binding = 0;
            attrDescs[4].location = 4;
            attrDescs[4].format = VK_FORMAT_R32G32_SFLOAT;
            attrDescs[4].offset = offsetof(Vertex, texCoords);
            break;

        case VERTEX_FORMAT_POS_UV:
            bindingDesc->stride = sizeof(float) * 5; // vec3 pos + vec2 uv
            *attrCount = 2;
            
            // Position
            attrDescs[0].binding = 0;
            attrDescs[0].location = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = 0;
            
            // UV
            attrDescs[1].binding = 0;
            attrDescs[1].location = 1;
            attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
            attrDescs[1].offset = sizeof(float) * 3;
            break;

        case VERTEX_FORMAT_POS_COLOR:
            bindingDesc->stride = sizeof(float) * 7; // vec3 pos + vec4 color
            *attrCount = 2;
            
            // Position
            attrDescs[0].binding = 0;
            attrDescs[0].location = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = 0;
            
            // Color
            attrDescs[1].binding = 0;
            attrDescs[1].location = 1;
            attrDescs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrDescs[1].offset = sizeof(float) * 3;
            break;
    }
}

// Helper function to get blend state for a blend mode
static void graphics_get_blend_state(BlendMode blendMode, VkPipelineColorBlendAttachmentState *blendState)
{
    blendState->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | 
                                  VK_COLOR_COMPONENT_G_BIT | 
                                  VK_COLOR_COMPONENT_B_BIT | 
                                  VK_COLOR_COMPONENT_A_BIT;

    switch (blendMode) {
        case BLEND_NONE:
            blendState->blendEnable = VK_FALSE;
            break;

        case BLEND_ALPHA:
            blendState->blendEnable = VK_TRUE;
            blendState->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendState->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendState->colorBlendOp = VK_BLEND_OP_ADD;
            blendState->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendState->alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BLEND_ADD:
            blendState->blendEnable = VK_TRUE;
            blendState->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendState->dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->colorBlendOp = VK_BLEND_OP_ADD;
            blendState->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BLEND_PREMULT:
            blendState->blendEnable = VK_TRUE;
            blendState->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendState->colorBlendOp = VK_BLEND_OP_ADD;
            blendState->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendState->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendState->alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
}

Pipeline graphics_createpipeline(Shader _vertShader, Shader _fragShader,
                                 VertexFormat format, RasterState state)
{
    GPUPipeline *pipeline = (GPUPipeline *)malloc(sizeof(GPUPipeline));
    if (!pipeline) {
        fprintf(stderr, "Failed to allocate pipeline\n");
        return NULL;
    }

    pipeline->vertShader = (VkShaderModule)_vertShader;
    pipeline->fragShader = (VkShaderModule)_fragShader;
    pipeline->vertexFormat = format;
    pipeline->rasterState = state;

    VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo vertShaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo fragShaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendStateCreateInfo colorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = { 0 };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkVertexInputBindingDescription bindingDescription = {};
    VkVertexInputAttributeDescription attributeDescriptions[5] = {};
    uint32_t attributeCount = 0;

    graphics_get_vertex_input_state(format, &bindingDescription, attributeDescriptions, &attributeCount);

    vertShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStage.module = pipeline->vertShader;
    vertShaderStage.pName = "main";

    fragShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStage.module = pipeline->fragShader;
    fragShaderStage.pName = "main";

    stages[0] = vertShaderStage;
    stages[1] = fragShaderStage;

    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions;

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    rasterization.cullMode = state.backfaceCulling ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    depthStencil.depthTestEnable = state.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    graphics_get_blend_state(state.blendMode, &colorBlendAttachment);

    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    createInfo.stageCount = 2;
    createInfo.pStages = stages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewport;
    createInfo.pRasterizationState = &rasterization;
    createInfo.pMultisampleState = &multisample;
    createInfo.pDepthStencilState = &depthStencil;
    createInfo.pColorBlendState = &colorBlend;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = pipelineLayout;
    createInfo.renderPass = renderPass;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &pipeline->pipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create graphics pipeline: %d\n", result);
        free(pipeline);
        return NULL;
    }

    return (Pipeline)pipeline;
}

void graphics_bindpipeline(Pipeline pipeline)
{
    if (pipeline) {
        GPUPipeline *gpuPipeline = (GPUPipeline *)pipeline;
        currentPipeline = gpuPipeline->pipeline;
        vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, gpuPipeline->pipeline);
    }
}

void graphics_destroypipeline(Pipeline pipeline)
{
    if (pipeline) {
        GPUPipeline *gpuPipeline = (GPUPipeline *)pipeline;
        if (gpuPipeline->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, gpuPipeline->pipeline, NULL);
        }
        free(gpuPipeline);
    }
}
