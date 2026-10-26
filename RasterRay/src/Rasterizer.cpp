#include "render_core.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;

struct VSOut {
    Vec4 pos;
    Vec3 worldPos;
    Vec3 normal;
    Vec2 uv;
    float w = 1.0f;
};

float edgeFunction(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

VSOut mainVS(const Vertex& vertex, const Mat4& model, const Mat4& viewProjection) {
    VSOut out;
    Vec4 worldPosition = model * Vec4(vertex.pos, 1.0f);
    out.worldPos = Vec3(worldPosition.x, worldPosition.y, worldPosition.z);

    float sx = Vec3(model.m[0][0], model.m[0][1], model.m[0][2]).length();
    float sy = Vec3(model.m[1][0], model.m[1][1], model.m[1][2]).length();
    float sz = Vec3(model.m[2][0], model.m[2][1], model.m[2][2]).length();
    Vec3 inverseScale(1.0f / sx, 1.0f / sy, 1.0f / sz);

    Vec4 rawNormal = model * Vec4(vertex.normal * inverseScale, 0.0f);
    out.normal = Vec3(rawNormal.x, rawNormal.y, rawNormal.z).normalize();
    out.pos = viewProjection * worldPosition;
    out.uv = vertex.uv;
    return out;
}

Vec4 shadowVS(const Vertex& vertex, const Mat4& model, const Mat4& lightProjection) {
    return lightProjection * model * Vec4(vertex.pos, 1.0f);
}

Vec3 calculateLight(
    Light light,
    Vec3 normal,
    Vec3 viewDir,
    Vec3 fragmentPosition,
    float shininess,
    float shadowFactor) {
    Vec3 lightDir;
    float attenuation = 1.0f;

    if (light.type == LIGHT_DIRECTIONAL) {
        lightDir = (Vec3(0, 0, 0) - light.dir).normalize();
    } else {
        lightDir = (light.pos - fragmentPosition).normalize();
        float distance = (light.pos - fragmentPosition).length();
        attenuation = 1.0f / (
            light.constant
            + light.linear * distance
            + light.quadratic * (distance * distance));
    }

    if (light.type == LIGHT_SPOT) {
        float theta = lightDir.dot((Vec3(0, 0, 0) - light.dir).normalize());
        float epsilon = light.cutOff - light.outerCutOff;
        float intensityFactor = std::max(0.0f, std::min(1.0f, (theta - light.outerCutOff) / epsilon));
        attenuation *= intensityFactor;
    }

    float diff = std::max(normal.dot(lightDir), 0.0f);
    Vec3 halfwayDir = (lightDir + viewDir).normalize();
    float spec = std::pow(std::max(normal.dot(halfwayDir), 0.0f), shininess);

    Vec3 diffuse = light.color * diff * light.intensity * attenuation;
    Vec3 specular = light.color * spec * light.intensity * attenuation;

    if (light.type == LIGHT_DIRECTIONAL) {
        diffuse = diffuse * (1.0f - shadowFactor);
        specular = specular * (1.0f - shadowFactor);
    }
    return diffuse + specular;
}

float sampleShadow(
    const VSOut& interpolated,
    const std::vector<Light>& lights,
    const Mat4& lightProjection,
    const std::vector<float>& shadowMap) {
    float shadow = 0.0f;
    Vec4 lightPosition = lightProjection * Vec4(interpolated.worldPos, 1.0f);
    Vec3 projected = Vec3(lightPosition.x, lightPosition.y, lightPosition.z) * (1.0f / lightPosition.w);
    projected = projected * 0.5f + Vec3(0.5f);

    if (!lights.empty()
        && projected.z < 1
        && projected.x > 0
        && projected.x < 1
        && projected.y > 0
        && projected.y < 1) {
        Vec3 lightDir = (Vec3(0, 0, 0) - lights[0].dir).normalize();
        float bias = std::max(0.05f * (1.0f - interpolated.normal.dot(lightDir)), 0.005f);

        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                int row = static_cast<int>(std::min(
                    std::max((1.0f - projected.y) * SHADOW_MAP_SIZE + y, 0.0f),
                    static_cast<float>(SHADOW_MAP_SIZE) - 1.0f));
                int col = static_cast<int>(std::min(
                    std::max(projected.x * SHADOW_MAP_SIZE + x, 0.0f),
                    static_cast<float>(SHADOW_MAP_SIZE) - 1.0f));
                float closestDepth = shadowMap[row * SHADOW_MAP_SIZE + col];
                if ((projected.z - bias) > closestDepth) {
                    shadow += 1.0f;
                }
            }
        }
        shadow /= 9.0f;
    }
    return shadow;
}

