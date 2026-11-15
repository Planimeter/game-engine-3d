/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include "window.h"
#include "math_c.h"
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

/* Frames in flight. Swapchain images are capped at 16 (see
 * graphics_getswapchainimages) and per-frame semaphores, fences and
 * descriptor sets are all sized from this, so it is defined before any
 * state that depends on it. */
#define MAX_FRAMES_IN_FLIGHT 16

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
/* Fence per frame slot, waited before acquiring that slot's semaphore pair. */
static VkFence frameFences[MAX_FRAMES_IN_FLIGHT];

/* 7.4. Semaphores */
/* One acquire + release semaphore per swapchain image. A single pair cannot be
 * reused while a previous frame's wait/signal is still pending
 * (VUID-vkAcquireNextImageKHR-semaphore-01779,
 * VUID-vkQueueSubmit-pSignalSemaphores-00067). */
static VkSemaphore acquireSemaphores[MAX_FRAMES_IN_FLIGHT];
static VkSemaphore releaseSemaphores[MAX_FRAMES_IN_FLIGHT];

/* 8. Render Pass */
static VkRenderPass renderPass;

/* 8.3. Framebuffers */
static VkFramebuffer *framebuffers;

/* 9. Shaders */
static Shader vertShader;
static Shader fragShader;
/* Instanced vertex shader, paired with fragShader in graphics_draw_instanced */
static Shader g_instanceVertShader;
/* Cached per-instance transform buffer, bound as vertex buffer index 1 */
static Buffer g_instanceBuffer;
/* Cached bone matrices, bound at UBO slot 2 */
static Buffer g_skinBuffer;

/* Shaderc compiler for runtime GLSL→SPIR-V compilation */
static shaderc_compiler_t g_shaderc_compiler;

/* Cached SPIR-V binaries to avoid recompilation on resize */
typedef struct {
	uint32_t *data;
	size_t    size;
} CachedSPIRV;
static CachedSPIRV g_cachedVertSPIRV;
static CachedSPIRV g_cachedFragSPIRV;

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

/* Pipeline cache for graphics_setshader — avoids destroy+recreate for repeated shader pairs */
#define MAX_CACHED_PIPELINES 16
typedef struct {
	VkPipeline pipeline;
	VkShaderModule vertShader;
	VkShaderModule fragShader;
} CachedPipeline;
static CachedPipeline g_cachedPipelines[MAX_CACHED_PIPELINES];
static int g_cachedPipelineCount = 0;

/* 12.2. Images */
static VkFormat depthFormat = VK_FORMAT_UNDEFINED;
static VkImage depthImage = VK_NULL_HANDLE;
static VkImageView depthImageView = VK_NULL_HANDLE;
static VmaAllocation depthAllocation = VK_NULL_HANDLE;

/* 12.5. Image Views */
static VkImageView *swapchainImageViews;

/* 14. Resource Descriptors */
/* Bindless texture slots. Nothing in the engine binds more than a couple at a
 * time (text uses slot 0), and one set is allocated per frame in flight, so
 * this has to stay modest: 16384 was both wasteful and over MoltenVK's
 * maxPerStageDescriptorUpdateAfterBindSamplers limit of 1024. */
#define MAX_BINDLESS_RESOURCES 64
static VkDescriptorPool bindlessDescriptorPool;
static VkDescriptorSetLayout bindlessDescriptorSetLayout;
/* One set per swapchain image, for the same reason as uboDescriptorSets. */
static VkDescriptorSet bindlessDescriptorSets[MAX_FRAMES_IN_FLIGHT];
static int g_bindlessUpdateAfterBind;

static void graphics_flushdescriptorwrites(void);
/* UBO descriptor set (set 1) — bindless uniform buffers for transform + material data */
#define MAX_UBO_SLOTS 16
static VkDescriptorPool uboDescriptorPool;
static VkDescriptorSetLayout uboDescriptorSetLayout;
/* One set per swapchain image. The engine fences per image, so an image's
 * set is only rewritten once its fence has signalled — that keeps
 * vkUpdateDescriptorSets from invalidating a set a submitted frame still
 * references (VUID-vkCmdDraw-commandBuffer-recording). */
static VkDescriptorSet uboDescriptorSets[MAX_FRAMES_IN_FLIGHT];

/* Descriptor writes are deferred to just before vkCmdBindDescriptorSets.
 * graphics_binduniformbuffer()/graphics_bindtexture() are called mid-frame
 * (every material change, and text rebinds a texture each frame), but a
 * vkUpdateDescriptorSets on a set that is already bound to the command buffer
 * being recorded invalidates that buffer — even within a single frame. So
 * bind* only records intent here and graphics_flushdescriptorwrites() applies
 * it, after the fence for imageIndex has been waited on and before the bind. */
static VkDescriptorBufferInfo g_pendingUboInfo[MAX_UBO_SLOTS];
static int g_pendingUboValid[MAX_UBO_SLOTS];
static VkDescriptorImageInfo g_pendingImageInfo[MAX_BINDLESS_RESOURCES];
static int g_pendingImageValid[MAX_BINDLESS_RESOURCES];

typedef struct {
    VkBuffer buffer;
    VmaAllocation allocation;
    size_t size;
    /* Persistently mapped CPU pointer for uniform buffers, or NULL. */
    void *persistentMap;
} GPUBuffer;

/* Global uniform buffer for transform matrices (matches Metal's g_uniformBuffer) */
#define UNIFORM_BUFFER_SIZE 4096
static GPUBuffer *g_uniformBuffer = NULL;

/* Persistent staging buffer for texture uploads — reused across frames */
static VkBuffer g_stagingBuffer = VK_NULL_HANDLE;
static VmaAllocation g_stagingAllocation = VK_NULL_HANDLE;
static void *g_stagingMap = NULL;
static size_t g_stagingSize = 0;

/* 34.2. WSI Surface */
static VkSurfaceKHR surface;

/* 34.10. WSI Swapchain */
static int w, h;
static VkSwapchainKHR swapchain;
static uint32_t swapchainImageCount;
static VkImage *swapchainImages;
static uint32_t imageIndex;
/* Rotating frame counter, used to pick an acquire semaphore before
 * vkAcquireNextImageKHR has written imageIndex. */
