#pragma once

#include <algorithm>
#include <cmath>

namespace Wheeler
{
    constexpr float Pi = 3.14159265358979323846f;

    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        Vec3 operator+(Vec3 b) const { return { x + b.x, y + b.y, z + b.z }; }
        Vec3 operator-(Vec3 b) const { return { x - b.x, y - b.y, z - b.z }; }
        Vec3 operator*(float b) const { return { x * b, y * b, z * b }; }
        Vec3 operator/(float b) const { return *this * (1.0f / b); }
        Vec3& operator+=(Vec3 b) { *this = *this + b; return *this; }
    };
    inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
    inline Vec3 Normalize(Vec3 v) { const float l = Length(v); return l > 1e-8f ? v / l : Vec3{ 0, 0, 1 }; }
}
