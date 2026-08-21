#ifndef VOLK_H
#define VOLK_H

// Mali-G57 fallback: use NDK Vulkan headers directly instead of volk loader
// volk is just a dynamic Vulkan loader; on Android we can use the NDK headers

#include <vulkan/vulkan.h>

// volk is typically used for dynamic loading, but on Android
// Vulkan is always available via libvulkan.so, so we just use the static headers

#endif // VOLK_H