static uint32_t g_frameIndex;
/* Frame slot of the image currently being recorded, for semaphore pairing. */
static uint32_t imageFrameIndex;
static VkSurfaceFormatKHR swapchainSurfaceFormat = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-instances */
static void graphics_createinstance()
{
    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };

    const char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    const char **enabledLayerNames = NULL;
    uint32_t enabledLayerCount = 0;

    /* volk must load the loader before any Vulkan entry point is callable.
     * The validation-layer probe below uses vkEnumerateInstanceLayerProperties,
     * so this has to come first. */
    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to initialize Vulkan loader: %d\n", result);
        exit(EXIT_FAILURE);
    }

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

    // Check for portability enumeration extension (required for MoltenVK).
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    if (!availableExtensions) {
        fprintf(stderr, "Failed to allocate memory for extension properties\n");
        return;
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
        return;
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
        return;
    }
    if (physicalDeviceCount > 64) { // Reasonable upper bound
        fprintf(stderr, "Too many physical devices: %u\n", physicalDeviceCount);
        return;
    }
    
    physicalDevices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
    if (!physicalDevices) {
        fprintf(stderr, "Failed to allocate memory for physical devices\n");
        return;
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
        return UINT32_MAX;
    }
    
    VkQueueFamilyProperties *queueFamilies = (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    if (!queueFamilies) {
        fprintf(stderr, "Failed to allocate memory for queue families\n");
        return UINT32_MAX;
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
        return UINT32_MAX;
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
        VkSurfaceFormatKHR fallback = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        return fallback;
    }
    
    VkSurfaceFormatKHR *availableFormats = (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    if (!availableFormats) {
        fprintf(stderr, "Failed to allocate memory for surface formats\n");
        VkSurfaceFormatKHR fallback = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        return fallback;
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
    return VK_FORMAT_UNDEFINED;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-device-creation */
static void graphics_createdevice()
{
    VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    float queuePriority = 1.0f;
    /* VK_KHR_portability_subset is mandatory on MoltenVK: if the device reports
     * it in vkEnumerateDeviceExtensionProperties, it must be enabled here or
     * vkCreateDevice is invalid (VUID-VkDeviceCreateInfo-pProperties-04451). */
    const char *enabledExtensionNames[2] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset" };
    uint32_t enabledExtensionCount = 1;
    VkResult result;

    /* Only enable portability_subset when the device actually advertises it */
    {
        uint32_t deviceExtCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevices[0], NULL, &deviceExtCount, NULL);
        if (deviceExtCount > 0) {
            VkExtensionProperties *deviceExts =
                (VkExtensionProperties *)malloc(deviceExtCount * sizeof(VkExtensionProperties));
            if (deviceExts) {
                vkEnumerateDeviceExtensionProperties(physicalDevices[0], NULL, &deviceExtCount, deviceExts);
                for (uint32_t i = 0; i < deviceExtCount; i++) {
                    if (strcmp(deviceExts[i].extensionName, "VK_KHR_portability_subset") == 0) {
                        enabledExtensionCount = 2;
                        break;
                    }
                }
                free(deviceExts);
            }
        }
    }

    /* Enable Vulkan 1.2 features for bindless rendering.
     *
     * These must be *queried* from the device, not just asserted: enabling a
     * feature the device does not support makes vkCreateDevice invalid, and
     * silently-unsupported descriptor indexing breaks the bindless set's
     * UPDATE_AFTER_BIND pool flag. */
    VkPhysicalDeviceVulkan12Features supported12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceFeatures2 supportedFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    supportedFeatures.pNext = &supported12;
    vkGetPhysicalDeviceFeatures2(physicalDevices[0], &supportedFeatures);

    VkPhysicalDeviceVulkan12Features vulkan12Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vulkan12Features.descriptorIndexing = supported12.descriptorIndexing;
    vulkan12Features.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
    vulkan12Features.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
    vulkan12Features.descriptorBindingVariableDescriptorCount = supported12.descriptorBindingVariableDescriptorCount;
    vulkan12Features.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
    vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
    vulkan12Features.bufferDeviceAddress = supported12.bufferDeviceAddress;

    /* The bindless set's pool uses UPDATE_AFTER_BIND, which requires this
     * feature. Without it, fall back to a non-update-after-bind pool. */
    g_bindlessUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;

    if (!supported12.descriptorIndexing) {
        fprintf(stderr, "Vulkan: device lacks descriptorIndexing; bindless textures unavailable\n");
    }

    // Find suitable queue family first
    graphicsQueueFamily = graphics_findqueuefamily(physicalDevices[0]);

    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#VkDeviceCreateInfo */
    createInfo.queueCreateInfoCount    = 1;
    createInfo.pQueueCreateInfos       = &queueCreateInfo;
    createInfo.enabledExtensionCount   = enabledExtensionCount;
    createInfo.ppEnabledExtensionNames = enabledExtensionNames;
    createInfo.pNext                   = &vulkan12Features;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkCreateDevice */
    result = vkCreateDevice(physicalDevices[0], &createInfo, NULL, &device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan device: %d\n", result);
        return;
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
        return;
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
        return;
    }

    commandPools = (VkCommandPool *)malloc(sizeof(VkCommandPool) * swapchainImageCount);
    if (!commandPools) {
        fprintf(stderr, "Failed to allocate memory for command pools\n");
        return;
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
            return;
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
        return;
    }

    commandBuffers = (VkCommandBuffer *)malloc(sizeof(VkCommandBuffer) * swapchainImageCount);
    if (!commandBuffers) {
        fprintf(stderr, "Failed to allocate memory for command buffers\n");
        return;
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
            return;
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
        return;
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#VkFenceCreateInfo */
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateFence */
    for (i = 0; i < swapchainImageCount; i++)
    {
        result = vkCreateFence(device, &createInfo, NULL, &fences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create fence %zu: %d\n", i, result);
            return;
        }
    }

    /* One fence per frame slot, matching the per-slot semaphore pairs. */
    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        result = vkCreateFence(device, &createInfo, NULL, &frameFences[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create frame fence %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-semaphores */
static void graphics_createsemaphores()
{
    VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkResult result;

    /* One pair per swapchain image so a frame's semaphores are never reused
     * while an earlier frame's wait/signal is still pending. */
    for (size_t i = 0; i < swapchainImageCount; i++)
    {
        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateSemaphore */
        result = vkCreateSemaphore(device, &createInfo, NULL, &acquireSemaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create acquire semaphore %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
        result = vkCreateSemaphore(device, &createInfo, NULL, &releaseSemaphores[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create release semaphore %zu: %d\n", i, result);
            exit(EXIT_FAILURE);
        }
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
                   VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    if (g_bindlessUpdateAfterBind) {
        bindingFlags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }

    bindingFlagsInfo.bindingCount = 1;
    bindingFlagsInfo.pBindingFlags = &bindingFlags;

    layoutInfo.flags = g_bindlessUpdateAfterBind
                     ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
                     : 0;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    layoutInfo.pNext = &bindingFlagsInfo;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCreateDescriptorSetLayout */
    result = vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &bindlessDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create bindless descriptor set layout: %d\n", result);
        return;
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets-pools */
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_BINDLESS_RESOURCES * MAX_FRAMES_IN_FLIGHT;

    poolInfo.flags = g_bindlessUpdateAfterBind ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCreateDescriptorPool */
    result = vkCreateDescriptorPool(device, &poolInfo, NULL, &bindlessDescriptorPool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create bindless descriptor pool: %d\n", result);
        return;
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#descriptorsets-allocation */
    {
        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        uint32_t counts[MAX_FRAMES_IN_FLIGHT];
        for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
            layouts[f] = bindlessDescriptorSetLayout;
            counts[f] = MAX_BINDLESS_RESOURCES;
        }

        variableDescriptorCountInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        variableDescriptorCountInfo.pDescriptorCounts = counts;

        allocInfo.descriptorPool = bindlessDescriptorPool;
        allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        allocInfo.pSetLayouts = layouts;
        allocInfo.pNext = &variableDescriptorCountInfo;

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkAllocateDescriptorSets */
        result = vkAllocateDescriptorSets(device, &allocInfo, bindlessDescriptorSets);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to allocate bindless descriptor sets: %d\n", result);
            exit(EXIT_FAILURE);
        }
    }
}

/* Create UBO descriptor set (set 1) with MAX_UBO_SLOTS uniform buffer descriptors.
 * Each slot can be bound to a VkBuffer via vkUpdateDescriptorSets.
 * Used for transforms (slot 0), material data (slot 1), etc. */
static void graphics_createubodescriptors()
{
    VkDescriptorSetLayoutBinding bindings[MAX_UBO_SLOTS];
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize poolSize = {};
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkResult result;
    int i;

    /* One discrete binding per slot so shaders can declare individual
     * layout(set = 1, binding = slot) uniform blocks */
    for (i = 0; i < MAX_UBO_SLOTS; i++) {
        bindings[i].binding = (uint32_t)i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = NULL;
    }

    layoutInfo.bindingCount = MAX_UBO_SLOTS;
    layoutInfo.pBindings = bindings;

    result = vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &uboDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create UBO descriptor set layout: %d\n", result);
        exit(EXIT_FAILURE);
    }

    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_UBO_SLOTS * MAX_FRAMES_IN_FLIGHT;

    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    result = vkCreateDescriptorPool(device, &poolInfo, NULL, &uboDescriptorPool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create UBO descriptor pool: %d\n", result);
        exit(EXIT_FAILURE);
    }

    /* One set per swapchain image, so a set is only ever rewritten once its
     * fence has signalled and no in-flight frame can still reference it. */
    {
        VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
        for (int f = 0; f < MAX_FRAMES_IN_FLIGHT; f++) {
            layouts[f] = uboDescriptorSetLayout;
        }

        allocInfo.descriptorPool = uboDescriptorPool;
        allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        allocInfo.pSetLayouts = layouts;

        result = vkAllocateDescriptorSets(device, &allocInfo, uboDescriptorSets);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to allocate UBO descriptor sets: %d\n", result);
            exit(EXIT_FAILURE);
        }
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
        return;
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
        return;
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
            return;
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#shader-modules */
static int graphics_createshaders()
{
    if (g_cachedVertSPIRV.data && g_cachedFragSPIRV.data) {
        /* Reuse cached SPIR-V on resize — skip shaderc recompilation */
        VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

        createInfo.codeSize = g_cachedVertSPIRV.size;
        createInfo.pCode    = g_cachedVertSPIRV.data;
        VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, (VkShaderModule *)&vertShader);
        if (vkResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create cached vert shader module: %d\n", vkResult);
            return 0;
        }

        createInfo.codeSize = g_cachedFragSPIRV.size;
        createInfo.pCode    = g_cachedFragSPIRV.data;
        vkResult = vkCreateShaderModule(device, &createInfo, NULL, (VkShaderModule *)&fragShader);
        if (vkResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create cached frag shader module: %d\n", vkResult);
            return 0;
        }
        return 1;
    }

    char   *vertSource;
    size_t  vertSize;
    char   *fragSource;
    size_t  fragSize;

    vertSize   = filesystem_fileread((void **)&vertSource, "shaders/triangle.vert");
    fragSize   = filesystem_fileread((void **)&fragSource, "shaders/triangle.frag");

    /* Compile and cache vertex SPIR-V */
    {
        shaderc_shader_kind kind = shaderc_glsl_vertex_shader;
        shaderc_compile_options_t options = shaderc_compile_options_initialize();
        shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
        shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
        shaderc_compilation_result_t result = shaderc_compile_into_spv(
            g_shaderc_compiler, vertSource, vertSize, kind, "shader", "main", options);
        shaderc_compile_options_release(options);

        if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
            fprintf(stderr, "Vertex shader compilation failed:\n%s\n",
                    shaderc_result_get_error_message(result));
            shaderc_result_release(result);
            free(fragSource);
            free(vertSource);
            return 0;
        }

        size_t spvSize = shaderc_result_get_length(result);
        g_cachedVertSPIRV.data = (uint32_t *)malloc(spvSize);
        memcpy(g_cachedVertSPIRV.data, shaderc_result_get_bytes(result), spvSize);
        g_cachedVertSPIRV.size = spvSize;

        VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = spvSize;
        createInfo.pCode    = g_cachedVertSPIRV.data;
        VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, (VkShaderModule *)&vertShader);
        shaderc_result_release(result);
        if (vkResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create vert shader module: %d\n", vkResult);
            free(fragSource);
            free(vertSource);
            return 0;
        }
    }

    /* Compile and cache fragment SPIR-V */
    {
        shaderc_shader_kind kind = shaderc_glsl_fragment_shader;
        shaderc_compile_options_t options = shaderc_compile_options_initialize();
        shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
        shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
        shaderc_compilation_result_t result = shaderc_compile_into_spv(
            g_shaderc_compiler, fragSource, fragSize, kind, "shader", "main", options);
        shaderc_compile_options_release(options);

        if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
            fprintf(stderr, "Fragment shader compilation failed:\n%s\n",
                    shaderc_result_get_error_message(result));
            shaderc_result_release(result);
            free(fragSource);
            free(vertSource);
            return 0;
        }

        size_t spvSize = shaderc_result_get_length(result);
        g_cachedFragSPIRV.data = (uint32_t *)malloc(spvSize);
        memcpy(g_cachedFragSPIRV.data, shaderc_result_get_bytes(result), spvSize);
        g_cachedFragSPIRV.size = spvSize;

        VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = spvSize;
        createInfo.pCode    = g_cachedFragSPIRV.data;
        VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, (VkShaderModule *)&fragShader);
        shaderc_result_release(result);
        if (vkResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create frag shader module: %d\n", vkResult);
            free(fragSource);
            free(vertSource);
            return 0;
        }
    }

    free(fragSource);
    fragSource = NULL;
    free(vertSource);
    vertSource = NULL;

    /* Instanced vertex shader: the model matrix arrives per instance as
     * vertex attributes 5-8. Failure is non-fatal — graphics_draw_instanced
     * falls back to per-instance draws when this module is missing. */
    {
        char   *src = NULL;
        size_t  srcSize = filesystem_fileread((void **)&src, "shaders/triangle_instanced.vert");
        if (!src || srcSize == 0) {
            fprintf(stderr, "Failed to read shaders/triangle_instanced.vert\n");
        } else {
            shaderc_compile_options_t options = shaderc_compile_options_initialize();
            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
            shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
            shaderc_compilation_result_t result = shaderc_compile_into_spv(
                g_shaderc_compiler, src, srcSize, shaderc_glsl_vertex_shader, "shader", "main", options);
            shaderc_compile_options_release(options);

            if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
                fprintf(stderr, "Instanced vertex shader compilation failed:\n%s\n",
                        shaderc_result_get_error_message(result));
                shaderc_result_release(result);
            } else {
                size_t spvSize = shaderc_result_get_length(result);
                VkShaderModuleCreateInfo moduleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                moduleInfo.codeSize = spvSize;
                moduleInfo.pCode = (const uint32_t *)shaderc_result_get_bytes(result);
                VkResult vkResult = vkCreateShaderModule(device, &moduleInfo, NULL,
                                                         (VkShaderModule *)&g_instanceVertShader);
                if (vkResult != VK_SUCCESS) {
                    fprintf(stderr, "Failed to create instanced vertex shader module: %d\n", vkResult);
                    g_instanceVertShader = VK_NULL_HANDLE;
                }
                shaderc_result_release(result);
            }
            free(src);
        }
    }

    return 1;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html#pipelines-graphics */
static void graphics_creategraphicspipeline()
{
    /* Invalidate pipeline cache — any cached handles are now stale */
    g_cachedPipelineCount = 0;

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
    VkPipelineDepthStencilStateCreateInfo         depthStencil             = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[7] = {};
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
    /* Skin attributes. Always bound so the same pipeline draws skinned and
     * static models; static vertices carry bone 0 at weight 1, i.e. identity. */
    attributeDescriptions[5].binding = 0;
    attributeDescriptions[5].location = 5;
    attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[5].offset = offsetof(Vertex, boneWeights);
    attributeDescriptions[6].binding = 0;
    attributeDescriptions[6].location = 6;
    attributeDescriptions[6].format = VK_FORMAT_R32G32B32A32_SINT;
    attributeDescriptions[6].offset = offsetof(Vertex, boneIndices);

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
    vertexInput.vertexAttributeDescriptionCount = 7;
    vertexInput.pVertexAttributeDescriptions    = attributeDescriptions;

    inputAssembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount                      = 1;
    viewport.scissorCount                       = 1;

    rasterization.cullMode                      = VK_CULL_MODE_BACK_BIT;
    /* Engine meshes are CCW in GL space, but the vertex shaders negate y for
     * Vulkan's y-down clip space, which reverses screen-space winding. */
    rasterization.frontFace                     = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth                     = 1.0f;

    multisample.rasterizationSamples            = VK_SAMPLE_COUNT_1_BIT;

    /* Depth test/write on for the default 3D pipeline. Leaving
     * pDepthStencilState NULL while the render pass has a depth attachment
     * makes every draw produce no fragments (Vulkan spec: no depth/stencil
     * state means the depth test always fails). */
    depthStencil.depthTestEnable         = VK_TRUE;
    depthStencil.depthWriteEnable        = VK_TRUE;
    depthStencil.depthCompareOp          = VK_COMPARE_OP_GREATER;  /* reverse-Z */
    depthStencil.depthBoundsTestEnable   = VK_FALSE;
    depthStencil.stencilTestEnable       = VK_FALSE;

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

    /* Per-draw transforms (mvp + world) via push constants so several draws
     * per frame can't clobber shared uniform memory */
    VkPushConstantRange transformRange = {};
    transformRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    transformRange.offset = 0;
    transformRange.size = 128;

    pipelineLayoutCreateInfo.setLayoutCount = 2;
    pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &transformRange;

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    }

    VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline layout: %d\n", result);
        return;
    }

    createInfo.stageCount                       = 2;
    createInfo.pStages                          = stages;
    createInfo.pVertexInputState                = &vertexInput;
    createInfo.pInputAssemblyState              = &inputAssembly;
    createInfo.pViewportState                   = &viewport;
    createInfo.pRasterizationState              = &rasterization;
    createInfo.pMultisampleState                = &multisample;
    createInfo.pDepthStencilState               = &depthStencil;
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
        return;
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
        return;
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
        return;
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
        return;
    }
    if (swapchainImageCount > 16) {
        fprintf(stderr, "Too many swapchain images: %u\n", swapchainImageCount);
        return;
    }
    
    swapchainImages = (VkImage *)malloc(sizeof(VkImage) * swapchainImageCount);
    if (!swapchainImages) {
        fprintf(stderr, "Failed to allocate memory for swapchain images\n");
        return;
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
        return;
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
        return;
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
        return;
    }

    for (i = 0; i < swapchainImageCount; i++)
    {
        createInfo.image = swapchainImages[i];

        result = vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create image view %zu: %d\n", i, result);
            return;
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

    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (frameFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, frameFences[i], NULL);
            frameFences[i] = VK_NULL_HANDLE;
        }
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
    for (size_t i = 0; i < swapchainImageCount; i++) {
        if (releaseSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, releaseSemaphores[i], NULL);
            releaseSemaphores[i] = VK_NULL_HANDLE;
        }
        if (acquireSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, acquireSemaphores[i], NULL);
            acquireSemaphores[i] = VK_NULL_HANDLE;
        }
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkAcquireNextImageKHR */
static VkResult graphics_acquirenextimage()
{
    VkResult res;

    /* Wait on this frame slot's fence *before* acquiring, not after on the
     * image's fence. The acquire semaphore may still have the previous
     * frame's signal pending, and vkAcquireNextImageKHR requires a semaphore
     * with no outstanding operations
     * (VUID-vkAcquireNextImageKHR-semaphore-01779). Waiting first also means
     * framesInFlight is effectively 1, which is why rotating over the image
     * count is safe here. */
    if (frameFences[g_frameIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(device, 1, &frameFences[g_frameIndex], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &frameFences[g_frameIndex]);
    }

    res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSemaphores[g_frameIndex], VK_NULL_HANDLE, &imageIndex);
    if (res != VK_SUCCESS)
    {
        return res;
    }
    if (commandPools[imageIndex] != VK_NULL_HANDLE)
    {
        vkResetCommandPool(device, commandPools[imageIndex], 0);
    }

    /* Submit and present must use the same semaphore pair that acquired this
     * image, so record which frame slot it belongs to. */
    imageFrameIndex = g_frameIndex;

    /* Advance to the next frame's semaphore pair. The fence wait above means
     * at most swapchainImageCount frames are ever in flight, which never
     * exceeds MAX_FRAMES_IN_FLIGHT. */
    g_frameIndex = (g_frameIndex + 1) % (swapchainImageCount < MAX_FRAMES_IN_FLIGHT
                                         ? swapchainImageCount : MAX_FRAMES_IN_FLIGHT);
    return VK_SUCCESS;
}

