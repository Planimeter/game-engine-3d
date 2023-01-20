/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"

#define VK_NO_PROTOTYPES
#ifdef HAVE_VULKAN_H
#include <vulkan/vulkan.h>
#else
/* SDL includes a copy for building on systems without the Vulkan SDK */
#include "../lib/SDL2-2.26.2/src/video/khronos/vulkan/vulkan.h"
#endif
#include "SDL_vulkan.h"

/* 4.1. Command Function Pointers */
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

/* 4.2. Instances */
static VkInstance instance;

/* 5. Devices and Queues */
static VkPhysicalDevice *physicalDevices;

/* 5.2.1. Device Creation */
static VkDevice device;

/* 5.3.2. Queue Creation */
static VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

/* 5.3.2. Queue Creation */
static VkQueue queue;

/* 6. Command Buffers */
static VkCommandBuffer commandBuffer;

/* 6.2. Command Pools */
static VkCommandPool commandPool;

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-functionpointers */
static void graphics_getcommandfunctionpointers()
{
    vkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr();
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-instances */
static void graphics_createinstance()
{
    PFN_vkCreateInstance vkCreateInstance;
    unsigned int count;
    char **names;
    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    SDL_Vulkan_GetInstanceExtensions(window, &count, NULL);
    names = malloc(sizeof(char *) * count);
    SDL_Vulkan_GetInstanceExtensions(window, &count, names);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkInstanceCreateInfo */
    createInfo.enabledExtensionCount   = count;
    createInfo.ppEnabledExtensionNames = names;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#vkCreateInstance */
    vkCreateInstance(&createInfo, NULL, &instance);

    free(names);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-physical-device-enumeration */
static void graphics_enumeratephysicaldevices()
{
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
    uint32_t physicalDeviceCount;

    vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
    physicalDevices = malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);

    /* free(physicalDevices); */
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-device-creation */
static void graphics_createdevice()
{
    PFN_vkCreateDevice vkCreateDevice;
    VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    const char *enabledExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#VkDeviceCreateInfo */
    deviceCreateInfo.queueCreateInfoCount    = 1;
    deviceCreateInfo.pQueueCreateInfos       = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount   = 1;
    deviceCreateInfo.ppEnabledExtensionNames = &enabledExtensionNames;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkCreateDevice */
    vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    vkCreateDevice(physicalDevices[0], &deviceCreateInfo, NULL, &device);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-queue-creation */
static void graphics_createqueue()
{
    const float queuePriority = 1.0f;

    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetDeviceQueue */
static void graphics_getqueue()
{
    VkQueue queue;
    PFN_vkGetDeviceQueue vkGetDeviceQueue;

    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
    vkGetDeviceQueue(device, 0, 0, &queue);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffer-allocation */
static void graphics_allocatecommandbuffer()
{
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandBufferAllocateInfo */
    allocateInfo.commandPool        = commandPool;
    allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkAllocateCommandBuffers */
    vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
    vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-pools */
static void graphics_createcommandpool()
{
    PFN_vkCreateCommandPool vkCreateCommandPool;
    VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandPoolCreateInfo */
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkCreateCommandPool */
    vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");
    vkCreateCommandPool(device, &commandPoolCreateInfo, NULL, &commandPool);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_surface */
static void graphics_createsurface()
{
    VkSurfaceKHR surface;

    SDL_Vulkan_CreateSurface(window, instance, &surface);
}

void graphics_init()
{
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_getcommandfunctionpointers();
    graphics_createinstance();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_enumeratephysicaldevices();
    graphics_createqueue();
    graphics_createdevice();
    graphics_getqueue();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html */
    graphics_createcommandpool();
    graphics_allocatecommandbuffer();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createsurface();
}

void graphics_present()
{
    /* 6.4 Command Buffer Recording */
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

#if 0
    /* 6.5 Command Buffer Submission */
    PFN_vkQueueSubmit vkQueueSubmit;
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkSubmitInfo */
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &commandBuffer;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-submission */
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkQueueSubmit */
    vkQueueSubmit = (PFN_vkQueueSubmit)vkGetInstanceProcAddr(instance, "vkQueueSubmit");
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
#endif

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandBufferBeginInfo */
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-recording */
    vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer");
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
}
