#pragma once

#include "render_core.h"

// 显示/渲染配置
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;
constexpr int SHADOW_MAP_SIZE = 1024;

// 相机：世界空间信息 + 相机空间基 + 渲染参数
struct Camera {
    Vec3  position{ 10, 11, 12 };   // 相机位置（世界空间）
    Vec3  target{ 0, 0, 0 };        // 看向的目标点（世界空间）
    Vec3  up{ 0, 1, 0 };            // 世界 up（lookAt 的参考，消除滚转）

    // 相机空间三根轴（世界空间表示，由 position/target/up 立即推出）
    Vec3 forward() const { return (target - position).normalize(); }
    Vec3 right() const { return forward().cross(up).normalize(); }
    Vec3 cameraUp() const { return right().cross(forward()).normalize(); }

    float fov = PI / 3.5f;          // 垂直视场角（弧度）
    float nearPlane = 0.1f;         // 近裁剪面距离
    float farPlane = 100.0f;        // 远裁剪面距离

    Mat4 view() const { return Mat4::lookAt(position, target, up); }
    Mat4 projection(float aspect) const { return Mat4::perspective(fov, aspect, nearPlane, farPlane); }

    // 光追：像素坐标 -> 射线方向（内部由 w/h 现算 aspect）
    Vec3 rayDirection(int x, int y, int w, int h) const {
        float aspect = (float)w / h;
        float px = (2.0f * (x + 0.5f) / w - 1.0f) * aspect * std::tan(fov / 2);
        float py = (1.0f - 2.0f * (y + 0.5f) / h) * std::tan(fov / 2);
        return (forward() + right() * px + cameraUp() * py).normalize();
    }

    // 自动取景：让包围盒充满视野
    void frame(const AABB& box) {
        Vec3 center = box.center();
        float halfHeight = (box.max.y - box.min.y) * 0.5f;   // 垂直半高（不用半对角线，太松）
        if (halfHeight < 1e-4f) halfHeight = 1.0f;
        float dist = halfHeight / std::tan(fov / 2.0f) * 1.1f;  // tan：让模型竖直方向填满视野

        const float elev = 0.5f;
        const float azim = 0.8f;
        Vec3 dir(std::cos(elev) * std::cos(azim), std::sin(elev), std::cos(elev) * std::sin(azim));
        position = center + dir * dist;
        target = center;
    }
};

class Rasterizer {
public:
    Rasterizer();

    void render(
        const Scene& scene,
        const Camera& camera,
        int selectedObjectIndex,
        float timeSeconds);

    const std::vector<uint32_t>& colorBuffer() const { return colorBuffer_; }

private:
    int width_ = WINDOW_WIDTH;
    int height_ = WINDOW_HEIGHT;
    std::vector<float> depthBuffer_;
    std::vector<uint32_t> colorBuffer_;
    std::vector<float> shadowMap_;
};

class RayTracer {
public:
    void render(
        const Scene& scene,
        const Camera& camera,
        const std::string& filename) const;
};
