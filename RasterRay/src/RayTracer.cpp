#include "render_core.h"
#include "Renderer.h"

#include "../third_party/stb/stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace std;

struct Ray {
    Vec3 origin;
    Vec3 dir;
};

struct HitInfo {
    float t = 0.0f;
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
    const Mesh* mesh = nullptr;
    bool isTransparent = false;
};

bool rayTriangleIntersect(
    const Ray& ray,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    float& t,
    float& u,
    float& v) {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 h = ray.dir.cross(edge2);
    float determinant = edge1.dot(h);

    if (std::abs(determinant) < 1e-6f) {
        return false;
    }

    float inverseDeterminant = 1.0f / determinant;
    Vec3 s = ray.origin - v0;
    u = inverseDeterminant * s.dot(h);
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    Vec3 q = s.cross(edge1);
    v = inverseDeterminant * ray.dir.dot(q);
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    t = inverseDeterminant * edge2.dot(q);
    return t > 0.001f;
}

bool intersectAABB(const Ray& ray, const Vec3& minBounds, const Vec3& maxBounds) {
    float tmin = (minBounds.x - ray.origin.x) / ray.dir.x;
    float tmax = (maxBounds.x - ray.origin.x) / ray.dir.x;
    if (tmin > tmax) {
        std::swap(tmin, tmax);
    }

    float tymin = (minBounds.y - ray.origin.y) / ray.dir.y;
    float tymax = (maxBounds.y - ray.origin.y) / ray.dir.y;
    if (tymin > tymax) {
        std::swap(tymin, tymax);
    }
    if (tmin > tymax || tymin > tmax) {
        return false;
    }

    if (tymin > tmin) {
        tmin = tymin;
    }
    if (tymax < tmax) {
        tmax = tymax;
    }

    float tzmin = (minBounds.z - ray.origin.z) / ray.dir.z;
    float tzmax = (maxBounds.z - ray.origin.z) / ray.dir.z;
    if (tzmin > tzmax) {
        std::swap(tzmin, tzmax);
    }

    return !(tmin > tzmax || tzmin > tmax);
}

void testMesh(
    const Mesh& mesh,
    const Mat4& world,
    bool isTransparent,
    const Ray& ray,
    HitInfo& hit,
    bool& found) {
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        Vec4 wp0 = world * Vec4(mesh.verts[mesh.indices[i]].pos, 1);
        Vec4 wp1 = world * Vec4(mesh.verts[mesh.indices[i + 1]].pos, 1);
        Vec4 wp2 = world * Vec4(mesh.verts[mesh.indices[i + 2]].pos, 1);
        Vec3 v0(wp0.x, wp0.y, wp0.z);
        Vec3 v1(wp1.x, wp1.y, wp1.z);
        Vec3 v2(wp2.x, wp2.y, wp2.z);

        float t = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        if (rayTriangleIntersect(ray, v0, v1, v2, t, u, v) && t < hit.t) {
            hit.t = t;
            hit.pos = ray.origin + ray.dir * t;

            Vec3 nObj = (1 - u - v) * mesh.verts[mesh.indices[i]].normal
                + u * mesh.verts[mesh.indices[i + 1]].normal
                + v * mesh.verts[mesh.indices[i + 2]].normal;
            Vec4 nW = world * Vec4(nObj, 0.0f);
            hit.normal = Vec3(nW.x, nW.y, nW.z).normalize();

            hit.uv.x = (1 - u - v) * mesh.verts[mesh.indices[i]].uv.x
                + u * mesh.verts[mesh.indices[i + 1]].uv.x
                + v * mesh.verts[mesh.indices[i + 2]].uv.x;
            hit.uv.y = (1 - u - v) * mesh.verts[mesh.indices[i]].uv.y
                + u * mesh.verts[mesh.indices[i + 1]].uv.y
                + v * mesh.verts[mesh.indices[i + 2]].uv.y;

            hit.mesh = &mesh;
            hit.isTransparent = isTransparent;
            found = true;
        }
    }
}

bool traceRay(const Scene& scene, const Ray& ray, HitInfo& hit) {
    hit.t = 1e30f;
    bool found = false;

    for (const SceneObject& obj : scene.objects) {
        if (!obj.visible) {
            continue;
        }
        AABB worldBox = obj.worldBounds();
        if (!intersectAABB(ray, worldBox.min, worldBox.max)) {
            continue;
        }
        Mat4 world = obj.transform.toMatrix();
        for (const Mesh& mesh : obj.model->meshes) {
            testMesh(mesh, world, mesh.material.isTransparent, ray, hit, found);
        }
    }

    return found;
}