void graphics_init()
{
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_createinstance();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_enumeratephysicaldevices();    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createsurface();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_createdevice();
    /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html */
    graphics_createallocator();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_getqueue();
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
        return;
    }

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html */
    if (!graphics_createshaders()) {
        fprintf(stderr, "Vulkan: shader setup failed, cannot continue\n");
        return;
    }
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
    /* Semaphores are per swapchain image, so they must come after
     * graphics_getswapchainimages() has set swapchainImageCount. */
    graphics_createsemaphores();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html */
    graphics_createimageviews();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createframebuffers();

    /* Seed the material descriptor (set 1, binding 1) so it is valid before the
     * first setmaterial() records a pending write. Without this the first
     * draw reads an uninitialised descriptor
     * (VUID-vkCmdDrawIndexed-None-08114). */
    {
        static const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        GPUBuffer *seed = (GPUBuffer *)graphics_createuniformbuffer(UNIFORM_BUFFER_SIZE);
        if (seed) {
            graphics_updatebuffer((Buffer)seed, identity, sizeof(identity));
            graphics_binduniformbuffer((Buffer)seed, 1);
            graphics_flushdescriptorwrites();
        }
    }

    atexit(graphics_shutdown);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#vkCreateShaderModule */
Shader graphics_createshader(ShaderStage stage, const char *source, size_t size,
                             const char **defines, size_t defineCount)
{
    /* SPIR-V magic number: 0x07230203. If the input is already compiled SPIR-V,
     * skip shaderc and use it directly. */
    static const uint32_t SPIRV_MAGIC = 0x07230203;
    int isSPIRV = (size >= 4 && size % 4 == 0 &&
                   *(const uint32_t *)source == SPIRV_MAGIC);

    VkShaderModule shaderModule;

    if (isSPIRV) {
        /* Input is already SPIR-V — use directly */
        VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = size;
        createInfo.pCode    = (const uint32_t *)source;

        VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);
        if (vkResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create shader module from SPIR-V: %d\n", vkResult);
            return NULL;
        }
        return shaderModule;
    }

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
            return NULL;
    }

    /* Input already compiled to SPIR-V (magic 0x07230203 LE, e.g. font .spv
     * files) — pass the bytes straight to vkCreateShaderModule. shaderc only
     * accepts GLSL source. */
    if (size >= 4 && memcmp(source, "\x03\x02\x23\x07", 4) == 0) {
        VkShaderModule shaderModule;
        VkShaderModuleCreateInfo spvInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        spvInfo.codeSize = size;
        spvInfo.pCode    = (const uint32_t *)source;
        VkResult spvResult = vkCreateShaderModule(device, &spvInfo, NULL, &shaderModule);
        if (spvResult != VK_SUCCESS) {
            fprintf(stderr, "Failed to create shader module: %d\n", spvResult);
            return NULL;
        }
        return shaderModule;
    }

    /* Set up compilation options */
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    if (!options) {
        fprintf(stderr, "Failed to create shaderc compile options\n");
        return NULL;
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
        return NULL;
    }

    /* Create VkShaderModule from compiled SPIR-V */
    VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = shaderc_result_get_length(result);
    createInfo.pCode    = (const uint32_t *)shaderc_result_get_bytes(result);

    VkResult vkResult = vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);
    if (vkResult != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module: %d\n", vkResult);
        shaderc_result_release(result);
        return NULL;
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
    clearValues[1].depthStencil.depth = 0.0f;  /* reverse-Z */
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

    /* Apply any binduniformbuffer()/bindtexture() calls from the previous
     * frame before binding, so no write lands on an already-bound set. */
    graphics_flushdescriptorwrites();

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap14.html#vkCmdBindDescriptorSets */
    vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 0, 1, &bindlessDescriptorSets[imageIndex], 0, NULL);

    /* Bind this frame's UBO descriptor set at set 1 */
    if (imageIndex < MAX_FRAMES_IN_FLIGHT) {
        vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pipelineLayout, 1, 1, &uboDescriptorSets[imageIndex], 0, NULL);
    }

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

