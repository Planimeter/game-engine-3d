/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include <stdlib.h>

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

/* 7.3. Fences */
static VkFence fence;

/* 7.4. Semaphores */
static VkSemaphore semaphore;

/* 8. Render Pass */
static VkRenderPass renderPass;

/* 8.3. Framebuffers */
static VkFramebuffer *framebuffers;

/* 9. Shaders */
static Shader vertShader;
static Shader fragShader;

/* 12.5. Image Views */
static VkImageView *swapchainImageViews;

/* 34.2. WSI Surface */
static VkSurfaceKHR surface;

/* 34.10. WSI Swapchain */
static int w, h;
static VkSwapchainKHR swapchain;
static uint32_t swapchainImageCount;
static VkImage *swapchainImages;

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
    const char *enabledLayerNames[] = { "VK_LAYER_KHRONOS_validation" };

    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    SDL_Vulkan_GetInstanceExtensions(window, &count, NULL);
    names = malloc(sizeof(char *) * count);
    SDL_Vulkan_GetInstanceExtensions(window, &count, names);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkInstanceCreateInfo */
    createInfo.enabledLayerCount       = 1;
    createInfo.ppEnabledLayerNames     = enabledLayerNames;
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
    const float queuePriority[] = { 1.0f };

    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = queuePriority;
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

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-fences */
static void graphics_createfence()
{
    PFN_vkCreateFence vkCreateFence;
    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateFence */
    vkCreateFence = (PFN_vkCreateFence)vkGetDeviceProcAddr(device, "vkCreateFence");
    vkCreateFence(device, &fenceCreateInfo, NULL, &fence);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-semaphores */
static void graphics_createsemaphore()
{
    PFN_vkCreateSemaphore vkCreateSemaphore;
    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateSemaphore */
    vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(device, "vkCreateSemaphore");
    vkCreateSemaphore(device, &semaphoreCreateInfo, NULL, &semaphore);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-creation */
static void graphics_createrenderpass()
{
    PFN_vkCreateRenderPass  vkCreateRenderPass;
    VkRenderPassCreateInfo  createInfo     = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkAttachmentDescription attachment     = { 0 };
    VkSubpassDescription    subpass        = { 0 };
    VkAttachmentReference   colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

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

    createInfo.attachmentCount   = 1;
    createInfo.pAttachments      = &attachment;
    createInfo.subpassCount      = 1;
    createInfo.pSubpasses        = &subpass;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkCreateRenderPass */
    vkCreateRenderPass = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(device, "vkCreateRenderPass");
    vkCreateRenderPass(device, &createInfo, NULL, &renderPass);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#_framebuffers */
static void graphics_createframebuffers()
{
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };

    framebuffers = malloc(sizeof(VkFramebuffer) * swapchainImageCount);
    vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(device, "vkCreateFramebuffer");

    for (size_t i = 0; i < swapchainImageCount; i++)
    {
        VkImageView attachments[] = { swapchainImageViews[i] };

        framebufferCreateInfo.renderPass      = renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments    = attachments;
        framebufferCreateInfo.width           = w;
        framebufferCreateInfo.height          = h;
        framebufferCreateInfo.layers          = 1;

        vkCreateFramebuffer(device, &framebufferCreateInfo, NULL, &framebuffers[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html#shader-modules */
static void graphics_createshaders()
{
    char   *vertBinary;
    size_t  vertSize;
    char   *fragBinary;
    size_t  fragSize;

    vertSize   = filesystem_fileread(&vertBinary, "shaders/triangle.vert.spv");
    fragSize   = filesystem_fileread(&fragBinary, "shaders/triangle.frag.spv");
    vertShader = graphics_createshader(vertBinary, vertSize);
    fragShader = graphics_createshader(fragBinary, fragSize);
    free(fragBinary);
    free(vertBinary);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html#pipelines-graphics */
static void graphics_creategraphicspipeline()
{
    VkPipeline                             graphicsPipeline;
    PFN_vkCreateGraphicsPipelines          vkCreateGraphicsPipelines;
    VkGraphicsPipelineCreateInfo           createInfo                 = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo        vertShaderStage            = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo        fragShaderStage            = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo        stages[2];
    VkPipelineVertexInputStateCreateInfo   vertexInput                = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly              = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo      viewport                   = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rasterization              = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo   multisample                = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendStateCreateInfo    colorBlend                 = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState    colorBlendAttachment       = { 0 };
    VkPipelineDynamicStateCreateInfo       dynamicState               = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    const VkDynamicState                   states[]                   = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineLayout                       pipelineLayout;
    PFN_vkCreatePipelineLayout             vkCreatePipelineLayout;
    VkPipelineLayoutCreateInfo             pipelineLayoutCreateInfo   = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

    vertShaderStage.stage                = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStage.module               = vertShader;
    vertShaderStage.pName                = "main";

    fragShaderStage.stage                = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStage.module               = fragShader;
    fragShaderStage.pName                = "main";

    stages[0]                            = vertShaderStage;
    stages[1]                            = fragShaderStage;

    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    viewport.viewportCount               = 1;
    viewport.scissorCount                = 1;

    rasterization.cullMode               = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace              = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth              = 1.0f;

    multisample.rasterizationSamples     = VK_SAMPLE_COUNT_1_BIT;

    colorBlendAttachment.colorWriteMask  = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    colorBlend.attachmentCount           = 1;
    colorBlend.pAttachments              = &colorBlendAttachment;

    dynamicState.dynamicStateCount       = 2;
    dynamicState.pDynamicStates          = states;

    vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vkGetDeviceProcAddr(device, "vkCreatePipelineLayout");
    vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NULL, &pipelineLayout);

    createInfo.stageCount                = 2;
    createInfo.pStages                   = stages;
    createInfo.pVertexInputState         = &vertexInput;
    createInfo.pInputAssemblyState       = &inputAssembly;
    createInfo.pViewportState            = &viewport;
    createInfo.pRasterizationState       = &rasterization;
    createInfo.pMultisampleState         = &multisample;
    createInfo.pColorBlendState          = &colorBlend;
    createInfo.pDynamicState             = &dynamicState;
    createInfo.layout                    = pipelineLayout;
    createInfo.renderPass                = renderPass;

    vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vkGetDeviceProcAddr(device, "vkCreateGraphicsPipelines");
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, NULL, &graphicsPipeline);

    graphics_destroyshader(vertShader);
    graphics_destroyshader(fragShader);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_surface */
static void graphics_createsurface()
{
    SDL_Vulkan_CreateSurface(window, instance, &surface);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_swapchain */
static void graphics_createswapchain()
{
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    VkSwapchainCreateInfoKHR swapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkExtent2D imageExtent;

    SDL_Vulkan_GetDrawableSize(window, &w, &h);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#VkSwapchainCreateInfoKHR */
    imageExtent.width  = w;
    imageExtent.height = h;

    swapchainCreateInfo.surface          = surface;
    swapchainCreateInfo.minImageCount    = 2;
    swapchainCreateInfo.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
    swapchainCreateInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainCreateInfo.imageExtent      = imageExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreateInfo.clipped          = VK_TRUE;
    swapchainCreateInfo.oldSwapchain     = VK_NULL_HANDLE;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkCreateSwapchainKHR */
    vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    vkCreateSwapchainKHR(device, &swapchainCreateInfo, NULL, &swapchain);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkGetSwapchainImagesKHR */
static void graphics_getswapchainimages()
{
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;

    vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, NULL);
    swapchainImages = malloc(sizeof(VkImage) * swapchainImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html#resources-image-views */
static void graphics_createimageviews()
{
    PFN_vkCreateImageView vkCreateImageView;
    size_t i;
    VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

    vkCreateImageView = (PFN_vkCreateImageView)vkGetDeviceProcAddr(device, "vkCreateImageView");

    createInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format                      = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.components.r                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a                = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.layerCount = 1;

    swapchainImageViews = malloc(sizeof(VkImageView) * swapchainImageCount);

    for (i = 0; i < swapchainImageCount; i++)
    {
        createInfo.image = swapchainImages[i];

        vkCreateImageView(device, &createInfo, NULL, &swapchainImageViews[i]);
    }
}

void graphics_init()
{
    void graphics_shutdown(void);

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
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html */
    graphics_createfence();
    graphics_createsemaphore();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createrenderpass();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap9.html */
    graphics_createshaders();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html */
    graphics_creategraphicspipeline();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html */
    graphics_createsurface();
    graphics_createswapchain();
    graphics_getswapchainimages();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap12.html */
    graphics_createimageviews();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html */
    graphics_createframebuffers();
    atexit(graphics_shutdown);
}

Shader graphics_createshader(const char *shader, size_t size)
{
    VkShaderModule shaderModule;
    PFN_vkCreateShaderModule vkCreateShaderModule;
    VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

    createInfo.codeSize = size;
    createInfo.pCode    = (const uint32_t *)shader;

    vkCreateShaderModule = (PFN_vkCreateShaderModule)vkGetDeviceProcAddr(device, "vkCreateShaderModule");
    vkCreateShaderModule(device, &createInfo, NULL, &shaderModule);

    return shaderModule;
}

void graphics_destroyshader(Shader shader)
{
    PFN_vkDestroyShaderModule vkDestroyShaderModule;

    vkDestroyShaderModule = (PFN_vkDestroyShaderModule)vkGetDeviceProcAddr(device, "vkDestroyShaderModule");
    vkDestroyShaderModule(device, shader, NULL);
}

void graphics_predraw()
{
    /* 6.4. Command Buffer Recording */
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    /* 8.4. Render Pass Commands */
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    VkRenderPassBeginInfo renderPassBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-recording */
    vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(device, "vkBeginCommandBuffer");
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    renderPassBegin.renderPass = renderPass;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-commands */
    vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass");
    vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
}

void graphics_postdraw()
{
}

void graphics_present()
{
}

void graphics_shutdown(void)
{
    PFN_vkDestroyImageView vkDestroyImageView;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
    PFN_vkDestroyDevice vkDestroyDevice;
    PFN_vkDestroyInstance vkDestroyInstance;

    vkDestroyImageView = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(device, "vkDestroyImageView");

    for (size_t i = 0; i < swapchainImageCount; i++)
    {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }

    vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    vkDestroySurfaceKHR(instance, surface, NULL);

    vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    vkDestroySwapchainKHR(device, swapchain, NULL);

    vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(device, "vkDestroyDevice");
    vkDestroyDevice(device, NULL);

    vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    vkDestroyInstance(instance, NULL);
}
