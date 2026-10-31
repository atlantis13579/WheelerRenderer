#pragma once
#include "Math.h"

namespace Wheeler
{
    class Camera
    {
    public:
        Vec3 target = {};
        float yaw = 0.0f;
        float pitch = 0.065f;
        float distance = 32.0f;
        float fov = 42.0f;
        float speed = 0.4f;
        bool autoOrbit = false;

        Vec3 Eye() const { return target + Vec3{ std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) } * distance; }
        Vec3 Forward() const { return Normalize(target - Eye()); }
        Vec3 Right() const { return Normalize(Cross(Forward(), { 0, 1, 0 })); }
        Vec3 Up() const { return Cross(Right(), Forward()); }
        void Orbit(float dx, float dy) { yaw += dx * 0.006f; pitch = std::clamp(pitch + dy * 0.006f, -1.54f, 1.54f); }
        void Zoom(float wheel) { distance = std::clamp(distance * std::exp(-wheel * 0.12f), 2.2f, 250.0f); KeepOutsideHorizon(); }
        void Pan(float dx, float dy, float viewportHeight)
        {
            const float scale = 2.0f * distance * std::tan(fov * Pi / 360.0f) / std::max(viewportHeight, 1.0f);
            target += Right() * (-dx * scale) + Up() * (dy * scale);
            KeepOutsideHorizon();
        }
        void Move(float forward, float right, float vertical, float dt, bool fast)
        {
            Vec3 f = Forward(); f.y = 0; f = Normalize(f);
            Vec3 delta = f * forward + Right() * right + Vec3{ 0, vertical, 0 };
            if (Dot(delta, delta) > 0.0f)
                target += Normalize(delta) * (std::max(distance, 3.0f) * speed * dt * (fast ? 3.0f : 1.0f));
            KeepOutsideHorizon();
        }
        void KeepOutsideHorizon()
        {
            // A static Schwarzschild observer must remain outside the horizon.
            const Vec3 eye = Eye();
            if (Length(eye) < 1.1f) target += Normalize(eye) * 1.1f - eye;
        }
        void Reset() { *this = Camera{}; }
    };
}