/* GPU vertex/index buffers live in the Model's Mesh fields (same layout as
 * the Metal backend), so the Model* handed to callers is a real Model and
 * model.h accessors (model_get_mesh_count etc.) work on it. */

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
    VkImage image;
    VkImageView view;
    VkSampler sampler;
    VmaAllocation allocation;
    int width;
    int height;
} GPUTexture;

typedef struct {
    RasterState state;
    GPUTexture *colorTextures[4];
    int colorCount;
    GPUTexture *depthTexture;
    int hasDepth;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    int initialized;
} GPURenderPass;

static GPURenderPass *g_currentRenderPass = NULL;

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

static VkFence g_uploadFence = VK_NULL_HANDLE;

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

    if (g_uploadFence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(device, &fi, NULL, &g_uploadFence);
    }

    vkQueueSubmit(queue, 1, &submit, g_uploadFence);
    vkWaitForFences(device, 1, &g_uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &g_uploadFence);

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

/* Destroy any GPU buffers already created for the model's meshes (safe to
 * call with none created). Does not free the Model itself. */
static void graphics_freemodelbuffers(Model *modelData)
{
    if (!modelData || !modelData->meshes) {
        return;
    }

    for (uint32_t i = 0; i < modelData->meshCount; i++) {
        Mesh *mesh = &modelData->meshes[i];
        if (mesh->vertexBuffer && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, (VkBuffer)mesh->vertexBuffer,
                             (VmaAllocation)mesh->vertexBufferMemory);
        }
        if (mesh->indexBuffer && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, (VkBuffer)mesh->indexBuffer,
                             (VmaAllocation)mesh->indexBufferMemory);
        }
        mesh->vertexBuffer = NULL;
        mesh->indexBuffer = NULL;
        mesh->vertexBufferMemory = NULL;
        mesh->indexBufferMemory = NULL;
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-buffers */
static int graphics_createmodelgpu(Model *modelData)
{
    if (!modelData || !modelData->meshes) {
        return 0;
    }

    for (uint32_t i = 0; i < modelData->meshCount; i++) {
        Mesh *mesh = &modelData->meshes[i];
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        VmaAllocationCreateInfo allocInfo = { 0 };
        VkResult result;

        mesh->vertexBuffer = NULL;
        mesh->indexBuffer = NULL;
        mesh->vertexBufferMemory = NULL;
        mesh->indexBufferMemory = NULL;

        bufferInfo.size        = mesh->vertexCount * sizeof(Vertex);
        bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#gac72ee55598617e8eecca384e746bab51 */
        result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                 (VkBuffer *)&mesh->vertexBuffer,
                                 (VmaAllocation *)&mesh->vertexBufferMemory, NULL);        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create vertex buffer for mesh %d: %d\n", i, result);
            return 0;
        }

        void *mappedData;
        result = vmaMapMemory(allocator, (VmaAllocation)mesh->vertexBufferMemory, &mappedData);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to map vertex buffer for mesh %d: %d\n", i, result);
            return 0;
        }
        memcpy(mappedData, mesh->vertices, bufferInfo.size);
        vmaUnmapMemory(allocator, (VmaAllocation)mesh->vertexBufferMemory);

        bufferInfo.size  = mesh->indexCount * sizeof(uint32_t);
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#gac72ee55598617e8eecca384e746bab51 */
        result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                 (VkBuffer *)&mesh->indexBuffer,
                                 (VmaAllocation *)&mesh->indexBufferMemory, NULL);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to create index buffer for mesh %d: %d\n", i, result);
            return 0;
        }

        result = vmaMapMemory(allocator, (VmaAllocation)mesh->indexBufferMemory, &mappedData);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "Failed to map index buffer for mesh %d: %d\n", i, result);
            return 0;
        }
        memcpy(mappedData, mesh->indices, bufferInfo.size);
        vmaUnmapMemory(allocator, (VmaAllocation)mesh->indexBufferMemory);
    }

    /* CPU-side mesh data stays alive, matching the Metal backend */
    return 1;
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

    if (!graphics_createmodelgpu(modelData)) {
        fprintf(stderr, "Failed to create GPU buffers for model: %s\n", filepath);
        graphics_freemodelbuffers(modelData);
        model_destroy(modelData);
        return NULL;
    }

    return modelData;
}

Model *graphics_createmesh(const void *vertices, size_t vertexCount,
                          size_t vertexStride,
                          const uint32_t *indices, size_t indexCount)
{
    if (!vertices || vertexCount == 0 || vertexStride == 0) {
        return NULL;
    }

    Model *model = (Model *)calloc(1, sizeof(Model));
    if (!model) return NULL;

    model->meshCount = 1;
    model->meshes = (Mesh *)calloc(1, sizeof(Mesh));
    if (!model->meshes) {
        free(model);
        return NULL;
    }

    Mesh *mesh = &model->meshes[0];
    mesh->vertexCount = (uint32_t)vertexCount;
    mesh->indexCount = (uint32_t)indexCount;
    mesh->vertices = (Vertex *)malloc(vertexCount * vertexStride);
    if (!mesh->vertices) {
        free(model->meshes);
        free(model);
        return NULL;
    }
    memcpy(mesh->vertices, vertices, vertexCount * vertexStride);

    if (indices && indexCount > 0) {
        mesh->indices = (uint32_t *)malloc(indexCount * sizeof(uint32_t));
        if (mesh->indices) {
            memcpy(mesh->indices, indices, indexCount * sizeof(uint32_t));
        } else {
            mesh->indexCount = 0;
        }
    }

    mesh->material.ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    mesh->material.diffuse = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    mesh->material.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mesh->material.emission = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    mesh->material.shininess = 32.0f;

    if (!graphics_createmodelgpu(model)) {
        graphics_freemodelbuffers(model);
        model_destroy(model);
        return NULL;
    }

    return model;
}

/* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/group__group__alloc.html#ga0386f883b5c5a093975b6303569735aa */
/* Skinning: bone matrices as a UBO at set 1 binding 2. MAX_SKIN_BONES must
 * match the shaders. */
#define MAX_SKIN_BONES 64
#define SKIN_BUFFER_SIZE (MAX_SKIN_BONES * 16 * sizeof(float))

/* Bone 0 must be the identity: every vertex shader reads it, and unskinned
 * meshes weight bone 0 at 1.0. A zeroed bone 0 collapses all geometry. */
static void vulkan_skinbuffer_init(void)
{
    if (!g_skinBuffer) {
        g_skinBuffer = graphics_createuniformbuffer(SKIN_BUFFER_SIZE);
        if (!g_skinBuffer) return;
    }
    float data[16] = {0};
    data[0] = 1.0f; data[5] = 1.0f; data[10] = 1.0f; data[15] = 1.0f;
    graphics_updatebuffer(g_skinBuffer, data, sizeof(data));
}

void graphics_bindskinidentity(void)
{
    vulkan_skinbuffer_init();
    if (g_skinBuffer) {
        graphics_binduniformbuffer(g_skinBuffer, 2);
    }
}

void graphics_setbones(Model *model, const float *boneMatrices, uint32_t boneCount)
{
    (void)model;

    vulkan_skinbuffer_init();
    if (!g_skinBuffer) return;

    if (boneMatrices && boneCount > 0) {
        if (boneCount > MAX_SKIN_BONES) boneCount = MAX_SKIN_BONES;
        graphics_updatebuffer(g_skinBuffer, boneMatrices,
                              boneCount * 16 * sizeof(float));
    }
    graphics_binduniformbuffer(g_skinBuffer, 2);
}

