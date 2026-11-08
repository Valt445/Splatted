#pragma once
// Base: everything platform/boilerplate the project needs before the real
// work starts. Plain data + free functions, no classes.
//
//   base_init()            window, instance (+ validation), surface, device, queue,
//                          VMA, swapchain + depth buffer, per-frame sync
//   base_poll()            events, Esc to quit, frame timing, fps in the title
//   base_begin_frame()     wait -> acquire -> command buffer ready to record
//   base_begin_rendering() dynamic rendering into swapchain + depth, clear, viewport
//   base_end_rendering()
//   base_end_frame()       submit -> present (rebuilds the swapchain on resize)
//   base_shutdown()        destroys all of it in reverse order
//
// Plus small helpers: buffers (+ staged upload), sync2 barriers, shader loading.
//
// Note: Base must stay at one address after base_init (callbacks point at it),
// so create it once (e.g. on the stack in main) and pass it by reference.

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <glm/glm.hpp>                    
#include <glm/gtc/matrix_transform.hpp>  
#include <glm/gtc/quaternion.hpp>         
#include <glm/gtc/type_ptr.hpp>           // glm::value_ptr (pointer to the floats, for uploads)
#include <cgltf.h>
#include <vector>
struct GLFWwindow;

constexpr uint32_t BASE_MAX_SWAPCHAIN_IMAGES = 8;
constexpr uint32_t BASE_FRAMES_IN_FLIGHT     = 2;

struct BaseConfig {
    const char*      title        = "Splatter";
    uint32_t         width        = 1600;
    uint32_t         height       = 900;
    bool             validation   = true;                     // layers + sync validation + messenger
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR; // IMMEDIATE for benchmark mode
};

// Per frame-in-flight resources (owned by Base).
struct BaseFrameSync {
    VkCommandPool   pool           = VK_NULL_HANDLE;
    VkCommandBuffer cmd            = VK_NULL_HANDLE;
    VkFence         in_flight      = VK_NULL_HANDLE;   // signaled when the GPU is done with this slot
    VkSemaphore     image_acquired = VK_NULL_HANDLE;
};

// What base_begin_frame hands you for the current frame.
struct BaseFrame {
    VkCommandBuffer cmd         = VK_NULL_HANDLE;  // already begun, record into it
    uint32_t        frame_index = 0;               // 0 .. BASE_FRAMES_IN_FLIGHT-1 (index your per-frame data)
    uint32_t        image_index = 0;               // which swapchain image
    VkImage         image       = VK_NULL_HANDLE;
    VkImageView     view        = VK_NULL_HANDLE;
    VkExtent2D      extent      = {};
};

struct BaseBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void*         mapped     = nullptr;   // non-null for host-visible buffers (persistently mapped)
    VkDeviceSize  size       = 0;
};

struct Base {
    // Window
    GLFWwindow* window = nullptr;
    char        title[128] = {};

    // Instance
    VkInstance               instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface         = VK_NULL_HANDLE;

    // Device
    VkPhysicalDevice           physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpu_props       = {};   // gpu_props.limits.timestampPeriod = ns per tick
    VkDevice                   device          = VK_NULL_HANDLE;

    // One queue for everything (graphics + compute + transfer + present)
    VkQueue  queue                = VK_NULL_HANDLE;
    uint32_t queue_family         = 0;
    uint32_t queue_timestamp_bits = 0;    // 0 = timestamps unsupported on this queue

    // Memory
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Swapchain (rebuilt automatically on resize; swapchain_generation counts rebuilds)
    VkSwapchainKHR   swapchain             = VK_NULL_HANDLE;
    VkFormat         swapchain_format      = VK_FORMAT_UNDEFINED;
    VkExtent2D       swapchain_extent      = {};
    VkPresentModeKHR present_mode          = VK_PRESENT_MODE_FIFO_KHR; // what we actually got
    VkPresentModeKHR requested_present     = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t         swapchain_image_count = 0;
    uint32_t         swapchain_generation  = 0;
    VkImage          swapchain_images[BASE_MAX_SWAPCHAIN_IMAGES] = {};
    VkImageView      swapchain_views[BASE_MAX_SWAPCHAIN_IMAGES]  = {};
    VkSemaphore      render_finished[BASE_MAX_SWAPCHAIN_IMAGES]  = {};   // one per swapchain image
    bool             resized               = false;