Vec3 pixelShader(
    const VSOut& interpolated,
    const Mesh& mesh,
    const std::vector<Light>& lights,
    const Vec3& cameraPosition,
    const Mat4& lightProjection,
    const std::vector<float>& shadowMap,
    bool isSelected,
    float timeSeconds) {
    const Material& mat = mesh.material;

    Vec3 texColor = mat.diffuseMap
        ? mat.diffuseMap->sample(interpolated.uv) * mat.diffuse
        : mat.diffuse;

    if (isSelected) {
        float pulse = (std::sin(timeSeconds * 5.0f) + 1.0f) * 0.3f;
        texColor = texColor + Vec3(pulse, pulse * 0.5f, 0);
    }

    float shadow = sampleShadow(interpolated, lights, lightProjection, shadowMap);

    Vec3 normal = interpolated.normal.normalize();
    if (mat.normalMap) {
        Vec3 normalMapColor = mat.normalMap->sample(interpolated.uv);
        if (normalMapColor.length() > 0.1f) {
            Vec3 tangentNormal = normalMapColor * 2.0f - Vec3(1.0f, 1.0f, 1.0f);
            tangentNormal = tangentNormal.normalize();

            Vec3 tangent = (std::abs(normal.x) > 0.9f)
                ? Vec3(0.0f, 1.0f, 0.0f)
                : Vec3(1.0f, 0.0f, 0.0f);
            tangent = (tangent - normal * normal.dot(tangent)).normalize();
            Vec3 bitangent = normal.cross(tangent).normalize();
            normal = (tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z).normalize();
        }
    }

    Vec3 viewDir = (cameraPosition - interpolated.worldPos).normalize();
    Vec3 result(0, 0, 0);
    for (const auto& light : lights) {
        result = result + calculateLight(light, normal, viewDir, interpolated.worldPos, mat.shininess, shadow);
    }

    // 半球环境光：无方向光也有立体感，不依赖任何 Light
    float hemi = 0.5f + 0.5f * normal.dot(Vec3(0, 1, 0));
    Vec3 ambient = Vec3(0.25f, 0.27f, 0.30f) * (1.0f - hemi) + Vec3(0.65f, 0.70f, 0.80f) * hemi;
    result = result * texColor + texColor * ambient;

    if (mat.isTransparent) {
        float fresnel = std::pow(1.0f - std::max(0.0f, normal.dot(viewDir)), 3.0f);
        result = result * (1.0f - fresnel) + Vec3(0.8f, 0.9f, 1.0f) * fresnel;
    }

    return Vec3(
        result.x / (1 + result.x),
        result.y / (1 + result.y),
        result.z / (1 + result.z));
}

void drawShadowTriangle(VSOut v0, VSOut v1, VSOut v2, std::vector<float>& shadowMap) {
    if (v0.pos.w <= 0.1f || v1.pos.w <= 0.1f || v2.pos.w <= 0.1f) {
        return;
    }

    auto toScreen = [&](VSOut& vertex) {
        float inverseW = 1.0f / vertex.pos.w;
        vertex.pos.x = (vertex.pos.x * inverseW + 1.0f) * 0.5f * SHADOW_MAP_SIZE;
        vertex.pos.y = (1.0f - (vertex.pos.y * inverseW + 1.0f) * 0.5f) * SHADOW_MAP_SIZE;
        vertex.pos.z = vertex.pos.z * inverseW * 0.5f + 0.5f;
        vertex.pos.w = inverseW;
    };
    toScreen(v0);
    toScreen(v1);
    toScreen(v2);

    int minX = std::max(0, static_cast<int>(std::floor(std::min({ v0.pos.x, v1.pos.x, v2.pos.x }))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({ v0.pos.y, v1.pos.y, v2.pos.y }))));
    int maxX = std::min(SHADOW_MAP_SIZE - 1, static_cast<int>(std::ceil(std::max({ v0.pos.x, v1.pos.x, v2.pos.x }))));
    int maxY = std::min(SHADOW_MAP_SIZE - 1, static_cast<int>(std::ceil(std::max({ v0.pos.y, v1.pos.y, v2.pos.y }))));

    Vec2 p0(v0.pos.x, v0.pos.y);
    Vec2 p1(v1.pos.x, v1.pos.y);
    Vec2 p2(v2.pos.x, v2.pos.y);
    float area = edgeFunction(p0, p1, p2);
    if (std::abs(area) < 1e-5) {
        return;
    }

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            Vec2 point(x + 0.5f, y + 0.5f);
            float w0 = edgeFunction(p1, p2, point) / area;
            float w1 = edgeFunction(p2, p0, point) / area;
            float w2 = edgeFunction(p0, p1, point) / area;
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float depth = w0 * v0.pos.z + w1 * v1.pos.z + w2 * v2.pos.z;
                int index = y * SHADOW_MAP_SIZE + x;
                if (depth < shadowMap[index]) {
                    shadowMap[index] = depth;
                }
            }
        }
    }
}