void graphics_bindmodeltexture(Model *model, Material mat)
{
    if (!model || !mat) {
        return;
    }

    /* Vulkan uses the bindless array (set 0), indexed by the shader through
     * the material's diffuseSlot/normalSlot floats. Slot 0 is the font
     * atlas's, so model textures are assigned from 1. */
    static int nextSlot = 1;
    static void *bound[2];

    int diffuseSlot = -1;
    int normalSlot = -1;

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (mesh->diffuseTexture && diffuseSlot < 0 && nextSlot < MAX_BINDLESS_RESOURCES) {
            diffuseSlot = nextSlot++;
            graphics_bindtexture((Texture)mesh->diffuseTexture, (unsigned)diffuseSlot);
            bound[0] = mesh->diffuseTexture;
        }
        if (mesh->normalTexture && normalSlot < 0 && nextSlot < MAX_BINDLESS_RESOURCES) {
            normalSlot = nextSlot++;
            graphics_bindtexture((Texture)mesh->normalTexture, (unsigned)normalSlot);
            bound[1] = mesh->normalTexture;
        }
        if (diffuseSlot >= 0 && normalSlot >= 0) break;
    }

    (void)bound;

    /* The shader skips a slot below zero, so untextured sides need no
     * fallback texture here. */
    graphics_material_set_float(mat, "diffuseSlot", (float)diffuseSlot);
    graphics_material_set_float(mat, "normalSlot", (float)normalSlot);
}

void graphics_reuploadmodel(Model *model)
{
    if (!model) return;
    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (mesh->vertexBuffer && mesh->vertices && mesh->vertexCount > 0) {
            VkBuffer vertexBuffer = (VkBuffer)mesh->vertexBuffer;
            VmaAllocation alloc = (VmaAllocation)mesh->vertexBufferMemory;
            void *mapped = NULL;
            if (vmaMapMemory(allocator, alloc, &mapped) == VK_SUCCESS) {
                memcpy(mapped, mesh->vertices, mesh->vertexCount * sizeof(Vertex));
                vmaUnmapMemory(allocator, alloc);
            }
        }
    }
}

void graphics_destroymodel(Model *model)
{
    if (!model) {
        return;
    }

    vkQueueWaitIdle(queue);
    graphics_freemodelbuffers(model);
    model_destroy(model);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#drawing */
void graphics_drawmodel(Model *model, Material mat, const float *transform4x4)
{
    if (!model || graphics_isminimized()) {
        return;
    }

    /* Bind the material inside the pass: vkCmdBindDescriptorSets is recorded
     * into the command buffer, so binding it before graphics_predraw() began
     * the render pass would leave the material's UBO unbound at draw time. */
    if (mat) {
        graphics_setmaterial(mat);
    }

    if (model->meshCount == 0) {
        return;
    }

    // Bind default 3D pipeline if no custom pipeline is bound
    if (currentPipeline != graphicsPipeline) {
        vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        currentPipeline = graphicsPipeline;
    }

    /* Per-draw transforms: mvp + world via push constants, so several draws
     * in one frame can't clobber shared uniform memory */
    if (transform4x4) {
        static const float identityModel[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        struct {
            float mvp[16];
            float model[16];
        } pushData;
        memcpy(pushData.mvp, transform4x4, sizeof(pushData.mvp));
        const GPUMaterial *pushMat = (const GPUMaterial *)mat;
        memcpy(pushData.model, pushMat ? pushMat->mat4 : identityModel, sizeof(pushData.model));
        vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushData), &pushData);
    }

    VkDeviceSize offsets[] = {0};

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        VkBuffer vertexBuffer = (VkBuffer)mesh->vertexBuffer;
        if (!vertexBuffer || !mesh->indexBuffer || mesh->indexCount == 0) {
            continue;
        }

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap22.html#vkCmdBindVertexBuffers */
        vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, &vertexBuffer, offsets);

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdBindIndexBuffer */
        vkCmdBindIndexBuffer(commandBuffers[imageIndex], (VkBuffer)mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdDrawIndexed */
        vkCmdDrawIndexed(commandBuffers[imageIndex], mesh->indexCount, 1, 0, 0, 0);
    }
}

void graphics_draw_instanced(Model *model,
                             Material mat,
                             const float *transforms4x4,
                             const float *viewProjection4x4,
                             size_t count)
{
    if (!model || !transforms4x4 || count == 0 || graphics_isminimized()) {
        return;
    }

    if (model->meshCount == 0) {
        return;
    }

    /* Bind inside the pass: descriptor binds are recorded commands, so doing
     * this before graphics_predraw() would place it outside the render pass. */
    graphics_setmaterial(mat);

    /* Per-instance mat4s as vertex binding 1, stepped per instance. */
    size_t needed = count * sizeof(float) * 16;
    GPUBuffer *instBuf = (GPUBuffer *)g_instanceBuffer;
    if (!instBuf || instBuf->size < needed) {
        if (instBuf) {
            graphics_destroybuffer((Buffer)instBuf);
        }
        instBuf = (GPUBuffer *)graphics_createvertexbuffer(NULL, needed);
        if (!instBuf) {
            return;
        }
        g_instanceBuffer = (Buffer)instBuf;
    }

    Pipeline p = NULL;
    if (g_instanceVertShader && fragShader) {
        RasterState state = {.depthWrite = 1, .depthTest = 1, .backfaceCulling = 1, .blendMode = BLEND_NONE};
        p = graphics_createpipeline(g_instanceVertShader, fragShader, VERTEX_FORMAT_INSTANCE, state);
    }

    if (!p) {
        /* No instanced pipeline (shader missing or creation failed): fall back
         * to one ordinary draw per instance. Correct, just not one call. */
        for (size_t i = 0; i < count; i++) {
            float mvp[16];
            if (viewProjection4x4) {
                math_multiply(mvp, viewProjection4x4, &transforms4x4[i * 16]);
            } else {
                memcpy(mvp, &transforms4x4[i * 16], sizeof(mvp));
            }
            graphics_drawmodel(model, mat, mvp);
        }
        return;
    }

    graphics_updatebuffer((Buffer)instBuf, transforms4x4, needed);

    if (currentPipeline != (VkPipeline)p) {
        vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipeline)p);
        currentPipeline = (VkPipeline)p;
    }

    /* Shared view-projection; the per-instance model matrices come from the
     * vertex binding, so the push-constant model slot stays identity. */
    {
        struct {
            float mvp[16];
            float model[16];
        } pushData;
        static const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        if (viewProjection4x4) {
            memcpy(pushData.mvp, viewProjection4x4, sizeof(pushData.mvp));
        } else {
            memcpy(pushData.mvp, identity, sizeof(pushData.mvp));
        }
        memcpy(pushData.model, identity, sizeof(pushData.model));
        vkCmdPushConstants(commandBuffers[imageIndex], pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushData), &pushData);
    }

    {
        VkDeviceSize offsets[2] = {0, 0};
        VkBuffer vertexBuffers[2] = {VK_NULL_HANDLE, instBuf->buffer};
        uint32_t instanceCount = (uint32_t)count;

        for (uint32_t i = 0; i < model->meshCount; i++) {
            Mesh *mesh = &model->meshes[i];
            VkBuffer vertexBuffer = (VkBuffer)mesh->vertexBuffer;
            if (!vertexBuffer || !mesh->indexBuffer || mesh->indexCount == 0) {
                continue;
            }
            vertexBuffers[0] = vertexBuffer;

            vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 2, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffers[imageIndex], (VkBuffer)mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffers[imageIndex], mesh->indexCount, instanceCount, 0, 0, 0);
        }
    }
}

