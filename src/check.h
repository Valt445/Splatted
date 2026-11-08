#pragma once

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>

inline const char* vk_result_str(VkResult r)
{
    switch (r) {
    case VK_SUCCESS:                        return "VK_SUCCESS";
    case VK_NOT_READY:                      return "VK_NOT_READY";
    case VK_TIMEOUT:                        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
    default:                                return "VkResult(unknown)";
    }
}

#define VK_CHECK(expr)                                                         \
    do {                                                                       \
        VkResult vk_check_result_ = (expr);                                    \
        if (vk_check_result_ != VK_SUCCESS) {                                  \
            std::fprintf(stderr, "[vk] %s failed: %s (%d)\n    at %s:%d\n",   \
                         #expr, vk_result_str(vk_check_result_),               \
                         (int)vk_check_result_, __FILE__, __LINE__);           \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[fatal] %s\n    at %s:%d\n", (msg),          \
                         __FILE__, __LINE__);                                  \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