void drawTriangle(
    VSOut v0,
    VSOut v1,
    VSOut v2,
    const Mesh& mesh,
    const Scene& scene,
    const Vec3& cameraPosition,
    const Mat4& lightProjection,
    const std::vector<float>& shadowMap,
    std::vector<float>& depthBuffer,
    std::vector<uint32_t>& colorBuffer,
    int width,
    int height,
    bool isSelected,
    float timeSeconds) {
    if (v0.pos.w <= 0.1f || v1.pos.w <= 0.1f || v2.pos.w <= 0.1f) {
        return;
    }

    auto toScreen = [&](VSOut& vertex) {
        float inverseW = 1.0f / vertex.pos.w;
        vertex.w = vertex.pos.w;
        vertex.pos.x = (vertex.pos.x * inverseW + 1.0f) * 0.5f * width;
        vertex.pos.y = (1.0f - (vertex.pos.y * inverseW + 1.0f) * 0.5f) * height;
        vertex.pos.z *= inverseW;
        vertex.uv.x *= inverseW;
        vertex.uv.y *= inverseW;
        vertex.pos.w = inverseW;
    };
    toScreen(v0);
    toScreen(v1);
    toScreen(v2);

    int minX = std::max(0, static_cast<int>(std::floor(std::min({ v0.pos.x, v1.pos.x, v2.pos.x }))));
    int minY = std::max(0, static_cast<int>(std::floor(std::min({ v0.pos.y, v1.pos.y, v2.pos.y }))));
    int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({ v0.pos.x, v1.pos.x, v2.pos.x }))));
    int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({ v0.pos.y, v1.pos.y, v2.pos.y }))));

    Vec2 p0(v0.pos.x, v0.pos.y);
    Vec2 p1(v1.pos.x, v1.pos.y);
    Vec2 p2(v2.pos.x, v2.pos.y);
    float area = edgeFunction(p0, p1, p2);
    if (std::abs(area) < 1e-5) {
        return;
    }

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            Vec2 point(x + 0.5f, y + 0.5f);
            float w0 = edgeFunction(p1, p2, point) / area;
            float w1 = edgeFunction(p2, p0, point) / area;
            float w2 = edgeFunction(p0, p1, point) / area;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }

            float depth = w0 * v0.pos.z + w1 * v1.pos.z + w2 * v2.pos.z;
            int index = y * width + x;
            if (depth >= depthBuffer[index]) {
                continue;
            }

            float oneOverW = w0 * v0.pos.w + w1 * v1.pos.w + w2 * v2.pos.w;
            float w = 1.0f / oneOverW;

            VSOut interpolated;
            interpolated.worldPos = w0 * v0.worldPos + w1 * v1.worldPos + w2 * v2.worldPos;
            interpolated.normal = (w0 * v0.normal + w1 * v1.normal + w2 * v2.normal).normalize();
            interpolated.uv.x = (w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x) * w;
            interpolated.uv.y = (w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y) * w;

            Vec3 color = pixelShader(
                interpolated,
                mesh,
                scene.lights,
                cameraPosition,
                lightProjection,
                shadowMap,
                isSelected,
                timeSeconds);

            uint8_t r = static_cast<uint8_t>(std::min(255.0f, color.x * 255));
            uint8_t g = static_cast<uint8_t>(std::min(255.0f, color.y * 255));
            uint8_t b = static_cast<uint8_t>(std::min(255.0f, color.z * 255));

            if (mesh.material.isTransparent) {
                uint32_t background = colorBuffer[index];
                uint8_t br = (background >> 16) & 0xFF;
                uint8_t bg = (background >> 8) & 0xFF;
                uint8_t bb = background & 0xFF;
                float alpha = mesh.material.alpha;
                r = static_cast<uint8_t>(r * alpha + br * (1 - alpha));
                g = static_cast<uint8_t>(g * alpha + bg * (1 - alpha));
                b = static_cast<uint8_t>(b * alpha + bb * (1 - alpha));
                colorBuffer[index] = (255 << 24) | (r << 16) | (g << 8) | b;
            } else {
                colorBuffer[index] = (255 << 24) | (r << 16) | (g << 8) | b;
                depthBuffer[index] = depth;
            }
        }
    }
}

