/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"

#define VK_NO_PROTOTYPES
#ifdef HAVE_VULKAN_H
#include <vulkan/vulkan.h>
#else
/* SDL includes a copy for building on systems without the Vulkan SDK */
#include "../lib/SDL2-2.26.1/src/video/khronos/vulkan/vulkan.h"
#endif
#include "SDL_vulkan.h"

/* 4.1. Command Function Pointers */
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

/* 4.2. Instances */
static VkInstance instance;

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

void graphics_init()
{
    /* https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap4.html */
    graphics_getcommandfunctionpointers();
    graphics_createinstance();
}
