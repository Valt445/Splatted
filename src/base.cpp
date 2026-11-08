// Base implementation. vk-bootstrap is used only inside this file: Base itself
// stores plain Vulkan handles, nothing from vkb.

#include "base.h"
#include "check.h"
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===========================================================================
// Debug messenger
// ===========================================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       user)
{
    Base& base = *static_cast<Base*>(user);

    const char* tag = "info";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        tag = "ERROR";
        base.validation_errors++;
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        tag = "warn";
        base.validation_warnings++;
    }

    const char* kind = (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  ? "validation"
                     : (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "perf"
                                                                                : "general";

    std::fprintf(stderr, "[vk %s/%s] %s\n\n", tag, kind, data->pMessage);
    return VK_FALSE; // never abort the call that triggered the message
}

static void framebuffer_size_callback(GLFWwindow* window, int, int)
{
    Base* base = static_cast<Base*>(glfwGetWindowUserPointer(window));
    if (base) base->resized = true;
}

static const char* present_mode_str(VkPresentModeKHR m)
{
    switch (m) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default:                               return "other";
    }
}

// ===========================================================================
// Init steps (each one does exactly one thing)
// ===========================================================================
static bool init_window(Base& base, const BaseConfig& cfg)
{
    if (!glfwInit()) {
        std::fprintf(stderr, "[base] glfwInit failed\n");
        return false;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[base] GLFW reports no Vulkan support (loader/ICD missing?)\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan, not OpenGL
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);     // swapchain is rebuilt on resize

    std::snprintf(base.title, sizeof(base.title), "%s", cfg.title);
    base.window = glfwCreateWindow((int)cfg.width, (int)cfg.height, base.title, nullptr, nullptr);
    if (!base.window) {
        std::fprintf(stderr, "[base] glfwCreateWindow failed\n");
        return false;
    }
    glfwSetWindowUserPointer(base.window, &base);
    glfwSetFramebufferSizeCallback(base.window, framebuffer_size_callback);
    return true;
}

static bool init_instance(Base& base, const BaseConfig& cfg, vkb::Instance& out)
{
    vkb::InstanceBuilder builder;
    builder.set_app_name(cfg.title)
           .set_engine_name("none")
           .require_api_version(1, 3, 0);

    // GLFW tells us which surface extensions this platform (X11/Wayland/Win32) needs.
    uint32_t     ext_count = 0;
    const char** exts      = glfwGetRequiredInstanceExtensions(&ext_count);
    for (uint32_t i = 0; i < ext_count; i++)
        builder.enable_extension(exts[i]);

    if (cfg.validation) {
        builder.enable_validation_layers(true)   // required: fail loudly if missing
               .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT)
               .set_debug_callback(debug_callback)
               .set_debug_callback_user_data_pointer(&base)
               .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
               .set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
    }

    auto result = builder.build();
    if (!result) {
        std::fprintf(stderr, "[base] instance: %s\n", result.error().message().c_str());
        if (cfg.validation)
            std::fprintf(stderr, "       (validation layers installed? Windows: Vulkan SDK, Fedora:"
                                 " vulkan-validation-layers; or run with --no-validation)\n");
        return false;
    }

    out                  = result.value();
    base.instance        = out.instance;
    base.debug_messenger = out.debug_messenger;
    return true;
}

static bool init_surface(Base& base)
{
    VkResult r = glfwCreateWindowSurface(base.instance, base.window, nullptr, &base.surface);
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[base] glfwCreateWindowSurface: %s\n", vk_result_str(r));
        return false;
    }
    return true;
}