Rasterizer::Rasterizer()
    : depthBuffer_(static_cast<size_t>(width_) * height_, 1.0f),
      colorBuffer_(static_cast<size_t>(width_) * height_, 0xFF202025),
      shadowMap_(static_cast<size_t>(SHADOW_MAP_SIZE) * SHADOW_MAP_SIZE, 1.0f) {
}

void Rasterizer::render(
    const Scene& scene,
    const Camera& camera,
    int selectedObjectIndex,
    float timeSeconds) {
    struct DrawItem {
        const Mesh* mesh;
        Mat4 world;
        int objectIndex;
        Vec3 sortPos;
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;

    for (int oi = 0; oi < static_cast<int>(scene.objects.size()); ++oi) {
        const SceneObject& obj = scene.objects[oi];
        if (!obj.visible) {
            continue;
        }
        Mat4 world = obj.transform.toMatrix();
        for (const Mesh& mesh : obj.model->meshes) {
            DrawItem item{ &mesh, world, oi, obj.transform.position };
            if (mesh.material.isTransparent) {
                transparent.push_back(item);
            } else {
                opaque.push_back(item);
            }
        }
    }

    // 清屏（无灯也要清，靠半球环境光可见）
    std::fill(depthBuffer_.begin(), depthBuffer_.end(), 1.0f);
    std::fill(colorBuffer_.begin(), colorBuffer_.end(), 0xFF202025);

    Mat4 viewProjection = camera.projection(static_cast<float>(width_) / height_) * camera.view();

    // 阴影图（仅当有方向光）
    Mat4 lightProjection;
    if (!scene.lights.empty()) {
        std::fill(shadowMap_.begin(), shadowMap_.end(), 1.0f);
        Vec3 lightPosition = Vec3(0, 0, 0) - scene.lights[0].dir * 20.0f;
        lightProjection = Mat4::ortho(-15, 15, -15, 15, 1, 50)
            * Mat4::lookAt(lightPosition, Vec3(0, 0, 0), Vec3(0, 1, 0));

        auto renderShadow = [&](const DrawItem& item) {
            for (size_t i = 0; i < item.mesh->indices.size(); i += 3) {
                VSOut v0;
                VSOut v1;
                VSOut v2;
                v0.pos = shadowVS(item.mesh->verts[item.mesh->indices[i]], item.world, lightProjection);
                v1.pos = shadowVS(item.mesh->verts[item.mesh->indices[i + 1]], item.world, lightProjection);
                v2.pos = shadowVS(item.mesh->verts[item.mesh->indices[i + 2]], item.world, lightProjection);
                drawShadowTriangle(v0, v1, v2, shadowMap_);
            }
        };
        for (const DrawItem& item : opaque) {
            renderShadow(item);
        }
        for (const DrawItem& item : transparent) {
            renderShadow(item);
        }
    }

    // 不透明
    for (const DrawItem& item : opaque) {
        bool isSelected = (item.objectIndex == selectedObjectIndex);
        for (size_t j = 0; j < item.mesh->indices.size(); j += 3) {
            drawTriangle(
                mainVS(item.mesh->verts[item.mesh->indices[j]], item.world, viewProjection),
                mainVS(item.mesh->verts[item.mesh->indices[j + 1]], item.world, viewProjection),
                mainVS(item.mesh->verts[item.mesh->indices[j + 2]], item.world, viewProjection),
                *item.mesh,
                scene,
                camera.position,
                lightProjection,
                shadowMap_,
                depthBuffer_,
                colorBuffer_,
                width_,
                height_,
                isSelected,
                timeSeconds);
        }
    }

    // 透明：back-to-front 排序后绘制
    std::sort(transparent.begin(), transparent.end(), [&](const DrawItem& a, const DrawItem& b) {
        return (a.sortPos - camera.position).length() > (b.sortPos - camera.position).length();
    });
    for (const DrawItem& item : transparent) {
        bool isSelected = (item.objectIndex == selectedObjectIndex);
        for (size_t j = 0; j < item.mesh->indices.size(); j += 3) {
            drawTriangle(
                mainVS(item.mesh->verts[item.mesh->indices[j]], item.world, viewProjection),
                mainVS(item.mesh->verts[item.mesh->indices[j + 1]], item.world, viewProjection),
                mainVS(item.mesh->verts[item.mesh->indices[j + 2]], item.world, viewProjection),
                *item.mesh,
                scene,
                camera.position,
                lightProjection,
                shadowMap_,
                depthBuffer_,
                colorBuffer_,
                width_,
                height_,
                isSelected,
                timeSeconds);
        }
    }
}
