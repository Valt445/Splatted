#pragma once
// Fly camera. Plain struct + free functions.
//
// Controls:
//   hold right mouse   look around
//   W A S D            move
//   Space / E          up       Ctrl / Q   down
//   Shift              4x faster
//
// World is +Y up (matches glTF). Projection is Vulkan-style: depth 0..1, Y down.

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct GLFWwindow;

struct Camera {
    glm::vec3 position    = {0.0f, 0.0f, 3.0f};
    float     yaw         = -90.0f;    // degrees; -90 looks down -Z
    float     pitch       = 0.0f;      // degrees, clamped to +-89
    float     fov_y       = 60.0f;     // vertical field of view, degrees
    float     z_near      = 0.05f;
    float     z_far       = 1000.0f;
    float     speed       = 2.0f;      // units per second
    float     sensitivity = 0.12f;     // degrees per pixel of mouse movement

    // Input state (managed by camera_update)
    bool   looking = false;
    double last_x  = 0.0;
    double last_y  = 0.0;
};

void camera_update(Camera& cam, GLFWwindow* window, float dt);

glm::vec3 camera_forward(const Camera& cam);
glm::mat4 camera_view(const Camera& cam);
glm::mat4 camera_proj(const Camera& cam, VkExtent2D extent);

// Focal lengths in pixels (fx, fy) for the current projection.
// Splat projection math (the Jacobian) needs these.
glm::vec2 camera_focal(const Camera& cam, VkExtent2D extent);
