/* Copyright Planimeter. All Rights Reserved. */

#ifndef WINDOW_H
#define WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Window;

/* https://github.com/libsdl-org/SDL/blob/release-2.26.3/include/SDL_vulkan.h#L39-L58 */
/* Avoid including vulkan.h, don't define VkInstance if it's already included */
#ifdef VULKAN_H_
#define NO_VULKAN_TYPEDEFS
#endif
#ifndef NO_VULKAN_TYPEDEFS
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
#else
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
#endif

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)

#endif /* !NO_VULKAN_TYPEDEFS */

typedef VkInstance vulkanInstance;
typedef VkSurfaceKHR vulkanSurface; /* for compatibility with Tizen */

void   window_init();
Window window_getwindow();
void   window_vulkan_createsurface(VkInstance instance, VkSurfaceKHR* surface);
void   window_vulkan_getdrawablesize(int *w, int *h);
void   window_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_H */