Vec3 shadeRayTraced(
    const Scene& scene,
    const HitInfo& hit,
    const Vec3& cameraPosition,
    const Vec3& rayDirection,
    int depth) {
    if (depth > 3) {
        return Vec3(0.1f);
    }

    const Mesh& mesh = *hit.mesh;
    const Material& mat = mesh.material;

    Vec3 texColor = mat.diffuseMap
        ? mat.diffuseMap->sample(hit.uv) * mat.diffuse
        : mat.diffuse;

    Vec3 normal = hit.normal.normalize();
    Vec3 viewDir = (cameraPosition - hit.pos).normalize();
    Vec3 result(0, 0, 0);

    for (const auto& light : scene.lights) {
        Vec3 lightDir;
        float attenuation = 1.0f;

        if (light.type == LIGHT_DIRECTIONAL) {
            lightDir = (Vec3(0, 0, 0) - light.dir).normalize();
        } else {
            lightDir = (light.pos - hit.pos).normalize();
            float distance = (light.pos - hit.pos).length();
            attenuation = 1.0f / (light.constant + light.linear * distance + light.quadratic * distance * distance);
        }

        Ray shadowRay;
        shadowRay.origin = hit.pos + normal * 0.01f;
        shadowRay.dir = lightDir;

        HitInfo shadowHit;
        float shadow = traceRay(scene, shadowRay, shadowHit) ? 0.3f : 1.0f;

        float diffuse = std::max(normal.dot(lightDir), 0.0f);
        Vec3 halfway = (lightDir + viewDir).normalize();
        float specular = std::pow(std::max(normal.dot(halfway), 0.0f), mat.shininess);

        result = result + light.color * (diffuse + specular * 0.5f)
            * light.intensity * attenuation * shadow;
    }

    // 半球环境光
    float hemi = 0.5f + 0.5f * normal.dot(Vec3(0, 1, 0));
    Vec3 ambient = Vec3(0.25f, 0.27f, 0.30f) * (1.0f - hemi) + Vec3(0.65f, 0.70f, 0.80f) * hemi;
    result = result * texColor + texColor * ambient;

    if (mat.isTransparent && depth < 2) {
        Vec3 reflectionDirection = rayDirection - normal * 2.0f * rayDirection.dot(normal);
        Ray reflectionRay;
        reflectionRay.origin = hit.pos + normal * 0.01f;
        reflectionRay.dir = reflectionDirection.normalize();

        HitInfo reflectionHit;
        if (traceRay(scene, reflectionRay, reflectionHit)) {
            Vec3 reflectionColor = shadeRayTraced(
                scene,
                reflectionHit,
                cameraPosition,
                reflectionRay.dir,
                depth + 1);
            result = result * 0.7f + reflectionColor * 0.3f;
        }
    }

    return result;
}

void RayTracer::render(
    const Scene& scene,
    const Camera& camera,
    const std::string& filename) const {
    std::cout << "Rendering raytraced image: " << filename << "..." << std::endl;
    std::vector<uint8_t> image(WINDOW_WIDTH * WINDOW_HEIGHT * 3);

    for (int y = 0; y < WINDOW_HEIGHT; ++y) {
        for (int x = 0; x < WINDOW_WIDTH; ++x) {
            Ray ray;
            ray.origin = camera.position;
            ray.dir = camera.rayDirection(x, y, WINDOW_WIDTH, WINDOW_HEIGHT);

            HitInfo hit;
            Vec3 color(0.12f, 0.12f, 0.14f);
            if (traceRay(scene, ray, hit)) {
                color = shadeRayTraced(scene, hit, camera.position, ray.dir, 0);
            }

            int index = (y * WINDOW_WIDTH + x) * 3;
            image[index] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, color.x) * 255));
            image[index + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, color.y) * 255));
            image[index + 2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, color.z) * 255));
        }

        if (y % 50 == 0) {
            std::cout << " Progress: " << (static_cast<float>(y) * 100.0f / WINDOW_HEIGHT) << "%" << std::endl;
        }
    }

    if (stbi_write_png(filename.c_str(), WINDOW_WIDTH, WINDOW_HEIGHT, 3, image.data(), WINDOW_WIDTH * 3)) {
        std::cout << "[OK] Saved: " << filename << std::endl;
    } else {
        std::cerr << "[ERROR] Failed to save PNG: " << filename << std::endl;
    }
}
