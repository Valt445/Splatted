#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct GLFWwindow;

struct Camera {
    glm::vec3 position    = {0.0f, 0.0f, 3.0f};
    float     yaw         = -90.0f;
    float     pitch       = 0.0f;
    float     fov_y       = 60.0f;
    float     z_near      = 0.05f;
    float     z_far       = 1000.0f;
    float     speed       = 2.0f;
    float     sensitivity = 0.12f;

    bool   looking = false;
    double last_x  = 0.0;
    double last_y  = 0.0;
};

void camera_update(Camera& cam, GLFWwindow* window, float dt);

glm::vec3 camera_forward(const Camera& cam);
glm::mat4 camera_view(const Camera& cam);
glm::mat4 camera_proj(const Camera& cam, VkExtent2D extent);

glm::vec2 camera_focal(const Camera& cam, VkExtent2D extent);
