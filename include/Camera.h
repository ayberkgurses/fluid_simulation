#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Camera — free-fly perspective camera backed by GLM.
//
// Design:
//   State is minimal: world-space `position` and two Euler angles (`yaw`,
//   `pitch`).  From these, three orthogonal basis vectors (front/right/up) are
//   recomputed by `updateVectors()` whenever angles change.  A GLM lookAt()
//   call then converts them into a view matrix each frame.
//
//   Yaw = 0   → camera faces -Z (OpenGL convention, looking "into" the screen)
//   Pitch > 0 → camera tilts upward
//   Pitch is clamped to ±89° to avoid gimbal flip at the poles.
// ─────────────────────────────────────────────────────────────────────────────

enum class CameraDir { Forward, Backward, Left, Right, Up, Down };

class Camera {
public:
    glm::vec3 position;
    float     yaw;         // horizontal rotation in degrees
    float     pitch;       // vertical rotation in degrees, clamped ±89
    float     moveSpeed;   // world units per second (WASD)
    float     mouseSensitivity;
    float     fovDeg;      // vertical field-of-view in degrees

    // Constructs a camera at `startPos` facing direction given by `startYaw`
    // and `startPitch`. Default: position (0,1.5,3.5), looking slightly down
    // toward the fluid container at the origin.
    explicit Camera(glm::vec3 startPos   = glm::vec3(0.0f, 1.5f, 3.5f),
                    float     startYaw   = -90.0f,
                    float     startPitch = -20.0f)
        : position(startPos)
        , yaw(startYaw)
        , pitch(startPitch)
        , moveSpeed(2.5f)
        , mouseSensitivity(0.10f)
        , fovDeg(45.0f)
    {
        updateVectors();
    }

    // Returns a view matrix computed via glm::lookAt using the current
    // position and the derived front/up vectors.
    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    // Returns a perspective projection matrix. `aspect` = width / height.
    // Near plane is 0.01 to allow close-up inspection; far plane 100.
    glm::mat4 projMatrix(float aspect) const {
        return glm::perspective(glm::radians(fovDeg), aspect, 0.01f, 100.0f);
    }

    // Move camera in the given direction, scaled by `dt` (seconds) and
    // `moveSpeed`. Motion is always relative to the camera's current
    // orientation, so WASD strafes correctly even when looking sideways.
    void processKeyboard(CameraDir dir, float dt) {
        float v = moveSpeed * dt;
        switch (dir) {
            case CameraDir::Forward:  position += front * v; break;
            case CameraDir::Backward: position -= front * v; break;
            case CameraDir::Left:     position -= right * v; break;
            case CameraDir::Right:    position += right * v; break;
            case CameraDir::Up:       position += up    * v; break;
            case CameraDir::Down:     position -= up    * v; break;
        }
    }

    // Rotate camera by mouse delta (pixels). `dy` is positive when the mouse
    // moves upward on screen, mapping to an increase in pitch (look up).
    void processMouseMovement(float dx, float dy) {
        yaw   += dx * mouseSensitivity;
        pitch += dy * mouseSensitivity;
        pitch  = std::clamp(pitch, -89.0f, 89.0f);
        updateVectors();
    }

    // Zoom by adjusting the vertical FOV. Scroll up → narrower FOV (zoom in).
    void processScroll(float yoffset) {
        fovDeg -= yoffset;
        fovDeg  = std::clamp(fovDeg, 1.0f, 90.0f);
    }

    glm::vec3 getFront() const { return front; }
    glm::vec3 getRight() const { return right; }

private:
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f,  0.0f};
    glm::vec3 up   {0.0f, 1.0f,  0.0f};

    // Recompute front, right, up from the stored yaw and pitch angles.
    // Standard spherical-to-Cartesian conversion on the unit sphere:
    //   front.x = cos(yaw)*cos(pitch)
    //   front.y = sin(pitch)
    //   front.z = sin(yaw)*cos(pitch)
    // right is the cross product of front with the world-up axis (0,1,0),
    // and up is the cross product of right with front (camera-relative up).
    void updateVectors() {
        glm::vec3 f;
        f.x   = cosf(glm::radians(yaw))   * cosf(glm::radians(pitch));
        f.y   = sinf(glm::radians(pitch));
        f.z   = sinf(glm::radians(yaw))   * cosf(glm::radians(pitch));
        front = glm::normalize(f);
        right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        up    = glm::normalize(glm::cross(right, front));
    }
};
