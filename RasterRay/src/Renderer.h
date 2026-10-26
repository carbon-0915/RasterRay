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

    // 自动取景：让整个包围盒（含水平方向）完整落在视野内
    void frame(const AABB& box) {
        Vec3 center = box.center();

        const float elev = 0.3f; //俯仰角，> 0 表示"从上往下看"
        const float azim = 1.8f; //方位角，决定从哪个水平方向看
        Vec3 dir(std::cos(elev) * std::cos(azim), std::sin(elev), std::cos(elev) * std::sin(azim));

        // 相机基：forward 指向中心（即 -dir），right/cameraUp 是屏幕上的右/上
        Vec3 forward = Vec3(0, 0, 0) - dir;
        Vec3 right = forward.cross(up).normalize();
        Vec3 cameraUp = right.cross(forward).normalize();

        // AABB 的 8 个角点（立方体顶点），显式列出，不用位运算
        Vec3 corners[8] = {
            { box.min.x, box.min.y, box.min.z },
            { box.max.x, box.min.y, box.min.z },
            { box.min.x, box.max.y, box.min.z },
            { box.max.x, box.max.y, box.min.z },
            { box.min.x, box.min.y, box.max.z },
            { box.max.x, box.min.y, box.max.z },
            { box.min.x, box.max.y, box.max.z },
            { box.max.x, box.max.y, box.max.z },
        };

        // 每个角点相对中心的偏移，投影到 right（屏幕水平）/ cameraUp（屏幕竖直）轴上，
        // 点积 = 该角点在屏幕横/竖方向离中心多远（有符号），取 min/max 得到半宽/半高。
        float minR = FLT_MAX, maxR = -FLT_MAX, minU = FLT_MAX, maxU = -FLT_MAX;
        for (const Vec3& corner : corners) {
            Vec3 offset = corner - center;               // 角点相对中心的偏移向量
            float r = offset.dot(right);                 // 投影到屏幕水平轴（有符号长度）
            float u = offset.dot(cameraUp);              // 投影到屏幕竖直轴（有符号长度）
            minR = std::min(minR, r); maxR = std::max(maxR, r);
            minU = std::min(minU, u); maxU = std::max(maxU, u);
        }
        float halfW = (maxR - minR) * 0.5f;   // 包围盒在屏幕上的半宽
        float halfH = (maxU - minU) * 0.5f;   // 包围盒在屏幕上的半高
        if (halfH < 1e-4f) halfH = 1.0f;

        // 距离：让半高填满竖直半视野、半宽填满水平半视野，取大的保证两个方向都装下，留 20% 余量
        float aspect = (float)WINDOW_WIDTH / WINDOW_HEIGHT;      // 宽高比
        float distV = halfH / std::tan(fov / 2.0f);              // 竖直：tan(半fov) = halfH / dist
        float distH = halfW / (std::tan(fov / 2.0f) * aspect);   // 水平：水平半fov的 tan = tan(fov/2)*aspect
        float dist = std::max(distV, distH) * 1.2f;

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
