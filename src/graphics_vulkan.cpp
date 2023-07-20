/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include "window.h"
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include "volk.h"

#include "vk_mem_alloc.h"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

/* 4.2. Instances */
static VkInstance instance;

/* 5. Devices and Queues */
static VkPhysicalDevice *physicalDevices;

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

/* 10. Pipelines */
static VkPipelineLayout pipelineLayout;
static VkPipeline graphicsPipeline;

/* 12.1. Buffers */
static VkBuffer vertexBuffer;

/* VmaAllocation Struct */
static VmaAllocation allocation;

/* 12.5. Image Views */
static VkImageView *swapchainImageViews;

/* 34.2. WSI Surface */
static VkSurfaceKHR surface;

/* 34.10. WSI Swapchain */
static int w, h;
static VkSwapchainKHR swapchain;
static uint32_t swapchainImageCount;
static VkImage *swapchainImages;
static uint32_t imageIndex;

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-instances */
static void graphics_createinstance()
{
    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    const char *enabledLayerNames[] = { "VK_LAYER_KHRONOS_validation" };
    char const *names[2] = { VK_KHR_SURFACE_EXTENSION_NAME };

    volkInitialize();

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    names[1] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    names[1] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_METAL_EXT)
    names[1] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    names[1] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    names[1] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    names[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
    names[1] = VK_KHR_DISPLAY_EXTENSION_NAME;
#else
    #error Platform not supported
#endif

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkApplicationInfo */
    app.apiVersion = VK_API_VERSION_1_3;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkInstanceCreateInfo */
    createInfo.pApplicationInfo        = &app;
    createInfo.enabledLayerCount       = 1;
    createInfo.ppEnabledLayerNames     = enabledLayerNames;
    createInfo.enabledExtensionCount   = 2;
    createInfo.ppEnabledExtensionNames = names;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#vkCreateInstance */
    vkCreateInstance(&createInfo, NULL, &instance);
    volkLoadInstance(instance);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-physical-device-enumeration */
static void graphics_enumeratephysicaldevices()
{
    uint32_t physicalDeviceCount;

    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
    physicalDevices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-device-creation */
static void graphics_createdevice()
{
    VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    float queuePriority = 1.0f;
    const char *enabledExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#VkDeviceCreateInfo */
    createInfo.queueCreateInfoCount    = 1;
    createInfo.pQueueCreateInfos       = &queueCreateInfo;
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = &enabledExtensionNames;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkCreateDevice */
    vkCreateDevice(physicalDevices[0], &createInfo, NULL, &device);
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

    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice   = physicalDevices[0];
    allocatorCreateInfo.device           = device;
    allocatorCreateInfo.instance         = instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

    vmaCreateAllocator(&allocatorCreateInfo, &allocator);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetDeviceQueue */
static void graphics_getqueue()
{
    vkGetDeviceQueue(device, 0, 0, &queue);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-pools */
static void graphics_createcommandpools()
{
    VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    size_t i;

    commandPools = (VkCommandPool *)malloc(sizeof(VkCommandPool) * swapchainImageCount);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandPoolCreateInfo */
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (i = 0; i < swapchainImageCount; i++)
    {
        vkCreateCommandPool(device, &createInfo, NULL, &commandPools[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffer-allocation */
static void graphics_allocatecommandbuffers()
{
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    size_t i;

    commandBuffers = (VkCommandBuffer *)malloc(sizeof(VkCommandBuffer) * swapchainImageCount);

    for (i = 0; i < swapchainImageCount; i++)
    {
        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandBufferAllocateInfo */
        allocateInfo.commandPool        = commandPools[i];
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkAllocateCommandBuffers */
        vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffers[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-fences */
static void graphics_createfences()
{
    VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    size_t i;

    fences = (VkFence *)malloc(sizeof(VkFence) * swapchainImageCount);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#VkFenceCreateInfo */
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateFence */
    for (i = 0; i < swapchainImageCount; i++)
    {
        vkCreateFence(device, &createInfo, NULL, &fences[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-semaphores */
static void graphics_createsemaphores()
{
    VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateSemaphore */
    vkCreateSemaphore(device, &createInfo, NULL, &acquireSemaphore);
    vkCreateSemaphore(device, &createInfo, NULL, &releaseSemaphore);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-creation */
static void graphics_createrenderpass()
{
    VkRenderPassCreateInfo  createInfo     = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkAttachmentDescription attachment     = { 0 };
    VkSubpassDescription    subpass        = { 0 };
    VkAttachmentReference   colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDependency     dependency     = { 0 };

    attachment.format            = VK_FORMAT_B8G8R8A8_UNORM;
    attachment.samples           = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp           = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp     = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout       = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorReference;

    dependency.srcSubpass        = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass        = 0;
    dependency.srcStageMask      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask     = 0;
    dependency.dstAccessMask     = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    createInfo.attachmentCount   = 1;
    createInfo.pAttachments      = &attachment;
    createInfo.subpassCount      = 1;
    createInfo.pSubpasses        = &subpass;
    createInfo.dependencyCount   = 1;
    createInfo.pDependencies     = &dependency;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkCreateRenderPass */
    vkCreateRenderPass(device, &createInfo, NULL, &renderPass);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#_framebuffers */
static void graphics_createframebuffers()
{
    VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    size_t i;

    framebuffers = (VkFramebuffer *)malloc(sizeof(VkFramebuffer) * swapchainImageCount);

    for (i = 0; i < swapchainImageCount; i++)
    {
        VkImageView attachments[] = { swapchainImageViews[i] };

        createInfo.renderPass      = renderPass;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments    = attachments;
        createInfo.width           = w;
        createInfo.height          = h;
        createInfo.layers          = 1;

        vkCreateFramebuffer(device, &createInfo, NULL, &framebuffers[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#shader-modules */
static void graphics_createshaders()
{
    char   *vertBinary;
    size_t  vertSize;
    char   *fragBinary;
    size_t  fragSize;

    vertSize   = filesystem_fileread((void **)&vertBinary, "shaders/triangle.vert.spv");
    fragSize   = filesystem_fileread((void **)&fragBinary, "shaders/triangle.frag.spv");
    vertShader = graphics_createshader(vertBinary, vertSize);
    fragShader = graphics_createshader(fragBinary, fragSize);
    free(fragBinary);
    free(vertBinary);
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

    vertShaderStage.stage                       = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStage.module                      = (VkShaderModule)vertShader;
    vertShaderStage.pName                       = "main";

    fragShaderStage.stage                       = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStage.module                      = (VkShaderModule)fragShader;
    fragShaderStage.pName                       = "main";

    stages[0]                                   = vertShaderStage;
    stages[1]                                   = fragShaderStage;

    vertexInput.vertexBindingDescriptionCount   = 0;
    vertexInput.pVertexBindingDescriptions      = NULL;
    vertexInput.vertexAttributeDescriptionCount = 0;
    vertexInput.pVertexAttributeDescriptions    = NULL;

    inputAssembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount                      = 1;
    viewport.scissorCount                       = 1;

    rasterization.cullMode                      = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace                     = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth                     = 1.0f;

    multisample.rasterizationSamples            = VK_SAMPLE_COUNT_1_BIT;

    colorBlendAttachment.colorWriteMask         = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    colorBlend.attachmentCount                  = 1;
    colorBlend.pAttachments                     = &colorBlendAttachment;

    dynamicState.dynamicStateCount              = 2;
    dynamicState.pDynamicStates                 = states;

    vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout);

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

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &graphicsPipeline);

    graphics_destroyshader(vertShader);
    graphics_destroyshader(fragShader);
    vertShader = VK_NULL_HANDLE;
    fragShader = VK_NULL_HANDLE;
}

typedef struct Vertex {
    glm::vec2 position;
    glm::vec3 color;
} Vertex;

static Vertex triangle_vertices[3] = {
    glm::vec2(0.0, -0.5), glm::vec3(1.0, 0.0, 0.0),
    glm::vec2(0.5, 0.5),  glm::vec3(0.0, 1.0, 0.0),
    glm::vec2(-0.5, 0.5), glm::vec3(0.0, 0.0, 1.0)
};

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-buffers */
static void graphics_createvertexbuffer()
{
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo allocInfo = { 0 };
    void *mappedData;

    bufferInfo.size        = sizeof(triangle_vertices) * sizeof(Vertex);
    bufferInfo.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &vertexBuffer, &allocation, NULL);

    vmaMapMemory(allocator, allocation, &mappedData);
    memcpy(mappedData, &triangle_vertices, sizeof(triangle_vertices) * sizeof(Vertex));
    vmaUnmapMemory(allocator, allocation);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_surface */
static void graphics_createsurface()
{
    window_vulkan_createsurface(instance, &surface);
}

static void graphics_destroyimageviews();
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

    window_vulkan_getdrawablesize(&w, &h);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#VkSwapchainCreateInfoKHR */
    imageExtent.width  = w;
    imageExtent.height = h;

    oldSwapchain = swapchain;

    createInfo.surface          = surface;
    createInfo.minImageCount    = 2;
    createInfo.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
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
    vkCreateSwapchainKHR(device, &createInfo, NULL, &swapchain);

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
    swapchainImages = (VkImage *)malloc(sizeof(VkImage) * swapchainImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-image-views */
static void graphics_createimageviews()
{
    VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    size_t i;

    createInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format                      = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.components.r                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.layerCount = 1;

    swapchainImageViews = (VkImageView *)malloc(sizeof(VkImageView) * swapchainImageCount);

    for (i = 0; i < swapchainImageCount; i++)
    {
        createInfo.image = swapchainImages[i];

        vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]);
    }
}

static void graphics_destroyframebuffers()
{
    size_t i;

    vkQueueWaitIdle(queue);

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    }
    free(framebuffers);
}

static void graphics_destroyimageviews()
{
    size_t i;

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }
    free(swapchainImageViews);
}

static void graphics_destroyfences()
{
    size_t i;

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyFence(device, fences[i], NULL);
    }
    free(fences);
}

static void graphics_freecommandbuffers()
{
    size_t i;

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkFreeCommandBuffers(device, commandPools[i], 1, &commandBuffers[i]);
    }
    free(commandBuffers);
}

static void graphics_destroycommandpools()
{
    size_t i;

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyCommandPool(device, commandPools[i], NULL);
    }
    free(commandPools);

}

static void graphics_destroysemaphores()
{
    vkDestroySemaphore(device, releaseSemaphore, NULL);
    vkDestroySemaphore(device, acquireSemaphore, NULL);
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
    void graphics_shutdown(void);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_createinstance();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_enumeratephysicaldevices();
    graphics_createdevice();
    /* https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html */
    graphics_createallocator();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_getqueue();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html */
    graphics_createsemaphores();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createrenderpass();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html */
    graphics_createshaders();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html */
    graphics_creategraphicspipeline();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html */
    graphics_createvertexbuffer();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createsurface();
    graphics_createswapchain();
    graphics_getswapchainimages();
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

Shader graphics_createshader(const char *shader, size_t size)
{
    VkShaderModule shaderModule;
    VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

    createInfo.codeSize = size;
    createInfo.pCode    = (const uint32_t *)shader;

    vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);

    return shaderModule;
}

void graphics_destroyshader(Shader shader)
{
    vkDestroyShaderModule(device, (VkShaderModule)shader, NULL);
}

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

void graphics_predraw()
{
    /* 3.5. Command Syntax and Duration */
    VkDeviceSize offsets[] = {0};

    /* 6.4. Command Buffer Recording */
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    /* 8.4. Render Pass Commands */
    VkRenderPassBeginInfo renderPassBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    /* 12.1. Buffers */
    VkBuffer vertexBuffers[] = {vertexBuffer};

    /* 19.3. Clear Values */
    VkClearValue clearValue = {{0.01f, 0.01f, 0.033f, 1.0f}};

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
    renderPassBegin.clearValueCount          = 1;
    renderPassBegin.pClearValues             = &clearValue;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-commands */
    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html#pipelines-binding */
    vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

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

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap22.html#vkCmdBindVertexBuffers */
    vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, vertexBuffers, offsets);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdDraw */
    vkCmdDraw(commandBuffers[imageIndex], sizeof(triangle_vertices), 1, 0, 0);
}

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

    graphics_destroyframebuffers();
    graphics_createswapchain();
    graphics_getswapchainimages();
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
    vkDeviceWaitIdle(device);

    graphics_destroyframebuffers();
    graphics_destroyimageviews();

    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyRenderPass(device, renderPass, NULL);

    graphics_destroysemaphores();
    graphics_destroyfences();
    graphics_freecommandbuffers();
    graphics_destroycommandpools();

    vmaDestroyBuffer(allocator, vertexBuffer, allocation);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, NULL);

    free(physicalDevices);

    vkDestroyInstance(instance, NULL);
}
