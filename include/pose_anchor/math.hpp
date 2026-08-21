#pragma once

#include <algorithm>
#include <cmath>

namespace pose_anchor {

struct Vec3 {
    double x{};
    double y{};
    double z{};

    [[nodiscard]] bool finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    [[nodiscard]] double squaredNorm() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] double norm() const noexcept { return std::sqrt(squaredNorm()); }
};

inline Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 v) noexcept { return {-v.x, -v.y, -v.z}; }
inline Vec3 operator*(Vec3 v, double s) noexcept { return {v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator*(double s, Vec3 v) noexcept { return v * s; }
inline Vec3 operator/(Vec3 v, double s) noexcept { return {v.x / s, v.y / s, v.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) noexcept { a = a + b; return a; }

inline double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Quat {
    double w{1.0};
    double x{};
    double y{};
    double z{};

    [[nodiscard]] bool finite() const noexcept {
        return std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    [[nodiscard]] double squaredNorm() const noexcept { return w * w + x * x + y * y + z * z; }
};

inline Quat operator-(Quat q) noexcept { return {-q.w, -q.x, -q.y, -q.z}; }
inline Quat operator*(Quat a, Quat b) noexcept {
    return {
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

inline double dot(Quat a, Quat b) noexcept { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
inline Quat conjugate(Quat q) noexcept { return {q.w, -q.x, -q.y, -q.z}; }

inline Quat normalized(Quat q) noexcept {
    const double n2 = q.squaredNorm();
    if (!std::isfinite(n2) || n2 <= 1e-18) {
        return {};
    }
    const double inv = 1.0 / std::sqrt(n2);
    return {q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}

inline Vec3 rotate(Quat q, Vec3 v) noexcept {
    q = normalized(q);
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = 2.0 * cross(u, v);
    return v + q.w * t + cross(u, t);
}

inline Quat fromRotationVector(Vec3 r) noexcept {
    const double angle = r.norm();
    if (angle < 1e-10) {
        return normalized({1.0, 0.5 * r.x, 0.5 * r.y, 0.5 * r.z});
    }
    const double half = 0.5 * angle;
    const double scale = std::sin(half) / angle;
    return {std::cos(half), r.x * scale, r.y * scale, r.z * scale};
}

inline Vec3 rotationVector(Quat q) noexcept {
    q = normalized(q);
    if (q.w < 0.0) {
        q = -q;
    }
    const double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-10) {
        return {2.0 * q.x, 2.0 * q.y, 2.0 * q.z};
    }
    const double angle = 2.0 * std::atan2(s, std::clamp(q.w, -1.0, 1.0));
    const double scale = angle / s;
    return {q.x * scale, q.y * scale, q.z * scale};
}

inline double angularDistance(Quat a, Quat b) noexcept {
    const double d = std::clamp(std::abs(dot(normalized(a), normalized(b))), 0.0, 1.0);
    return 2.0 * std::acos(d);
}

inline Quat sameHemisphere(Quat q, Quat reference) noexcept {
    q = normalized(q);
    return dot(q, reference) < 0.0 ? -q : q;
}

inline Vec3 leftJacobianApply(Vec3 r, Vec3 derivative) noexcept {
    const double theta = r.norm();
    const Vec3 first = cross(r, derivative);
    const Vec3 second = cross(r, first);
    double a;
    double b;
    if (theta < 1e-5) {
        const double t2 = theta * theta;
        a = 0.5 - t2 / 24.0;
        b = 1.0 / 6.0 - t2 / 120.0;
    } else {
        const double t2 = theta * theta;
        a = (1.0 - std::cos(theta)) / t2;
        b = (theta - std::sin(theta)) / (t2 * theta);
    }
    return derivative + a * first + b * second;
}

inline Vec3 leftJacobianInverseApply(Vec3 r, Vec3 angularVelocity) noexcept {
    const double theta = r.norm();
    const Vec3 first = cross(r, angularVelocity);
    const Vec3 second = cross(r, first);
    double c;
    if (theta < 1e-5) {
        const double t2 = theta * theta;
        c = 1.0 / 12.0 + t2 / 720.0;
    } else {
        const double half = 0.5 * theta;
        c = 1.0 / (theta * theta) - (std::cos(half) / std::sin(half)) / (2.0 * theta);
    }
    return angularVelocity - 0.5 * first + c * second;
}

}  // namespace pose_anchor
