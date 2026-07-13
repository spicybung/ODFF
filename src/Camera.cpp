#include "Camera.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;

    float DegreesToRadians(float degrees)
    {
        return degrees * Pi / 180.0f;
    }

    float WrapAngle(float angle)
    {
        angle = std::fmod(angle, 360.0f);

        if (angle < 0.0f)
        {
            angle += 360.0f;
        }

        return angle;
    }
}

void Camera::Frame(const Bounds& bounds)
{
    if (!bounds.valid)
    {
        target = {};
        distance = 10.0f;
        return;
    }

    target = bounds.Center();
    distance = std::max(bounds.Radius() * 2.5f, 0.5f);
}

void Camera::Orbit(float deltaX, float deltaY)
{
    yaw = WrapAngle(yaw + deltaX * 0.35f);
    pitch = WrapAngle(pitch + deltaY * 0.35f);
}

void Camera::Pan(float deltaX, float deltaY)
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);

    const float scale = distance * 0.0015f;

    target += right * (-deltaX * scale);
    target += up * (deltaY * scale);
}

void Camera::Zoom(float amount)
{
    const float factor = std::pow(0.88f, amount);

    distance = std::clamp(
        distance * factor,
        0.02f,
        100000.0f);
}

Vec3 Camera::Position() const
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);

    return target - forward * distance;
}

Vec3 Camera::Forward() const
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);
    return forward;
}

Vec3 Camera::Right() const
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);
    return right;
}

Vec3 Camera::Up() const
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);
    return up;
}

void Camera::ApplyView() const
{
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    CalculateBasis(forward, right, up);

    const Vec3 eye = target - forward * distance;

    const float viewMatrix[16] = {
        right.x, up.x, -forward.x, 0.0f,
        right.y, up.y, -forward.y, 0.0f,
        right.z, up.z, -forward.z, 0.0f,
        0.0f,    0.0f, 0.0f,       1.0f
    };

    glMultMatrixf(viewMatrix);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void Camera::CalculateBasis(
    Vec3& forward,
    Vec3& right,
    Vec3& up) const
{
    const float yawRadians = DegreesToRadians(yaw);
    const float pitchRadians = DegreesToRadians(pitch);

    const float cosinePitch = std::cos(pitchRadians);

    const Vec3 eyeOffsetDirection = {
        cosinePitch * std::cos(yawRadians),
        cosinePitch * std::sin(yawRadians),
        std::sin(pitchRadians)
    };

    forward = Normalize(eyeOffsetDirection * -1.0f);

    right = Normalize({
        -std::sin(yawRadians),
        std::cos(yawRadians),
        0.0f
    });

    up = Normalize(Cross(right, forward));
}