static bool init_device(Base& base, const vkb::Instance& vkb_instance)
{
    // Vulkan 1.3 core features we rely on.
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;   // no render passes / framebuffers
    features13.synchronization2 = VK_TRUE;   // vkCmdPipelineBarrier2, vkQueueSubmit2

    vkb::PhysicalDeviceSelector selector{vkb_instance};
    auto phys = selector.set_minimum_version(1, 3)
                        .set_required_features_13(features13)
                        .set_surface(base.surface)
                        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                        .select();
    if (!phys) {
        std::fprintf(stderr, "[base] no suitable GPU: %s\n", phys.error().message().c_str());
        return false;
    }

    auto dev = vkb::DeviceBuilder{phys.value()}.build();
    if (!dev) {
        std::fprintf(stderr, "[base] device: %s\n", dev.error().message().c_str());
        return false;
    }

    vkb::Device& d       = dev.value();
    base.physical_device = d.physical_device.physical_device;
    base.gpu_props       = d.physical_device.properties;
    base.device          = d.device;

    // One queue does everything: graphics + compute + transfer + present.
    auto queue_index = d.get_queue_index(vkb::QueueType::graphics);
    auto queue       = d.get_queue(vkb::QueueType::graphics);
    if (!queue_index || !queue) {
        std::fprintf(stderr, "[base] no graphics queue\n");
        return false;
    }
    base.queue_family = queue_index.value();
    base.queue        = queue.value();

    auto families = d.physical_device.get_queue_families();
    const VkQueueFamilyProperties& family = families[base.queue_family];
    if (!(family.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        std::fprintf(stderr, "[base] graphics queue family has no compute support\n");
        return false;
    }
    base.queue_timestamp_bits = family.timestampValidBits;

    VkBool32 can_present = VK_FALSE;
    VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(base.physical_device, base.queue_family,
                                                  base.surface, &can_present));
    if (!can_present) {
        std::fprintf(stderr, "[base] graphics queue family cannot present to this surface\n");
        return false;
    }
    return true;
}

static bool init_allocator(Base& base)
{
    VmaAllocatorCreateInfo info = {};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.instance         = base.instance;
    info.physicalDevice   = base.physical_device;
    info.device           = base.device;
    VK_CHECK(vmaCreateAllocator(&info, &base.allocator));
    return true;
}

// ---------------------------------------------------------------------------
// Swapchain + depth + per-image semaphores (created together, rebuilt together)
// ---------------------------------------------------------------------------
static void create_depth(Base& base)
{
    VkImageCreateInfo image = {};
    image.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType     = VK_IMAGE_TYPE_2D;
    image.format        = base.depth_format;
    image.extent        = {base.swapchain_extent.width, base.swapchain_extent.height, 1};
    image.mipLevels     = 1;
    image.arrayLayers   = 1;
    image.samples       = VK_SAMPLE_COUNT_1_BIT;
    image.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc = {};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    VK_CHECK(vmaCreateImage(base.allocator, &image, &alloc, &base.depth_image, &base.depth_allocation, nullptr));

    VkImageViewCreateInfo view = {};
    view.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image            = base.depth_image;
    view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    view.format           = base.depth_format;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(base.device, &view, nullptr, &base.depth_view));
}

static bool create_swapchain(Base& base, VkSwapchainKHR old_swapchain)
{
    // Framebuffer size can differ from window size on HiDPI screens.
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(base.window, &fb_w, &fb_h);

    VkSurfaceFormatKHR format = {};
    format.format     = VK_FORMAT_B8G8R8A8_UNORM;          // colors land on screen exactly as written
    format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    auto result = vkb::SwapchainBuilder{base.physical_device, base.device, base.surface,
                                        base.queue_family, base.queue_family}
                      .set_desired_format(format)
                      .set_desired_present_mode(base.requested_present)
                      .set_desired_extent((uint32_t)fb_w, (uint32_t)fb_h)
                      .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                      .set_old_swapchain(old_swapchain)
                      .build();
    if (!result) {
        std::fprintf(stderr, "[base] swapchain: %s\n", result.error().message().c_str());
        return false;
    }

    vkb::Swapchain sc = result.value();
    auto images = sc.get_images();
    auto views  = sc.get_image_views();
    if (!images || !views) {
        std::fprintf(stderr, "[base] swapchain images/views\n");
        return false;
    }
    if (images.value().size() > BASE_MAX_SWAPCHAIN_IMAGES) {
        std::fprintf(stderr, "[base] too many swapchain images (%zu)\n", images.value().size());
        return false;
    }

    base.swapchain             = sc.swapchain;
    base.swapchain_format      = sc.image_format;
    base.swapchain_extent      = sc.extent;
    base.present_mode          = sc.present_mode;
    base.swapchain_image_count = (uint32_t)images.value().size();
    for (uint32_t i = 0; i < base.swapchain_image_count; i++) {
        base.swapchain_images[i] = images.value()[i];
        base.swapchain_views[i]  = views.value()[i];

        VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(base.device, &sem, nullptr, &base.render_finished[i]));
    }

    create_depth(base);
    return true;
}