void graphics_draw_buffers(Buffer vertexBuffer,
                           Buffer indexBuffer,
                           size_t indexCount,
                           size_t firstIndex,
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
    vkCmdDrawIndexed(commandBuffers[imageIndex], (uint32_t)indexCount, 1, (uint32_t)firstIndex, 0, 0);
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

/* Every buffer the backend hands out is registered here so shutdown can free
 * the ones the caller still owns. vmaDestroyAllocator does not free them, and
 * vkDestroyDevice reports them as leaked objects. */
static GPUBuffer **g_ownedBuffers;
static size_t g_ownedBufferCount;
static size_t g_ownedBufferCapacity;

/* Textures and pipelines are tracked the same way: anything the caller still
 * owns at shutdown (the font's atlas pages and pipeline) must be freed before
 * vkDestroyDevice, which otherwise reports them as leaked. */
static GPUTexture **g_ownedTextures;
static size_t g_ownedTextureCount;
static size_t g_ownedTextureCapacity;
static GPUPipeline **g_ownedPipelines;
static size_t g_ownedPipelineCount;
static size_t g_ownedPipelineCapacity;

/* Defined next to the state above so every registry is in one place. */
static void graphics_tracktexture(GPUTexture *t){
    if (!t) return;
    if (g_ownedTextureCount == g_ownedTextureCapacity) {
        size_t cap = g_ownedTextureCapacity ? g_ownedTextureCapacity * 2 : 8;
        GPUTexture **tmp = (GPUTexture **)realloc(g_ownedTextures, cap * sizeof(GPUTexture *));
        if (!tmp) return;
        g_ownedTextures = tmp;
        g_ownedTextureCapacity = cap;
    }
    g_ownedTextures[g_ownedTextureCount++] = t;
}

static void graphics_untracktexture(GPUTexture *t)
{
    for (size_t i = 0; i < g_ownedTextureCount; i++) {
        if (g_ownedTextures[i] == t) {
            g_ownedTextures[i] = g_ownedTextures[--g_ownedTextureCount];
            return;
        }
    }
}

static void graphics_freealltextures(void)
{
    while (g_ownedTextureCount > 0) {
        graphics_destroytexture((Texture)g_ownedTextures[--g_ownedTextureCount]);
    }
    free(g_ownedTextures);
    g_ownedTextures = NULL;
    g_ownedTextureCapacity = 0;
}

static void graphics_trackpipeline(GPUPipeline *p)
{
    if (!p) return;
    if (g_ownedPipelineCount == g_ownedPipelineCapacity) {
        size_t cap = g_ownedPipelineCapacity ? g_ownedPipelineCapacity * 2 : 8;
        GPUPipeline **tmp = (GPUPipeline **)realloc(g_ownedPipelines, cap * sizeof(GPUPipeline *));
        if (!tmp) return;
        g_ownedPipelines = tmp;
        g_ownedPipelineCapacity = cap;
    }
    g_ownedPipelines[g_ownedPipelineCount++] = p;
}

static void graphics_untrackpipeline(GPUPipeline *p)
{
    for (size_t i = 0; i < g_ownedPipelineCount; i++) {
        if (g_ownedPipelines[i] == p) {
            g_ownedPipelines[i] = g_ownedPipelines[--g_ownedPipelineCount];
            return;
        }
    }
}

static void graphics_freeallpipelines(void)
{
    while (g_ownedPipelineCount > 0) {
        graphics_destroypipeline((Pipeline)g_ownedPipelines[--g_ownedPipelineCount]);
    }
    free(g_ownedPipelines);
    g_ownedPipelines = NULL;
    g_ownedPipelineCapacity = 0;
}

static void graphics_trackbuffer(GPUBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    if (g_ownedBufferCount == g_ownedBufferCapacity) {
        size_t newCap = g_ownedBufferCapacity ? g_ownedBufferCapacity * 2 : 32;
        GPUBuffer **tmp = (GPUBuffer **)realloc(g_ownedBuffers, newCap * sizeof(GPUBuffer *));
        if (!tmp) {
            return;
        }
        g_ownedBuffers = tmp;
        g_ownedBufferCapacity = newCap;
    }
    g_ownedBuffers[g_ownedBufferCount++] = buffer;
}

static void graphics_untrackbuffer(GPUBuffer *buffer)
{
    for (size_t i = 0; i < g_ownedBufferCount; i++) {
        if (g_ownedBuffers[i] == buffer) {
            g_ownedBuffers[i] = g_ownedBuffers[--g_ownedBufferCount];
            return;
        }
    }
}

static void graphics_freeallbuffers(void)
{
    /* Back to front: callers often depend on a later buffer */
    while (g_ownedBufferCount > 0) {
        graphics_destroybuffer((Buffer)g_ownedBuffers[--g_ownedBufferCount]);
    }
    free(g_ownedBuffers);
    g_ownedBuffers = NULL;
    g_ownedBufferCapacity = 0;
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

    graphics_trackbuffer(buffer);
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

    graphics_trackbuffer(buffer);
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
    graphics_trackbuffer(buffer);

    /* Persistently map the uniform buffer — avoids map/unmap per update */
    vmaMapMemory(allocator, buffer->allocation, &buffer->persistentMap);

    return buffer;
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    size_t copySize;

    if (!buffer || !data || size == 0) {
        return;
    }

    copySize = size < buffer->size ? size : buffer->size;

    if (buffer->persistentMap) {
        memcpy(buffer->persistentMap, data, copySize);
    } else {
        void *mappedData;
        VkResult result = vmaMapMemory(allocator, buffer->allocation, &mappedData);
        if (result != VK_SUCCESS) {
            return;
        }
        memcpy(mappedData, data, copySize);
        vmaUnmapMemory(allocator, buffer->allocation);
    }
}

void graphics_destroybuffer(Buffer buf)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    if (!buffer) {
        return;
    }

    if (buffer->persistentMap && allocator != VK_NULL_HANDLE) {
        vmaUnmapMemory(allocator, buffer->allocation);
        buffer->persistentMap = NULL;
    }

    if (buffer->buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer->buffer, buffer->allocation);
    }

    graphics_untrackbuffer(buffer);
    free(buffer);
}

void graphics_binduniformbuffer(Buffer buf, unsigned slot)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;

    if (!buffer || slot >= MAX_UBO_SLOTS) {
        return;
    }

    /* Recorded, not applied — see graphics_flushdescriptorwrites(). */
    g_pendingUboInfo[slot].buffer = buffer->buffer;
    g_pendingUboInfo[slot].offset = 0;
    g_pendingUboInfo[slot].range = buffer->size;
    g_pendingUboValid[slot] = 1;
}

/* Apply the writes recorded by graphics_binduniformbuffer()/graphics_bindtexture().
 * Must run after the fence for imageIndex has been waited on and before the
 * descriptor sets are bound for the frame. */
static void graphics_flushdescriptorwrites()
{
    VkWriteDescriptorSet writes[MAX_UBO_SLOTS];
    int n = 0;

    if (imageIndex >= MAX_FRAMES_IN_FLIGHT) {
        return;
    }

    for (unsigned i = 0; i < MAX_UBO_SLOTS; i++) {
        if (!g_pendingUboValid[i]) {
            continue;
        }
        memset(&writes[n], 0, sizeof(writes[n]));
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstSet = uboDescriptorSets[imageIndex];
        writes[n].dstBinding = i;
        writes[n].dstArrayElement = 0;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[n].pBufferInfo = &g_pendingUboInfo[i];
        n++;
    }
    if (n > 0) {
        vkUpdateDescriptorSets(device, (uint32_t)n, writes, 0, NULL);
    }

    n = 0;
    for (unsigned i = 0; i < MAX_BINDLESS_RESOURCES; i++) {
        if (!g_pendingImageValid[i]) {
            continue;
        }
        memset(&writes[n], 0, sizeof(writes[n]));
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstSet = bindlessDescriptorSets[imageIndex];
        writes[n].dstBinding = 0;
        writes[n].dstArrayElement = i;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[n].pImageInfo = &g_pendingImageInfo[i];
        n++;
    }
    if (n > 0) {
        vkUpdateDescriptorSets(device, (uint32_t)n, writes, 0, NULL);
    }

    memset(g_pendingUboValid, 0, sizeof(g_pendingUboValid));
    memset(g_pendingImageValid, 0, sizeof(g_pendingImageValid));
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

    graphics_tracktexture(texture);
    return texture;
}

