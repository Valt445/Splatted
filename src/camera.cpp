#include "camera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

glm::vec3 camera_forward(const Camera& cam)
{
    float yaw   = glm::radians(cam.yaw);
    float pitch = glm::radians(cam.pitch);
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch),
                                    std::sin(pitch),
                                    std::sin(yaw) * std::cos(pitch)));
}

void camera_update(Camera& cam, GLFWwindow* window, float dt)
{
    // ---- Look: hold right mouse ----
    bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    if (rmb && !cam.looking) {                 // just started: no jump on first frame
        cam.looking = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &x, &y);
        cam.last_x = x;
        cam.last_y = y;
    } else if (!rmb && cam.looking) {
        cam.looking = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (cam.looking) {
        cam.yaw   += (float)(x - cam.last_x) * cam.sensitivity;
        cam.pitch -= (float)(y - cam.last_y) * cam.sensitivity;
        cam.pitch  = glm::clamp(cam.pitch, -89.0f, 89.0f);
        cam.last_x = x;
        cam.last_y = y;
    }

    // ---- Move ----
    const glm::vec3 world_up = {0.0f, 1.0f, 0.0f};
    glm::vec3 forward = camera_forward(cam);
    glm::vec3 right   = glm::normalize(glm::cross(forward, world_up));

    glm::vec3 move = {0.0f, 0.0f, 0.0f};
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) move += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) move -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move -= right;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)          move += world_up;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)          move -= world_up;

    if (glm::dot(move, move) > 0.0f) {
        float speed = cam.speed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 4.0f;
        cam.position += glm::normalize(move) * speed * dt;
    }
}

glm::mat4 camera_view(const Camera& cam)
{
    return glm::lookAt(cam.position, cam.position + camera_forward(cam), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 camera_proj(const Camera& cam, VkExtent2D extent)
{
    float     aspect = (float)extent.width / (float)extent.height;
    glm::mat4 proj   = glm::perspective(glm::radians(cam.fov_y), aspect, cam.z_near, cam.z_far);
    proj[1][1] *= -1.0f;   // Vulkan clip space has +Y pointing down
    return proj;
}

glm::vec2 camera_focal(const Camera& cam, VkExtent2D extent)
{
    // Square pixels: same focal length on both axes.
    float fy = (float)extent.height / (2.0f * std::tan(glm::radians(cam.fov_y) * 0.5f));
    return {fy, fy};
}