static void destroy_swapchain_resources(Base& base)
{
    if (base.depth_view)  vkDestroyImageView(base.device, base.depth_view, nullptr);
    if (base.depth_image) vmaDestroyImage(base.allocator, base.depth_image, base.depth_allocation);
    base.depth_view  = VK_NULL_HANDLE;
    base.depth_image = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < base.swapchain_image_count; i++) {
        vkDestroyImageView(base.device, base.swapchain_views[i], nullptr);
        vkDestroySemaphore(base.device, base.render_finished[i], nullptr);
        base.swapchain_views[i]  = VK_NULL_HANDLE;
        base.render_finished[i]  = VK_NULL_HANDLE;
        base.swapchain_images[i] = VK_NULL_HANDLE;
    }
    base.swapchain_image_count = 0;
}

static void rebuild_swapchain(Base& base)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(base.window, &w, &h);
    if (w == 0 || h == 0) return;   // minimized: try again later

    vkDeviceWaitIdle(base.device);

    VkSwapchainKHR old = base.swapchain;
    destroy_swapchain_resources(base);
    CHECK(create_swapchain(base, old), "swapchain rebuild failed");
    vkDestroySwapchainKHR(base.device, old, nullptr);

    base.resized = false;
    base.swapchain_generation++;
}



static void init_frames(Base& base)
{
    for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++) {
        BaseFrameSync& f = base.frames[i];

        VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = base.queue_family;
        VK_CHECK(vkCreateCommandPool(base.device, &pool, nullptr, &f.pool));

        VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool        = f.pool;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(base.device, &alloc, &f.cmd));

        VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // first wait passes immediately
        VK_CHECK(vkCreateFence(base.device, &fence, nullptr, &f.in_flight));

        VkSemaphoreCreateInfo sem = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(base.device, &sem, nullptr, &f.image_acquired));
    }

    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = base.queue_family;
    VK_CHECK(vkCreateCommandPool(base.device, &pool, nullptr, &base.upload_pool));

    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(base.device, &fence, nullptr, &base.upload_fence));
}

static void print_summary(const Base& base, const BaseConfig& cfg)
{
    const VkPhysicalDeviceProperties& p = base.gpu_props;
    std::printf("[base] GPU        %s (Vulkan %u.%u.%u)\n", p.deviceName,
                VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
                VK_API_VERSION_PATCH(p.apiVersion));
    std::printf("[base] queue      family %u, timestamps %s (%u bits, %.3f ns/tick)\n",
                base.queue_family, base.queue_timestamp_bits ? "yes" : "NO",
                base.queue_timestamp_bits, p.limits.timestampPeriod);
    std::printf("[base] swapchain  %ux%u, %u images, present %s\n",
                base.swapchain_extent.width, base.swapchain_extent.height,
                base.swapchain_image_count, present_mode_str(base.present_mode));
    if (base.present_mode != cfg.present_mode)
        std::printf("[base] note       asked for %s, driver gave %s\n",
                    present_mode_str(cfg.present_mode), present_mode_str(base.present_mode));
    std::printf("[base] validation %s\n", cfg.validation ? "ON (+ sync validation)" : "off");
    std::fflush(stdout);
}

// ===========================================================================
// Public API: lifetime
// ===========================================================================
bool base_init(Base& base, const BaseConfig& cfg)
{
    vkb::Instance vkb_instance;
    base.requested_present = cfg.present_mode;

    if (!init_window(base, cfg))                  return false;
    if (!init_instance(base, cfg, vkb_instance))  return false;
    if (!init_surface(base))                      return false;
    if (!init_device(base, vkb_instance))         return false;
    if (!init_allocator(base))                    return false;
    if (!create_swapchain(base, VK_NULL_HANDLE))  return false;
    init_frames(base);

    
    base.time = glfwGetTime();
    print_summary(base, cfg);
    return true;
}

void base_shutdown(Base& base)
{
    if (base.device)
        vkDeviceWaitIdle(base.device);

    if (base.device) {
        for (uint32_t i = 0; i < BASE_FRAMES_IN_FLIGHT; i++) {
            BaseFrameSync& f = base.frames[i];
            if (f.image_acquired) vkDestroySemaphore(base.device, f.image_acquired, nullptr);
            if (f.in_flight)      vkDestroyFence(base.device, f.in_flight, nullptr);
            if (f.pool)           vkDestroyCommandPool(base.device, f.pool, nullptr);
        }
        if (base.upload_fence) vkDestroyFence(base.device, base.upload_fence, nullptr);
        if (base.upload_pool)  vkDestroyCommandPool(base.device, base.upload_pool, nullptr);

        destroy_swapchain_resources(base);
        if (base.swapchain) vkDestroySwapchainKHR(base.device, base.swapchain, nullptr);
    }
    if (base.allocator)       vmaDestroyAllocator(base.allocator);
    if (base.device)          vkDestroyDevice(base.device, nullptr);
    if (base.surface)         vkDestroySurfaceKHR(base.instance, base.surface, nullptr);
    if (base.debug_messenger) vkb::destroy_debug_utils_messenger(base.instance, base.debug_messenger);
    if (base.instance)        vkDestroyInstance(base.instance, nullptr);
    if (base.window)          glfwDestroyWindow(base.window);
    glfwTerminate();

    if (base.validation_errors || base.validation_warnings)
        std::printf("[base] validation: %u errors, %u warnings\n",
                    base.validation_errors, base.validation_warnings);
    else
        std::printf("[base] validation: clean\n");

    base = Base{};
}

