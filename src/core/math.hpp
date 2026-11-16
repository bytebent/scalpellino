#pragma once
// Minimal double-precision math for the core. No graphics dependencies.
#include <cmath>

namespace scal {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }

    double  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i)       { return i == 0 ? x : (i == 1 ? y : z); }
};

inline constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const double l = length(v);
    return l > 0 ? v / l : Vec3{};
}

// Plane: dot(n, p) == d. n points OUT of the brush.
struct Plane {
    Vec3 n;
    double d = 0;

    double distance(const Vec3& p) const { return dot(n, p) - d; }

    // Quake/TrenchBroom convention: n = normalize(cross(c - a, b - a)).
    static Plane fromPoints(const Vec3& a, const Vec3& b, const Vec3& c) {
        Plane p;
        p.n = normalize(cross(c - a, b - a));
        p.d = dot(p.n, a);
        return p;
    }
};

struct AABB {
    Vec3 min{ 1e300,  1e300,  1e300};
    Vec3 max{-1e300, -1e300, -1e300};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void add(const Vec3& p) {
        for (int i = 0; i < 3; ++i) {
            if (p[i] < min[i]) min[i] = p[i];
            if (p[i] > max[i]) max[i] = p[i];
        }
    }
    void add(const AABB& b) { if (b.valid()) { add(b.min); add(b.max); } }
    Vec3 size() const { return max - min; }
    Vec3 center() const { return (min + max) * 0.5; }
};

// Snap values within eps of an integer to that integer (cleans clipping noise).
inline double cleanNumber(double v, double eps = 1e-6) {
    const double r = std::round(v);
    return std::fabs(v - r) < eps ? r : v;
}
inline Vec3 cleanVec(const Vec3& v) { return {cleanNumber(v.x), cleanNumber(v.y), cleanNumber(v.z)}; }

} // namespace scal
