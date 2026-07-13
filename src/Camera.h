#pragma once

#include "MathTypes.h"

class Camera
{
public:
    void Frame(const Bounds& bounds);
    void Orbit(float deltaX, float deltaY);
    void Pan(float deltaX, float deltaY);
    void Zoom(float amount);
    void ApplyView() const;

    Vec3 Position() const;
    Vec3 Forward() const;
    Vec3 Right() const;
    Vec3 Up() const;

    float yaw = 35.0f;
    float pitch = 20.0f;
    float distance = 10.0f;
    Vec3 target{};

private:
    void CalculateBasis(
        Vec3& forward,
        Vec3& right,
        Vec3& up) const;
};