void graphics_updatetexture(Texture tex,
                            int x, int y,
                            int width, int height,
                            const unsigned char *pixels)
{
    GPUTexture *texture = (GPUTexture *)tex;
    VkCommandBuffer commandBuffer;
    VkBufferImageCopy region = { 0 };
    size_t pixelSize;

    if (!texture || !pixels || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + width > texture->width || y + height > texture->height) {
        return;
    }

    pixelSize = (size_t)width * (size_t)height * 4;

    /* Grow persistent staging buffer if needed */
    if (pixelSize > g_stagingSize) {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        VmaAllocationCreateInfo allocInfo = { 0 };

        if (g_stagingBuffer != VK_NULL_HANDLE) {
            vmaUnmapMemory(allocator, g_stagingAllocation);
            vmaDestroyBuffer(allocator, g_stagingBuffer, g_stagingAllocation);
        }

        g_stagingSize = pixelSize < 256 * 1024 ? 256 * 1024 : pixelSize;
        bufferInfo.size = g_stagingSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &g_stagingBuffer, &g_stagingAllocation, NULL) != VK_SUCCESS) {
            g_stagingSize = 0;
            return;
        }
        vmaMapMemory(allocator, g_stagingAllocation, &g_stagingMap);
    }

    memcpy(g_stagingMap, pixels, pixelSize);

    commandBuffer = graphics_begin_one_time_commands();
    if (commandBuffer == VK_NULL_HANDLE) {
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

    vkCmdCopyBufferToImage(commandBuffer, g_stagingBuffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    graphics_transition_image(commandBuffer, texture->image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    graphics_end_one_time_commands(commandBuffer);
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

    graphics_untracktexture(texture);
    free(texture);
}

void graphics_bindtexture(Texture tex, unsigned slot)
{
    GPUTexture *texture = (GPUTexture *)tex;

    if (!texture || slot >= MAX_BINDLESS_RESOURCES) {
        return;
    }

    /* Recorded, not applied — see graphics_flushdescriptorwrites(). */
    g_pendingImageInfo[slot].sampler = texture->sampler;
    g_pendingImageInfo[slot].imageView = texture->view;
    g_pendingImageInfo[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    g_pendingImageValid[slot] = 1;
}

RenderPass graphics_createpass(const char *name, RasterState state)
{
    GPURenderPass *pass = (GPURenderPass *)calloc(1, sizeof(GPURenderPass));
    if (!pass) {
        return NULL;
    }

    (void)name;
    pass->state = state;
    pass->renderPass = VK_NULL_HANDLE;
    pass->framebuffer = VK_NULL_HANDLE;
    pass->initialized = 0;
    return pass;
}

void graphics_pass_set_color_texture(RenderPass pass, Texture tex, unsigned slot)
{
    if (!pass || !tex) return;
    GPURenderPass *gp = (GPURenderPass *)pass;
    GPUTexture *gt = (GPUTexture *)tex;
    if (slot < 4) {
        gp->colorTextures[slot] = gt;
        if ((int)slot + 1 > gp->colorCount) gp->colorCount = (int)slot + 1;
    }
}

void graphics_pass_set_depth_texture(RenderPass pass, Texture tex)
{
    if (!pass || !tex) return;
    GPURenderPass *gp = (GPURenderPass *)pass;
    gp->depthTexture = (GPUTexture *)tex;
    gp->hasDepth = 1;
}

static void graphics_pass_build(GPURenderPass *gp)
{
    if (gp->initialized) return;

    /* Determine attachment count and formats */
    int attachmentCount = gp->colorCount + (gp->hasDepth ? 1 : 0);
    if (attachmentCount == 0) {
        gp->initialized = 1;
        return;
    }

    VkAttachmentDescription attachments[5] = {};
    VkAttachmentReference colorRefs[4] = {};
    VkAttachmentReference depthRef = {};
    int hasDepth = gp->hasDepth;

    for (int i = 0; i < gp->colorCount; i++) {
        if (!gp->colorTextures[i]) continue;
        attachments[i].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        colorRefs[i].attachment = (uint32_t)i;
        colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    if (hasDepth) {
        int depthIdx = gp->colorCount;
        attachments[depthIdx].format = VK_FORMAT_D32_SFLOAT;
        attachments[depthIdx].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[depthIdx].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[depthIdx].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[depthIdx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[depthIdx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[depthIdx].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[depthIdx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthRef.attachment = (uint32_t)depthIdx;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)gp->colorCount;
    subpass.pColorAttachments = colorRefs;
    if (hasDepth) {
        subpass.pDepthStencilAttachment = &depthRef;
    }

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = (uint32_t)attachmentCount;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device, &rpInfo, NULL, &gp->renderPass) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create offscreen render pass\n");
        return;
    }

    /* Create framebuffer */
    VkImageView attachmentsViews[5] = {};
    for (int i = 0; i < gp->colorCount; i++) {
        if (gp->colorTextures[i]) {
            attachmentsViews[i] = gp->colorTextures[i]->view;
        }
    }
    if (hasDepth && gp->depthTexture) {
        attachmentsViews[gp->colorCount] = gp->depthTexture->view;
    }

    VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = gp->renderPass;
    fbInfo.attachmentCount = (uint32_t)attachmentCount;
    fbInfo.pAttachments = attachmentsViews;
    fbInfo.width = (uint32_t)(gp->colorCount > 0 && gp->colorTextures[0] ? gp->colorTextures[0]->width : w);
    fbInfo.height = (uint32_t)(gp->colorCount > 0 && gp->colorTextures[0] ? gp->colorTextures[0]->height : h);
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device, &fbInfo, NULL, &gp->framebuffer) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create offscreen framebuffer\n");
        vkDestroyRenderPass(device, gp->renderPass, NULL);
        gp->renderPass = VK_NULL_HANDLE;
        return;
    }

    gp->initialized = 1;
}

void graphics_beginpass(RenderPass pass)
{
    if (pass) {
        GPURenderPass *gp = (GPURenderPass *)pass;

        /* If currently in the swapchain pass, end just the render pass (not the command buffer) */
        if (inPass && !g_currentRenderPass) {
            vkCmdEndRenderPass(commandBuffers[imageIndex]);
            inPass = 0;
        }

        /* Build Vulkan render pass + framebuffer lazily */
        graphics_pass_build(gp);

        if (gp->renderPass == VK_NULL_HANDLE) {
            inPass = 1;
            g_currentRenderPass = gp;
            return;
        }

        /* Transition color attachments to color attachment layout */
        VkCommandBuffer cmd = commandBuffers[imageIndex];
        for (int i = 0; i < gp->colorCount; i++) {
            if (gp->colorTextures[i]) {
                graphics_transition_image(cmd, gp->colorTextures[i]->image,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }
        if (gp->hasDepth && gp->depthTexture) {
            graphics_transition_image(cmd, gp->depthTexture->image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }

        VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        beginInfo.renderPass = gp->renderPass;
        beginInfo.framebuffer = gp->framebuffer;
        beginInfo.renderArea.extent.width = (uint32_t)(gp->colorCount > 0 && gp->colorTextures[0] ? gp->colorTextures[0]->width : w);
        beginInfo.renderArea.extent.height = (uint32_t)(gp->colorCount > 0 && gp->colorTextures[0] ? gp->colorTextures[0]->height : h);

        VkClearValue clearValues[5] = {};
        for (int i = 0; i < gp->colorCount; i++) {
            clearValues[i].color.float32[0] = 0.0f;
            clearValues[i].color.float32[1] = 0.0f;
            clearValues[i].color.float32[2] = 0.0f;
            clearValues[i].color.float32[3] = 0.0f;
        }
        if (gp->hasDepth) {
            clearValues[gp->colorCount].depthStencil.depth = 0.0f;  /* reverse-Z */
        }
        beginInfo.clearValueCount = (uint32_t)(gp->colorCount + (gp->hasDepth ? 1 : 0));
        beginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

        /* Bind descriptor sets */
        graphics_flushdescriptorwrites();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &bindlessDescriptorSets[imageIndex], 0, NULL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 1, 1, &uboDescriptorSets[imageIndex], 0, NULL);

        VkViewport vp = {};
        vp.width = (float)beginInfo.renderArea.extent.width;
        vp.height = (float)beginInfo.renderArea.extent.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor = {};
        scissor.extent = beginInfo.renderArea.extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        g_currentRenderPass = gp;
        inPass = 1;
    } else {
        /* NULL pass = resume swapchain rendering after offscreen pass */
        if (inPass && g_currentRenderPass) {
            /* End offscreen render pass */
            vkCmdEndRenderPass(commandBuffers[imageIndex]);

            /* Transition color attachments back to shader-read layout */
            VkCommandBuffer cmd = commandBuffers[imageIndex];
            for (int i = 0; i < g_currentRenderPass->colorCount; i++) {
                if (g_currentRenderPass->colorTextures[i]) {
                    graphics_transition_image(cmd, g_currentRenderPass->colorTextures[i]->image,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
            g_currentRenderPass = NULL;
            inPass = 0;
        }

        /* Re-begin the swapchain render pass that graphics_predraw() set up */
        VkRenderPassBeginInfo beginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        beginInfo.renderPass = renderPass;
        beginInfo.framebuffer = framebuffers[imageIndex];
        beginInfo.renderArea.extent.width = w;
        beginInfo.renderArea.extent.height = h;

        VkClearValue clearValues[2];
        clearValues[0].color.float32[0] = CLEAR_COLOR[0];
        clearValues[0].color.float32[1] = CLEAR_COLOR[1];
        clearValues[0].color.float32[2] = CLEAR_COLOR[2];
        clearValues[0].color.float32[3] = CLEAR_COLOR[3];
        clearValues[1].depthStencil.depth = 0.0f;  /* reverse-Z */
        clearValues[1].depthStencil.stencil = 0;
        beginInfo.clearValueCount = 2;
        beginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffers[imageIndex], &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

        /* Re-bind descriptor sets */
        graphics_flushdescriptorwrites();
        vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &bindlessDescriptorSets[imageIndex], 0, NULL);
        vkCmdBindDescriptorSets(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 1, 1, &uboDescriptorSets[imageIndex], 0, NULL);

        VkViewport vp = {};
        vp.width = (float)w;
        vp.height = (float)h;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &vp);

        VkRect2D scissor = {};
        scissor.extent.width = w;
        scissor.extent.height = h;
        vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &scissor);

        inPass = 1;
    }
}

void graphics_endpass(RenderPass pass)
{
    (void)pass;

    if (inPass) {
        if (g_currentRenderPass) {
            /* End offscreen render pass */
            vkCmdEndRenderPass(commandBuffers[imageIndex]);

            /* Transition color attachments back to shader-read layout */
            VkCommandBuffer cmd = commandBuffers[imageIndex];
            for (int i = 0; i < g_currentRenderPass->colorCount; i++) {
                if (g_currentRenderPass->colorTextures[i]) {
                    graphics_transition_image(cmd, g_currentRenderPass->colorTextures[i]->image,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
        }
        inPass = 0;
        g_currentRenderPass = NULL;
    }
}

void graphics_destroypass(RenderPass pass)
{
    if (!pass) return;
    GPURenderPass *gp = (GPURenderPass *)pass;
    if (gp->framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, gp->framebuffer, NULL);
    }
    if (gp->renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, gp->renderPass, NULL);
    }
    free(gp);
}

void graphics_get_text_shaders(Shader *out_vert, Shader *out_frag)
{
    fprintf(stderr, "graphics_get_text_shaders: Text shaders not available on Vulkan backend. "
                    "Ensure SPIR-V shaders are loaded.\n");
    if (out_vert) *out_vert = NULL;
    if (out_frag) *out_frag = NULL;
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
    submit.pWaitSemaphores      = &acquireSemaphores[imageFrameIndex];
    submit.pWaitDstStageMask    = &waitStage;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &releaseSemaphores[imageFrameIndex];

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkQueueSubmit */
    vkQueueSubmit(queue, 1, &submit, frameFences[imageFrameIndex]);
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
    presentInfo.pWaitSemaphores    = &releaseSemaphores[imageFrameIndex];

    res = vkQueuePresentKHR(queue, &presentInfo);

    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        graphics_resize();
    }
}

/* GAME_CAPTURE is not wired up for this backend yet: reading the swapchain
 * image back needs a TRANSFER_SRC barrier and a host-visible copy, and the
 * render pass currently finishes each image in PRESENT_SRC_KHR. Reported as
 * unavailable rather than failed silently. */
void graphics_capture_setup()
{
    fprintf(stderr, "GAME_CAPTURE: not implemented for the Vulkan backend\n");
}

unsigned char *graphics_capture_read(int *width, int *height)
{
    (void)width;
    (void)height;
    return NULL;
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
    if (!graphics_createshaders()) {
        fprintf(stderr, "Vulkan: shader rebuild failed during resize\n");
        return;
    }
    graphics_createrenderpass();    graphics_creategraphicspipeline();
    graphics_createcommandpools();
    graphics_allocatecommandbuffers();
    graphics_createfences();
    graphics_createimageviews();
    graphics_createframebuffers();
}

void graphics_setshader(Shader _vertShader, Shader _fragShader)
{
    VkShaderModule vertModule = (VkShaderModule)_vertShader;
    VkShaderModule fragModule = (VkShaderModule)_fragShader;

    /* Check cache for existing pipeline with same shader pair */
    for (int i = 0; i < g_cachedPipelineCount; i++) {
        if (g_cachedPipelines[i].vertShader == vertModule &&
            g_cachedPipelines[i].fragShader == fragModule)
        {
            if (graphicsPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, graphicsPipeline, NULL);
            }
            graphicsPipeline = g_cachedPipelines[i].pipeline;
            vertShader = _vertShader;
            fragShader = _fragShader;
            return;
        }
    }

    vertShader = _vertShader;
    fragShader = _fragShader;
    graphics_creategraphicspipeline();

    /* Cache the newly created pipeline if space remains */
    if (g_cachedPipelineCount < MAX_CACHED_PIPELINES) {
        g_cachedPipelines[g_cachedPipelineCount].pipeline = graphicsPipeline;
        g_cachedPipelines[g_cachedPipelineCount].vertShader = vertModule;
        g_cachedPipelines[g_cachedPipelineCount].fragShader = fragModule;
        g_cachedPipelineCount++;
    }
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
            swapchain = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, NULL);
            surface = VK_NULL_HANDLE;
        }

        /* Shader modules are owned here, not by the pipeline. */
        if (vertShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, (VkShaderModule)vertShader, NULL);
            vertShader = VK_NULL_HANDLE;
        }
        if (fragShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, (VkShaderModule)fragShader, NULL);
            fragShader = VK_NULL_HANDLE;
        }
        if (g_instanceVertShader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, (VkShaderModule)g_instanceVertShader, NULL);
            g_instanceVertShader = VK_NULL_HANDLE;
        }
        if (graphicsPipeline != VK_NULL_HANDLE) {
            /* Destroy all cached pipelines */
            for (int i = 0; i < g_cachedPipelineCount; i++) {
                if (g_cachedPipelines[i].pipeline != graphicsPipeline) {
                    vkDestroyPipeline(device, g_cachedPipelines[i].pipeline, NULL);
                }
            }
            g_cachedPipelineCount = 0;
            vkDestroyPipeline(device, graphicsPipeline, NULL);
        }
        /* Free cached SPIR-V binaries */
        if (g_cachedVertSPIRV.data) {
            free(g_cachedVertSPIRV.data);
            g_cachedVertSPIRV.data = NULL;
            g_cachedVertSPIRV.size = 0;
        }
        if (g_cachedFragSPIRV.data) {
            free(g_cachedFragSPIRV.data);
            g_cachedFragSPIRV.data = NULL;
            g_cachedFragSPIRV.size = 0;
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
        /* Caches in graphics_createpipeline()/graphics_createuniformbuffer()
         * live past init; vmaDestroyAllocator below does not free them. */
        if (g_instanceBuffer) {
            graphics_destroybuffer(g_instanceBuffer);
            g_instanceBuffer = NULL;
        }
        graphics_freeallbuffers();
        graphics_freeallpipelines();
        graphics_freealltextures();

        if (g_stagingBuffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaUnmapMemory(allocator, g_stagingAllocation);
            vmaDestroyBuffer(allocator, g_stagingBuffer, g_stagingAllocation);
            g_stagingBuffer = VK_NULL_HANDLE;
            g_stagingAllocation = VK_NULL_HANDLE;
            g_stagingMap = NULL;
            g_stagingSize = 0;
        }

        graphics_destroysemaphores();
        graphics_destroyfences();

        if (g_uploadFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, g_uploadFence, NULL);
            g_uploadFence = VK_NULL_HANDLE;
        }

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
                                             VkVertexInputBindingDescription *instanceDesc,
                                             VkVertexInputAttributeDescription *attrDescs,
                                             uint32_t *attrCount)
{
    bindingDesc->binding = 0;
    bindingDesc->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    switch (format) {
        case VERTEX_FORMAT_FULL:
            bindingDesc->stride = sizeof(Vertex);
            *attrCount = 7;

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

            // Skin weights / bone indices. Always declared so one pipeline
            // serves skinned and static models; static vertices carry bone 0
            // at weight 1, which is the identity.
            attrDescs[5].binding = 0;
            attrDescs[5].location = 5;
            attrDescs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrDescs[5].offset = offsetof(Vertex, boneWeights);

            attrDescs[6].binding = 0;
            attrDescs[6].location = 6;
            attrDescs[6].format = VK_FORMAT_R32G32B32A32_SINT;
            attrDescs[6].offset = offsetof(Vertex, boneIndices);
            break;

        case VERTEX_FORMAT_SKINNED:
            bindingDesc->stride = sizeof(Vertex);
            *attrCount = 7;

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

            // BoneIDs
            attrDescs[5].binding = 0;
            attrDescs[5].location = 5;
            attrDescs[5].format = VK_FORMAT_R32G32B32A32_UINT;
            attrDescs[5].offset = offsetof(Vertex, boneIDs);

            // BoneWeights
            attrDescs[6].binding = 0;
            attrDescs[6].location = 6;
            attrDescs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrDescs[6].offset = offsetof(Vertex, boneWeights);
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

        case VERTEX_FORMAT_INSTANCE:
            bindingDesc->stride = sizeof(Vertex);
            *attrCount = 5;

            // Position / Normal / Tangent / Bitangent / TexCoords — same as
            // VERTEX_FORMAT_FULL, on binding 0 stepped per vertex.
            attrDescs[0].binding = 0;
            attrDescs[0].location = 0;
            attrDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[0].offset = offsetof(Vertex, position);

            attrDescs[1].binding = 0;
            attrDescs[1].location = 1;
            attrDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[1].offset = offsetof(Vertex, normal);

            attrDescs[2].binding = 0;
            attrDescs[2].location = 2;
            attrDescs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[2].offset = offsetof(Vertex, tangent);

            attrDescs[3].binding = 0;
            attrDescs[3].location = 3;
            attrDescs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrDescs[3].offset = offsetof(Vertex, bitangent);

            attrDescs[4].binding = 0;
            attrDescs[4].location = 4;
            attrDescs[4].format = VK_FORMAT_R32G32_SFLOAT;
            attrDescs[4].offset = offsetof(Vertex, texCoords);

            /* Skin attributes, same as VERTEX_FORMAT_FULL */
            attrDescs[5].binding = 0;
            attrDescs[5].location = 5;
            attrDescs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrDescs[5].offset = offsetof(Vertex, boneWeights);

            attrDescs[6].binding = 0;
            attrDescs[6].location = 6;
            attrDescs[6].format = VK_FORMAT_R32G32B32A32_SINT;
            attrDescs[6].offset = offsetof(Vertex, boneIndices);

            /* Per-instance mat4 on binding 1, stepped per instance and read
             * as vertex attributes 7-10 (one vec4 per matrix column). */
            instanceDesc->binding = 1;
            instanceDesc->stride = sizeof(float) * 16;
            instanceDesc->inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            for (uint32_t c = 0; c < 4; c++) {
                attrDescs[7 + c].binding = 1;
                attrDescs[7 + c].location = 7 + c;
                attrDescs[7 + c].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attrDescs[7 + c].offset = c * sizeof(float) * 4;
            }
            *attrCount = 11;
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
    if (!_vertShader || !_fragShader) {
        return NULL;
    }

    GPUPipeline *pipeline = (GPUPipeline *)malloc(sizeof(GPUPipeline));
    if (!pipeline) {
        fprintf(stderr, "Failed to allocate pipeline\n");
        return NULL;
    }

    pipeline->vertShader = (VkShaderModule)_vertShader;
    pipeline->fragShader = (VkShaderModule)_fragShader;
    pipeline->vertexFormat = format;
    pipeline->rasterState = state;
    graphics_trackpipeline(pipeline);

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

    VkVertexInputBindingDescription bindingDescriptions[2] = {};
    VkVertexInputBindingDescription *bindingDescription = &bindingDescriptions[0];
    VkVertexInputAttributeDescription attributeDescriptions[11] = {};
    uint32_t attributeCount = 0;

    graphics_get_vertex_input_state(format, bindingDescription, &bindingDescriptions[1],
                                    attributeDescriptions, &attributeCount);

    vertShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStage.module = pipeline->vertShader;
    vertShaderStage.pName = "main";

    fragShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStage.module = pipeline->fragShader;
    fragShaderStage.pName = "main";

    stages[0] = vertShaderStage;
    stages[1] = fragShaderStage;

    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = bindingDescriptions;
    vertexInput.vertexAttributeDescriptionCount = attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions;

    /* VERTEX_FORMAT_INSTANCE adds a second, per-instance binding */
    if (format == VERTEX_FORMAT_INSTANCE) {
        vertexInput.vertexBindingDescriptionCount = 2;
    }

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    rasterization.cullMode = state.backfaceCulling ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    /* Same y-flip caveat as the default pipeline: GL-space CCW winding
     * arrives counter-clockwise after the vertex shader negates y. */
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    depthStencil.depthTestEnable = state.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;  /* reverse-Z */

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

/* Pipelines the caller still owns at shutdown (font.c creates one and only
 * destroys it in font_destroy). Registered so vkDestroyDevice does not report
 * them as leaked. State is declared alongside the buffer/texture registries. */
void graphics_destroypipeline(Pipeline pipeline)
{
    if (pipeline) {
        GPUPipeline *gpuPipeline = (GPUPipeline *)pipeline;
        if (gpuPipeline->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, gpuPipeline->pipeline, NULL);
        }
        graphics_untrackpipeline(gpuPipeline);
        free(gpuPipeline);
    }
}
