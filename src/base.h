#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cgltf.h>
#include <vector>
struct GLFWwindow;

constexpr uint32_t BASE_MAX_SWAPCHAIN_IMAGES = 8;
constexpr uint32_t BASE_FRAMES_IN_FLIGHT     = 2;

struct BaseConfig {
    const char*      title        = "Splatter";
    uint32_t         width        = 1600;
    uint32_t         height       = 900;
    bool             validation   = true;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};


struct BaseFrameSync {
    VkCommandPool   pool           = VK_NULL_HANDLE;
    VkCommandBuffer cmd            = VK_NULL_HANDLE;
    VkFence         in_flight      = VK_NULL_HANDLE;
    VkSemaphore     image_acquired = VK_NULL_HANDLE;
};

struct BaseFrame {
    VkCommandBuffer cmd         = VK_NULL_HANDLE;
    uint32_t        frame_index = 0;
    uint32_t        image_index = 0;
    VkImage         image       = VK_NULL_HANDLE;
    VkImageView     view        = VK_NULL_HANDLE;
    VkExtent2D      extent      = {};
};

struct BaseBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void*         mapped     = nullptr;
	VkDeviceSize  size = 0;
};

struct Base {
    GLFWwindow* window = nullptr;
    char        title[128] = {};

    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface         = VK_NULL_HANDLE;

    VkPhysicalDevice           physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpu_props       = {};
    VkDevice                   device          = VK_NULL_HANDLE;

    VkQueue  queue                = VK_NULL_HANDLE;
    uint32_t queue_family         = 0;
    uint32_t queue_timestamp_bits = 0;

    VmaAllocator allocator = VK_NULL_HANDLE;

    VkSwapchainKHR   swapchain             = VK_NULL_HANDLE;
    VkFormat         swapchain_format      = VK_FORMAT_UNDEFINED;
    VkExtent2D       swapchain_extent      = {};
    VkPresentModeKHR present_mode          = VK_PRESENT_MODE_FIFO_KHR;
    VkPresentModeKHR requested_present     = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t         swapchain_image_count = 0;
    uint32_t         swapchain_generation  = 0;
    VkImage          swapchain_images[BASE_MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView      swapchain_views[BASE_MAX_SWAPCHAIN_IMAGES]  = {};
    VkSemaphore      render_finished[BASE_MAX_SWAPCHAIN_IMAGES]  = {};
    bool             resized               = false;

    VkFormat      depth_format     = VK_FORMAT_D32_SFLOAT;
    VkImage       depth_image      = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView   depth_view       = VK_NULL_HANDLE;

    BaseFrameSync frames[BASE_FRAMES_IN_FLIGHT] = {};
    uint64_t      frame_number = 0;

    VkCommandPool upload_pool  = VK_NULL_HANDLE;
    VkFence       upload_fence = VK_NULL_HANDLE;

    double   time        = 0.0;
    float    dt          = 0.0f;
    double   fps_timer   = 0.0;
    uint32_t fps_frames  = 0;

    uint32_t validation_errors   = 0;
    uint32_t validation_warnings = 0;
};


bool base_init(Base& base, const BaseConfig& cfg);


void base_shutdown(Base& base);


bool base_poll(Base& base);

bool base_begin_frame(Base& base, BaseFrame& frame);

void base_begin_rendering(const Base& base, const BaseFrame& frame, float r, float g, float b);
void base_end_rendering(const BaseFrame& frame);

void base_end_frame(Base& base, const BaseFrame& frame);


BaseBuffer base_create_buffer(const Base& base, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible);
void       base_destroy_buffer(const Base& base, BaseBuffer& buffer);

void base_upload(Base& base, const BaseBuffer& dst, const void* data, VkDeviceSize size, VkDeviceSize dst_offset = 0);


void base_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout old_layout, VkImageLayout new_layout,
                        VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

void base_memory_barrier(VkCommandBuffer cmd,
                         VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                         VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);



VkShaderModule base_load_shader(const Base& base, const char* name);
