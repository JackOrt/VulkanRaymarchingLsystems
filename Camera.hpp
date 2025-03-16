#pragma once

#include "CommonHeader.hpp"

enum class Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 5.0f;
const float SENSITIVITY = 0.2f;
const float ZOOM = 45.0f;

class Camera {
public:
    // Camera attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    // Euler Angles
    float Yaw;
    float Pitch;
    // Options
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    // Constructor with default values.
    Camera(glm::vec3 position = glm::vec3(0.0f, 1.0f, 3.0f),
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
        float yaw = YAW,
        float pitch = PITCH);

    // Returns the view matrix using LookAt.
    glm::mat4 GetViewMatrix();

    // Processes input from keyboard.
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);

    // Processes input from mouse movement.
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    // Processes mouse scroll for zoom.
    void ProcessMouseScroll(float yoffset);

    // Reset camera to default parameters.
    void Reset();

private:
    // Recalculate the front vector.
    void updateCameraVectors();
};
