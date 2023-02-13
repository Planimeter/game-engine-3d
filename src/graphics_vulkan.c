/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include <stdlib.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
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

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#initialization-functionpointers */
static void graphics_getcommandfunctionpointers()
{
    // FIXME: Separate SDL from this implementation.
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

    // FIXME: Separate SDL from this implementation.
    SDL_Vulkan_GetInstanceExtensions(window, &count, NULL);
    names = malloc(sizeof(char *) * count);
    SDL_Vulkan_GetInstanceExtensions(window, &count, (const char **)names);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html#VkInstanceCreateInfo */
    createInfo.enabledLayerCount       = 1;
    createInfo.ppEnabledLayerNames     = enabledLayerNames;
    createInfo.enabledExtensionCount   = count;
    createInfo.ppEnabledExtensionNames = (const char* const*)names;

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
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#devsandqueues-device-creation */
static void graphics_createdevice()
{
    PFN_vkCreateDevice vkCreateDevice;
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
    vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    vkCreateDevice(physicalDevices[0], &createInfo, NULL, &device);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html#vkGetDeviceQueue */
static void graphics_getqueue()
{
    PFN_vkGetDeviceQueue vkGetDeviceQueue;

    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetDeviceProcAddr(device, "vkGetDeviceQueue");
    vkGetDeviceQueue(device, 0, 0, &queue);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-pools */
static void graphics_createcommandpools()
{
    PFN_vkCreateCommandPool vkCreateCommandPool;
    VkCommandPoolCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    size_t i;

    commandPools = malloc(sizeof(VkCommandPool) * swapchainImageCount);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandPoolCreateInfo */
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkCreateCommandPool */
    vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetDeviceProcAddr(device, "vkCreateCommandPool");

    for (i = 0; i < swapchainImageCount; i++)
    {
        vkCreateCommandPool(device, &createInfo, NULL, &commandPools[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffer-allocation */
static void graphics_allocatecommandbuffers()
{
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    size_t i;

    commandBuffers = malloc(sizeof(VkCommandBuffer) * swapchainImageCount);

    for (i = 0; i < swapchainImageCount; i++)
    {
        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#VkCommandBufferAllocateInfo */
        allocateInfo.commandPool        = commandPools[i];
        allocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkAllocateCommandBuffers */
        vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers");
        vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffers[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-fences */
static void graphics_createfences()
{
    PFN_vkCreateFence vkCreateFence;
    VkFenceCreateInfo createInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    size_t i;

    fences = malloc(sizeof(VkFence) * swapchainImageCount);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#VkFenceCreateInfo */
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateFence */
    vkCreateFence = (PFN_vkCreateFence)vkGetDeviceProcAddr(device, "vkCreateFence");

    for (i = 0; i < swapchainImageCount; i++)
    {
        vkCreateFence(device, &createInfo, NULL, &fences[i]);
    }
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-semaphores */
static void graphics_createsemaphores()
{
    PFN_vkCreateSemaphore vkCreateSemaphore;
    VkSemaphoreCreateInfo createInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkCreateSemaphore */
    vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetDeviceProcAddr(device, "vkCreateSemaphore");
    vkCreateSemaphore(device, &createInfo, NULL, &acquireSemaphore);
    vkCreateSemaphore(device, &createInfo, NULL, &releaseSemaphore);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-creation */
static void graphics_createrenderpass()
{
    PFN_vkCreateRenderPass  vkCreateRenderPass;
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
    vkCreateRenderPass = (PFN_vkCreateRenderPass)vkGetDeviceProcAddr(device, "vkCreateRenderPass");
    vkCreateRenderPass(device, &createInfo, NULL, &renderPass);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#_framebuffers */
static void graphics_createframebuffers()
{
    PFN_vkCreateFramebuffer vkCreateFramebuffer;
    VkFramebufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    size_t i;

    framebuffers = malloc(sizeof(VkFramebuffer) * swapchainImageCount);
    vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetDeviceProcAddr(device, "vkCreateFramebuffer");

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
    // FIXME: Separate SDL from this implementation.
    SDL_Vulkan_CreateSurface(window, instance, &surface);
}

/* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#_wsi_swapchain */
static void graphics_createswapchain()
{
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkExtent2D imageExtent;

    // FIXME: Separate SDL from this implementation.
    SDL_Vulkan_GetDrawableSize(window, &w, &h);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#VkSwapchainCreateInfoKHR */
    imageExtent.width  = w;
    imageExtent.height = h;

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
    createInfo.oldSwapchain     = VK_NULL_HANDLE;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkCreateSwapchainKHR */
    vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
    vkCreateSwapchainKHR(device, &createInfo, NULL, &swapchain);
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
    VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    size_t i;

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

static graphics_destroyframebuffers()
{
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
    size_t i;

    vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetDeviceProcAddr(device, "vkDestroyFramebuffer");

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyFramebuffer(device, framebuffers[i], NULL);
    }
    free(framebuffers);
}

void graphics_init()
{
    void graphics_shutdown(void);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_getcommandfunctionpointers();
    graphics_createinstance();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap5.html */
    graphics_enumeratephysicaldevices();
    graphics_createdevice();
    graphics_getqueue();
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html */
    graphics_createsemaphores();
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

    /* 7.3. Fences */
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetFences vkResetFences;

    /* 8.4. Render Pass Commands */
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
    VkRenderPassBeginInfo renderPassBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };

    /* 10.10. Pipeline Binding */
    PFN_vkCmdBindPipeline vkCmdBindPipeline;

    /* 19.3. Clear Values */
    VkClearValue clearValue = {{0.01f, 0.01f, 0.033f, 1.0f}};

    /* 21.3. Programmable Primitive Shading */
    PFN_vkCmdDraw vkCmdDraw;

    /* 27.9. Controlling the Viewport */
    PFN_vkCmdSetViewport vkCmdSetViewport;
    VkViewport viewport = { 0 };

    /* 29.2. Scissor Test */
    PFN_vkCmdSetScissor vkCmdSetScissor;
    VkRect2D scissor = { 0 };

    /* 34.10. WSI Swapchain */
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap34.html#vkAcquireNextImageKHR */
    vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkWaitForFences */
    vkWaitForFences = (PFN_vkWaitForFences)vkGetDeviceProcAddr(device, "vkWaitForFences");
    vkWaitForFences(device, 1, &fences[imageIndex], VK_TRUE, UINT64_MAX);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#vkResetFences */
    vkResetFences = (PFN_vkResetFences)vkGetDeviceProcAddr(device, "vkResetFences");
    vkResetFences(device, 1, &fences[imageIndex]);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#commandbuffers-recording */
    vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(device, "vkBeginCommandBuffer");
    vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

    renderPassBegin.renderPass               = renderPass;
    renderPassBegin.framebuffer              = framebuffers[imageIndex];
    renderPassBegin.renderArea.extent.width  = w;
    renderPassBegin.renderArea.extent.height = h;
    renderPassBegin.clearValueCount          = 1;
    renderPassBegin.pClearValues             = &clearValue;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#renderpass-commands */
    vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass");
    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap10.html#pipelines-binding */
    vkCmdBindPipeline = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(device, "vkCmdBindPipeline");
    vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    viewport.width    = w;
    viewport.height   = h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap27.html#vertexpostproc-viewport */
    vkCmdSetViewport = (PFN_vkCmdSetViewport)vkGetDeviceProcAddr(device, "vkCmdSetViewport");
    vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &viewport);

    scissor.extent.width  = w;
    scissor.extent.height = h;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap29.html#fragops-scissor */
    vkCmdSetScissor = (PFN_vkCmdSetScissor)vkGetDeviceProcAddr(device, "vkCmdSetScissor");
    vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &scissor);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap21.html#vkCmdDraw */
    vkCmdDraw = (PFN_vkCmdDraw)vkGetDeviceProcAddr(device, "vkCmdDraw");
    vkCmdDraw(commandBuffers[imageIndex], 3, 1, 0, 0);
}

void graphics_postdraw()
{
    /* 8.4. Render Pass Commands */
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass;

    /* 6.4. Command Buffer Recording */
    PFN_vkEndCommandBuffer vkEndCommandBuffer;

    /* 6.5. Command Buffer Submission */
    PFN_vkQueueSubmit vkQueueSubmit;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    /* 7.1.2. Pipeline Stages */
    VkPipelineStageFlags waitStage = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap8.html#vkCmdEndRenderPass */
    vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(device, "vkCmdEndRenderPass");
    vkCmdEndRenderPass(commandBuffers[imageIndex]);

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkEndCommandBuffer */
    vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(device, "vkEndCommandBuffer");
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &commandBuffers[imageIndex];
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &acquireSemaphore;
    submit.pWaitDstStageMask    = &waitStage;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &releaseSemaphore;

    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap6.html#vkQueueSubmit */
    vkQueueSubmit = (PFN_vkQueueSubmit)vkGetDeviceProcAddr(device, "vkQueueSubmit");
    vkQueueSubmit(queue, 1, &submit, fences[imageIndex]);
}

void graphics_present()
{
    PFN_vkQueuePresentKHR vkQueuePresentKHR;
    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &swapchain;
    presentInfo.pImageIndices      = &imageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &releaseSemaphore;

    vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
    vkQueuePresentKHR(queue, &presentInfo);
}

void graphics_resize()
{
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

    if (device == VK_NULL_HANDLE)
    {
        return;
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevices[0], surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width  == w &&
        surfaceCapabilities.currentExtent.height == h)
    {
        return;
    }

    vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
    vkDeviceWaitIdle(device);
}

void graphics_shutdown(void)
{
    PFN_vkDeviceWaitIdle        vkDeviceWaitIdle;
    PFN_vkDestroyImageView      vkDestroyImageView;
    size_t                      i;
    PFN_vkDestroySurfaceKHR     vkDestroySurfaceKHR;
    PFN_vkDestroySwapchainKHR   vkDestroySwapchainKHR;
    PFN_vkDestroyPipeline       vkDestroyPipeline;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
    PFN_vkDestroyRenderPass     vkDestroyRenderPass;
    PFN_vkDestroySemaphore      vkDestroySemaphore;
    PFN_vkDestroyFence          vkDestroyFence;
    PFN_vkFreeCommandBuffers    vkFreeCommandBuffers;
    PFN_vkDestroyCommandPool    vkDestroyCommandPool;
    PFN_vkDestroyDevice         vkDestroyDevice;
    PFN_vkDestroyInstance       vkDestroyInstance;

    vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetDeviceProcAddr(device, "vkDeviceWaitIdle");
    vkDeviceWaitIdle(device);

    graphics_destroyframebuffers();

    vkDestroyImageView = (PFN_vkDestroyImageView)vkGetDeviceProcAddr(device, "vkDestroyImageView");

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }
    free(swapchainImageViews);

    vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
    vkDestroySwapchainKHR(device, swapchain, NULL);

    vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    vkDestroySurfaceKHR(instance, surface, NULL);

    vkDestroyPipeline = (PFN_vkDestroyPipeline)vkGetDeviceProcAddr(device, "vkDestroyPipeline");
    vkDestroyPipeline(device, graphicsPipeline, NULL);

    vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)vkGetDeviceProcAddr(device, "vkDestroyPipelineLayout");
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);

    vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetDeviceProcAddr(device, "vkDestroyRenderPass");
    vkDestroyRenderPass(device, renderPass, NULL);

    vkDestroySemaphore = (PFN_vkDestroySemaphore)vkGetDeviceProcAddr(device, "vkDestroySemaphore");
    vkDestroySemaphore(device, releaseSemaphore, NULL);
    vkDestroySemaphore(device, acquireSemaphore, NULL);

    vkDestroyFence = (PFN_vkDestroyFence)vkGetDeviceProcAddr(device, "vkDestroyFence");

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkDestroyFence(device, fences[i], NULL);
    }
    free(fences);

    for (i = swapchainImageCount; i-- > 0;)
    {
        vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vkGetDeviceProcAddr(device, "vkFreeCommandBuffers");
        vkFreeCommandBuffers(device, commandPools[i], 1, &commandBuffers[i]);

        vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetDeviceProcAddr(device, "vkDestroyCommandPool");
        vkDestroyCommandPool(device, commandPools[i], NULL);
    }
    free(commandPools);
    free(commandBuffers);

    vkDestroyDevice = (PFN_vkDestroyDevice)vkGetDeviceProcAddr(device, "vkDestroyDevice");
    vkDestroyDevice(device, NULL);

    free(physicalDevices);

    vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    vkDestroyInstance(instance, NULL);
}
