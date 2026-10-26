#pragma once

#include <cmath>

constexpr float PI = 3.14159265f;

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float a, float b) : x(a), y(b) {}
};

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float a) : x(a), y(a), z(a) {}
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}

    Vec3 operator+(const Vec3& r) const { return Vec3(x + r.x, y + r.y, z + r.z); }
    Vec3 operator-(const Vec3& r) const { return Vec3(x - r.x, y - r.y, z - r.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator*(const Vec3& r) const { return Vec3(x * r.x, y * r.y, z * r.z); }
    float dot(const Vec3& r) const { return x * r.x + y * r.y + z * r.z; }
    Vec3 cross(const Vec3& r) const { return Vec3(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x); }
    Vec3 normalize() const {
        float l = std::sqrt(x * x + y * y + z * z);
        return l > 0 ? *this * (1.0f / l) : *this;
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
};

inline Vec3 operator*(float s, const Vec3& v) {
    return Vec3(v.x * s, v.y * s, v.z * s);
}

struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(1) {}
    Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    Vec4(Vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

struct Mat4 {
    float m[4][4];

    Mat4() {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] = (i == j ? 1.0f : 0.0f);
            }
        }
    }

    static Mat4 identity() { return Mat4(); }

    static Mat4 translate(Vec3 v) {
        Mat4 r;
        r.m[0][3] = v.x;
        r.m[1][3] = v.y;
        r.m[2][3] = v.z;
        return r;
    }

    static Mat4 scale(Vec3 v) {
        Mat4 r;
        r.m[0][0] = v.x;
        r.m[1][1] = v.y;
        r.m[2][2] = v.z;
        return r;
    }

    static Mat4 rotateY(float rad) {
        Mat4 r;
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][2] = s;
        r.m[2][0] = -s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotateX(float rad) {
        Mat4 r;
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[1][1] = c;
        r.m[1][2] = -s;
        r.m[2][1] = s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotateZ(float rad) {
        Mat4 r;
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][1] = -s;
        r.m[1][0] = s;
        r.m[1][1] = c;
        return r;
    }

    static Mat4 perspective(float fov, float aspect, float n, float f) {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = 0;
            }
        }
        float th = std::tan(fov / 2);
        r.m[0][0] = 1 / (aspect * th);
        r.m[1][1] = 1 / th;
        r.m[2][2] = -(f + n) / (f - n);
        r.m[2][3] = -(2 * f * n) / (f - n);
        r.m[3][2] = -1;
        return r;
    }

    static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
        Mat4 m;
        m.m[0][0] = 2 / (r - l);
        m.m[0][3] = -(r + l) / (r - l);
        m.m[1][1] = 2 / (t - b);
        m.m[1][3] = -(t + b) / (t - b);
        m.m[2][2] = -2 / (f - n);
        m.m[2][3] = -(f + n) / (f - n);
        return m;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalize();
        Vec3 s = f.cross(up).normalize();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[0][1] = s.y;
        r.m[0][2] = s.z;
        r.m[0][3] = -s.dot(eye);
        r.m[1][0] = u.x;
        r.m[1][1] = u.y;
        r.m[1][2] = u.z;
        r.m[1][3] = -u.dot(eye);
        r.m[2][0] = -f.x;
        r.m[2][1] = -f.y;
        r.m[2][2] = -f.z;
        r.m[2][3] = f.dot(eye);
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        Vec4 r(0, 0, 0, 0);
        r.x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w;
        r.y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w;
        r.z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w;
        r.w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w;
        return r;
    }

    Mat4 operator*(const Mat4& r) const {
        Mat4 res;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                res.m[i][j] = 0;
                for (int k = 0; k < 4; ++k) {
                    res.m[i][j] += m[i][k] * r.m[k][j];
                }
            }
        }
        return res;
    }
};