    // Depth buffer (same size as the swapchain)
    VkFormat      depth_format     = VK_FORMAT_D32_SFLOAT;
    VkImage       depth_image      = VK_NULL_HANDLE;
    VmaAllocation depth_allocation = VK_NULL_HANDLE;
    VkImageView   depth_view       = VK_NULL_HANDLE;

    // Frames in flight
    BaseFrameSync frames[BASE_FRAMES_IN_FLIGHT] = {};
    uint64_t      frame_number = 0;     // total frames submitted

    // One-shot uploads
    VkCommandPool upload_pool  = VK_NULL_HANDLE;
    VkFence       upload_fence = VK_NULL_HANDLE;

    // Timing (updated by base_poll)
    double   time        = 0.0;   // seconds since init
    float    dt          = 0.0f;  // seconds since last base_poll (clamped to 0.1)
    double   fps_timer   = 0.0;
    uint32_t fps_frames  = 0;

    // Incremented by the debug messenger. Target: both stay 0.
    uint32_t validation_errors   = 0;
    uint32_t validation_warnings = 0;
};


// ---- Lifetime --------------------------------------------------------------

// Brings everything up. Returns false (after printing why) on failure.
bool base_init(Base& base, const BaseConfig& cfg);

// Waits for the device to go idle, then destroys everything base_init made.
// Destroy your own Vulkan objects before calling this.
void base_shutdown(Base& base);

// ---- Per frame -------------------------------------------------------------

// Pumps window events and updates time/dt/fps. Returns false once the window
// should close (window X or Esc).
bool base_poll(Base& base);

// Waits for this frame slot, acquires a swapchain image, begins the command
// buffer and transitions color + depth to attachment layouts.
// Returns false when there's nothing to draw this frame (minimized, or the
// swapchain was just rebuilt) -- skip the frame and loop again.
bool base_begin_frame(Base& base, BaseFrame& frame);

// Starts dynamic rendering into the swapchain image + depth buffer, clears
// color to (r,g,b,1) and depth to 1.0, and sets a full-window viewport/scissor.
void base_begin_rendering(const Base& base, const BaseFrame& frame, float r, float g, float b);
void base_end_rendering(const BaseFrame& frame);

// Transitions the image for present, submits, presents.
void base_end_frame(Base& base, const BaseFrame& frame);

// ---- Buffers ---------------------------------------------------------------

// host_visible = true : CPU-writable, persistently mapped (buffer.mapped). For small per-frame data.
// host_visible = false: device-local, fastest for the GPU. Fill it with base_upload.
BaseBuffer base_create_buffer(const Base& base, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible);
void       base_destroy_buffer(const Base& base, BaseBuffer& buffer);

// Copies size bytes into a (device-local) buffer through a staging buffer.
// Blocks until the copy is done; meant for load time, not per frame.
void base_upload(Base& base, const BaseBuffer& dst, const void* data, VkDeviceSize size, VkDeviceSize dst_offset = 0);

// ---- Barriers (synchronization2) -------------------------------------------

void base_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout old_layout, VkImageLayout new_layout,
                        VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

void base_memory_barrier(VkCommandBuffer cmd,
                         VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                         VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

// ---- Shaders ---------------------------------------------------------------

// Loads build/<preset>/shaders/<name>.spv (e.g. "splat.vert") into a shader module.
// Returns VK_NULL_HANDLE (after printing why) on failure.
VkShaderModule base_load_shader(const Base& base, const char* name);