// ===========================================================================
// Public API: per frame
// ===========================================================================
bool base_poll(Base& base)
{
    glfwPollEvents();
    if (glfwGetKey(base.window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(base.window, GLFW_TRUE);

    double now = glfwGetTime();
    double dt  = now - base.time;
    base.time  = now;
    base.dt    = (float)(dt > 0.1 ? 0.1 : dt);   // clamp so a hitch doesn't teleport things

    base.fps_timer += dt;
    base.fps_frames++;
    if (base.fps_timer >= 0.5) {
        double fps = base.fps_frames / base.fps_timer;
        char   title[192];
        std::snprintf(title, sizeof(title), "%s  |  %.0f fps  (%.2f ms)", base.title, fps, 1000.0 / fps);
        glfwSetWindowTitle(base.window, title);
        base.fps_timer  = 0.0;
        base.fps_frames = 0;
    }

    return !glfwWindowShouldClose(base.window);
}

bool base_begin_frame(Base& base, BaseFrame& frame)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(base.window, &w, &h);
    if (w == 0 || h == 0) {             // minimized: don't spin the CPU
        glfwWaitEventsTimeout(0.1);
        return false;
    }
    if (base.resized) {
        rebuild_swapchain(base);
        return false;
    }

    uint32_t       slot = (uint32_t)(base.frame_number % BASE_FRAMES_IN_FLIGHT);
    BaseFrameSync& f    = base.frames[slot];

    VK_CHECK(vkWaitForFences(base.device, 1, &f.in_flight, VK_TRUE, UINT64_MAX));

    uint32_t image_index = 0;
    VkResult r = vkAcquireNextImageKHR(base.device, base.swapchain, UINT64_MAX,
                                       f.image_acquired, VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild_swapchain(base);
        return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        VK_CHECK(r);

    VK_CHECK(vkResetFences(base.device, 1, &f.in_flight));
    VK_CHECK(vkResetCommandPool(base.device, f.pool, 0));

    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &begin));

    // Swapchain image: contents don't matter (we clear), so from UNDEFINED.
    // Waits on the acquire semaphore at COLOR_ATTACHMENT_OUTPUT (see end_frame).
    base_image_barrier(f.cmd, base.swapchain_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);

    // Depth is shared by all frames in flight: wait for the previous frame's depth writes.
    const VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    base_image_barrier(f.cmd, base.depth_image, VK_IMAGE_ASPECT_DEPTH_BIT,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                       depth_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                       depth_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    frame.cmd         = f.cmd;
    frame.frame_index = slot;
    frame.image_index = image_index;
    frame.image       = base.swapchain_images[image_index];
    frame.view        = base.swapchain_views[image_index];
    frame.extent      = base.swapchain_extent;
    return true;
}

void base_begin_rendering(const Base& base, const BaseFrame& frame, float r, float g, float b)
{
    VkRenderingAttachmentInfo color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = frame.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{r, g, b, 1.0f}};

    VkRenderingAttachmentInfo depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView   = base.depth_view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea           = {{0, 0}, frame.extent};
    info.layerCount           = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments    = &color;
    info.pDepthAttachment     = &depth;
    vkCmdBeginRendering(frame.cmd, &info);

    VkViewport viewport = {0.0f, 0.0f, (float)frame.extent.width, (float)frame.extent.height, 0.0f, 1.0f};
    VkRect2D   scissor  = {{0, 0}, frame.extent};
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
}

void base_end_rendering(const BaseFrame& frame)
{
    vkCmdEndRendering(frame.cmd);
}

