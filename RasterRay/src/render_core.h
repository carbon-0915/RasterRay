#pragma once

#include "math.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

struct Texture {
    int w = 0;
    int h = 0;
    std::vector<Vec3> pixels;

    // 最近邻采样：读 uv 处颜色（const 必须：贴图以 shared_ptr<const Texture> 共享）
    Vec3 sample(Vec2 uv) const {
        int x = std::abs((int)(uv.x * w)) % w;
        int y = std::abs((int)(uv.y * h)) % h;
        return pixels[y * w + x];
    }
};

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
};

// 轴对齐包围盒（AABB）
struct AABB {
    Vec3 min{ FLT_MAX, FLT_MAX, FLT_MAX };
    Vec3 max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

    // 工厂：从顶点列表造盒（模型空间）
    static AABB fromVertices(const std::vector<Vertex>& verts) {
        AABB box;
        for (const auto& v : verts) {
            box.min.x = std::min(box.min.x, v.pos.x);
            box.min.y = std::min(box.min.y, v.pos.y);
            box.min.z = std::min(box.min.z, v.pos.z);
            box.max.x = std::max(box.max.x, v.pos.x);
            box.max.y = std::max(box.max.y, v.pos.y);
            box.max.z = std::max(box.max.z, v.pos.z);
        }
        return box;
    }

    // 取并集
    AABB merged(const AABB& other) const {
        AABB box;
        box.min.x = std::min(min.x, other.min.x);
        box.min.y = std::min(min.y, other.min.y);
        box.min.z = std::min(min.z, other.min.z);
        box.max.x = std::max(max.x, other.max.x);
        box.max.y = std::max(max.y, other.max.y);
        box.max.z = std::max(max.z, other.max.z);
        return box;
    }

    // 变换 8 个角点后重新取盒（模型空间 -> 世界空间）
    AABB transformed(const Mat4& m) const {
        const Vec3 corners[8] = {
            { min.x, min.y, min.z }, { max.x, min.y, min.z },
            { min.x, max.y, min.z }, { max.x, max.y, min.z },
            { min.x, min.y, max.z }, { max.x, min.y, max.z },
            { min.x, max.y, max.z }, { max.x, max.y, max.z },
        };
        AABB box;
        for (int i = 0; i < 8; ++i) {
            Vec4 p = m * Vec4(corners[i], 1.0f);
            box.min.x = std::min(box.min.x, p.x);
            box.min.y = std::min(box.min.y, p.y);
            box.min.z = std::min(box.min.z, p.z);
            box.max.x = std::max(box.max.x, p.x);
            box.max.y = std::max(box.max.y, p.y);
            box.max.z = std::max(box.max.z, p.z);
        }
        return box;
    }

    Vec3 center() const { return (min + max) * 0.5f; }
    float radius() const { return (max - min).length() * 0.5f; }
};

// 变换：位置 / 旋转（欧拉角，全 3 轴）/ 缩放
struct Transform {
    Vec3 position{ 0, 0, 0 };
    Vec3 rotation{ 0, 0, 0 };
    Vec3 scale{ 1, 1, 1 };

    // T * R * S：局部空间 -> 父空间（本项目即世界空间）
    Mat4 toMatrix() const {
        return Mat4::translate(position)
            * Mat4::rotateY(rotation.y)
            * Mat4::rotateX(rotation.x)
            * Mat4::rotateZ(rotation.z)
            * Mat4::scale(scale);
    }
};

// 材质：描述表面外观，光照/着色时读取（本阶段只保留 Blinn-Phong 用到的字段）
struct Material {
    Vec3  diffuse{ 1, 1, 1 };                    // Kd 漫反射底色（无贴图用它，有贴图与贴图相乘）
    std::shared_ptr<const Texture> diffuseMap;   // map_Kd 颜色贴图
    std::shared_ptr<const Texture> normalMap;    // map_Bump 法线贴图
    float shininess = 32.0f;                     // Ns 高光指数
    bool  isTransparent = false;                 // 是否透明（d<1 或 Tr>0）
    float alpha = 1.0f;                          // 不透明度 d
};

// 网格：一个 Mesh 一个材质
struct Mesh {
    std::vector<Vertex> verts;
    std::vector<int> indices;
    Material material;
    AABB localBounds;            // 模型空间包围盒（加载后不变）
};

// 模型：一个 obj = 一个 Model，按 usemtl 拆成多个 Mesh（本次通常 1 个）
struct Model {
    std::vector<Mesh> meshes;
    std::string sourcePath;
};

// 场景对象：共享几何 + 自身变换
struct SceneObject {
    std::string name;
    std::shared_ptr<const Model> model;
    Transform transform;
    bool visible = true;

    // 现算：模型局部盒变换到世界空间
    AABB worldBounds() const {
        AABB local;
        for (const auto& mesh : model->meshes)
            local = local.merged(mesh.localBounds);
        return local.transformed(transform.toMatrix());
    }
};

enum LightType {
    LIGHT_DIRECTIONAL,
    LIGHT_POINT,
    LIGHT_SPOT
};

struct Light {
    LightType type = LIGHT_DIRECTIONAL;
    Vec3 pos;
    Vec3 dir;
    Vec3 color = Vec3(1, 1, 1);
    float intensity = 1.0f;
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    float cutOff = std::cos(12.5f * PI / 180.0f);
    float outerCutOff = std::cos(17.5f * PI / 180.0f);
};

// 场景：纯容器
struct Scene {
    std::vector<SceneObject> objects;
    std::vector<Light> lights;

    int add(SceneObject object) {
        objects.push_back(object);
        return static_cast<int>(objects.size()) - 1;
    }

    bool remove(int index) {
        if (index < 0 || index >= static_cast<int>(objects.size())) return false;
        objects.erase(objects.begin() + index);
        return true;
    }

    AABB bounds() const {
        AABB box;
        for (const auto& obj : objects)
            if (obj.visible) box = box.merged(obj.worldBounds());
        return box;
    }
};