void base_end_frame(Base& base, const BaseFrame& frame)
{
    BaseFrameSync& f = base.frames[frame.frame_index];

    base_image_barrier(frame.cmd, frame.image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);   // keeps the layout transition inside
                                                                   // the render_finished signal's scope
    VK_CHECK(vkEndCommandBuffer(frame.cmd));

    VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.image_acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = base.render_finished[frame.image_index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmd = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd.commandBuffer = frame.cmd;

    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount   = 1;
    submit.pWaitSemaphoreInfos      = &wait;
    submit.commandBufferInfoCount   = 1;
    submit.pCommandBufferInfos      = &cmd;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos    = &signal;
    VK_CHECK(vkQueueSubmit2(base.queue, 1, &submit, f.in_flight));

    VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &base.render_finished[frame.image_index];
    present.swapchainCount     = 1;
    present.pSwapchains        = &base.swapchain;
    present.pImageIndices      = &frame.image_index;

    VkResult r = vkQueuePresentKHR(base.queue, &present);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        base.resized = true;            // rebuilt at the start of the next frame
    else
        VK_CHECK(r);

    base.frame_number++;
}

// ===========================================================================
// Public API: buffers
// ===========================================================================
BaseBuffer base_create_buffer(const Base& base, VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible)
{
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size  = size;
    info.usage = usage;
    if (!host_visible)
        info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;   // so base_upload can fill it

    VmaAllocationCreateInfo alloc = {};
    if (host_visible) {
        alloc.usage = VMA_MEMORY_USAGE_AUTO;
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    BaseBuffer        buffer = {};
    VmaAllocationInfo out    = {};
    VK_CHECK(vmaCreateBuffer(base.allocator, &info, &alloc, &buffer.buffer, &buffer.allocation, &out));
    buffer.mapped = out.pMappedData;
    buffer.size   = size;
    return buffer;
}

void base_destroy_buffer(const Base& base, BaseBuffer& buffer)
{
    if (buffer.buffer)
        vmaDestroyBuffer(base.allocator, buffer.buffer, buffer.allocation);
    buffer = BaseBuffer{};
}

void base_upload(Base& base, const BaseBuffer& dst, const void* data, VkDeviceSize size, VkDeviceSize dst_offset)
{
    CHECK(dst_offset + size <= dst.size, "base_upload: write past the end of the buffer");

    BaseBuffer staging = base_create_buffer(base, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data, (size_t)size);
    VK_CHECK(vmaFlushAllocation(base.allocator, staging.allocation, 0, VK_WHOLE_SIZE));

    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool        = base.upload_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(base.device, &alloc, &cmd));

    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    VkBufferCopy copy = {0, dst_offset, size};
    vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &copy);

    // Make the copy visible to everything submitted after this.
    base_memory_barrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd;
    VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos    = &cmd_info;
    VK_CHECK(vkQueueSubmit2(base.queue, 1, &submit, base.upload_fence));
    VK_CHECK(vkWaitForFences(base.device, 1, &base.upload_fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(base.device, 1, &base.upload_fence));

    vkFreeCommandBuffers(base.device, base.upload_pool, 1, &cmd);
    base_destroy_buffer(base, staging);
}

// ===========================================================================
// Public API: barriers
// ===========================================================================
void base_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout old_layout, VkImageLayout new_layout,
                        VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask        = src_stage;
    b.srcAccessMask       = src_access;
    b.dstStageMask        = dst_stage;
    b.dstAccessMask       = dst_access;
    b.oldLayout           = old_layout;
    b.newLayout           = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void base_memory_barrier(VkCommandBuffer cmd,
                         VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                         VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
    VkMemoryBarrier2 b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask  = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask  = dst_stage;
    b.dstAccessMask = dst_access;

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// ===========================================================================
// Public API: shaders
// ===========================================================================
VkShaderModule base_load_shader(const Base& base, const char* name)
{
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.spv", SHADER_DIR, name);

    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[base] can't open shader %s\n", path);
        return VK_NULL_HANDLE;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size <= 0 || size % 4 != 0) {
        std::fprintf(stderr, "[base] bad SPIR-V size (%ld) in %s\n", size, path);
        std::fclose(f);
        return VK_NULL_HANDLE;
    }

    uint32_t* code = static_cast<uint32_t*>(std::malloc((size_t)size));
    size_t    read = std::fread(code, 1, (size_t)size, f);
    std::fclose(f);
    if (read != (size_t)size) {
        std::fprintf(stderr, "[base] short read on %s\n", path);
        std::free(code);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = (size_t)size;
    info.pCode    = code;

    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(base.device, &info, nullptr, &module));
    std::free(code);
    return module;
}
