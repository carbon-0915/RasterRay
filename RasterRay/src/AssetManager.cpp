#define _CRT_SECURE_NO_WARNINGS

#include "AssetManager.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

using namespace std;

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

shared_ptr<const Texture> AssetManager::loadTexture(const string& path) {
    auto it = textureCache_.find(path);
    if (it != textureCache_.end()) return it->second;   // 命中缓存

    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);   // 强制 3 通道 RGB
    if (!data) {
        cerr << "[WARN] 纹理加载失败: " << path << endl;
        return nullptr;
    }

    auto tex = make_shared<Texture>();
    tex->w = w;
    tex->h = h;
    tex->pixels.resize(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 3;
            tex->pixels[y * w + x] = Vec3(data[idx] / 255.0f, data[idx + 1] / 255.0f, data[idx + 2] / 255.0f);
        }
    stbi_image_free(data);

    textureCache_[path] = tex;   // 存缓存（隐式转 shared_ptr<const Texture>）
    return tex;
}

unordered_map<string, Material> AssetManager::loadMTL(const string& filename, const string& basePath) {
    unordered_map<string, Material> materials;
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "[WARN] MTL 未找到: " << filename << endl;
        return materials;
    }

    cout << "[INFO] 加载 MTL: " << filename << endl;
    Material* cur = nullptr;
    string line;
    while (getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#') continue;

        istringstream iss(line);
        string type; iss >> type;

        if (type == "newmtl") {
            string name; iss >> name;
            cur = &materials[name];
            cout << "  [MAT] " << name << endl;
        } else if (cur) {
            if (type == "Kd")      iss >> cur->diffuse.x >> cur->diffuse.y >> cur->diffuse.z;
            else if (type == "Ns") iss >> cur->shininess;
            else if (type == "d") {                          // dissolve：不透明度（1=不透明）
                iss >> cur->alpha;
                cur->isTransparent = (cur->alpha < 1.0f);
            }
            else if (type == "Tr") {                         // transparency：与 d 互补
                float tr; iss >> tr;
                cur->alpha = 1.0f - tr;
                cur->isTransparent = (cur->alpha < 1.0f);
            }
            else if (type == "map_Kd") {                     // 漫反射颜色贴图
                string f; iss >> f;
                cur->diffuseMap = loadTexture(basePath + f);
            }
            else if (type == "map_Bump" || type == "bump" || type == "norm" || type == "map_normal") {
                // 法线贴图：跳过 -bm/-o 等参数，取最后一个非选项 token
                string f, tok;
                while (iss >> tok) { if (tok[0] == '-') { string v; iss >> v; } else f = tok; }
                if (!f.empty()) cur->normalMap = loadTexture(basePath + f);
            }
            // map_Ka / map_Ke / refl 等其它槽位：本阶段忽略
        }
    }
    cout << "[OK] MTL 完成，材质数=" << materials.size() << endl;
    return materials;
}

shared_ptr<const Model> AssetManager::loadOBJ(const string& path) {
    auto it = modelCache_.find(path);
    if (it != modelCache_.end()) return it->second;   // 命中缓存

    auto model = make_shared<Model>();
    model->sourcePath = path;

    size_t lastSlash = path.find_last_of("/\\");
    string basePath = (lastSlash != string::npos) ? path.substr(0, lastSlash + 1) : "";

    vector<Vec3> positions, normals;
    vector<Vec2> texCoords;
    unordered_map<string, Material> materials;

    ifstream file(path);
    if (!file.is_open()) {
        cerr << "[ERROR] OBJ 打不开: " << path << endl;
        return nullptr;   // 失败返回空指针，与 loadTexture 对齐，调用方可判空
    }

    cout << "\n[INFO] 加载 OBJ: " << path << endl;
    string line;

    // 第一遍：v / vt / vn / mtllib
    while (getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#') continue;

        istringstream iss(line);
        string type; iss >> type;

        if (type == "mtllib") {
            string mtl; iss >> mtl;
            materials = loadMTL(basePath + mtl, basePath);
        } else if (type == "v") {
            float x, y, z; iss >> x >> y >> z;
            positions.push_back(Vec3(x, y, z));
        } else if (type == "vn") {
            float x, y, z; iss >> x >> y >> z;
            normals.push_back(Vec3(x, y, z).normalize());
        } else if (type == "vt") {
            float u, v; iss >> u >> v;
            texCoords.push_back(Vec2(u, 1.0f - v));
        }
    }

    cout << "顶点统计: 位置=" << positions.size()
        << " 法线=" << normals.size()
        << " UV=" << texCoords.size() << endl;

    file.clear();
    file.seekg(0);

    // 第二遍：按 usemtl 拆多个 Mesh
    unordered_map<string, int> matToMesh;   // 材质名 -> meshes 下标
    vector<Mesh> meshes;
    int cur = -1;                            // 当前面片归属；-1 = 尚未遇到 usemtl

    while (getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#') continue;

        istringstream iss(line);
        string type; iss >> type;

        if (type == "usemtl") {
            string name; iss >> name;
            auto mit = materials.find(name);
            if (mit == materials.end()) continue;   // 未定义材质：保持当前材质不变
            auto mm = matToMesh.find(name);
            if (mm == matToMesh.end()) {            // 该材质第一次出现 -> 新建 Mesh
                meshes.emplace_back();
                meshes.back().material = mit->second;
                matToMesh[name] = (int)meshes.size() - 1;
                cur = (int)meshes.size() - 1;
            } else {
                cur = mm->second;                   // 已有 -> 复用
            }
        } else if (type == "f") {
            if (cur < 0) {                          // face 出现在任何 usemtl 之前：默认材质兜底
                meshes.emplace_back();
                cur = (int)meshes.size() - 1;
            }

            vector<int> vertIndices, texIndices, normalIndices;
            string vertex;
            while (iss >> vertex) {
                int vi = -1, ti = -1, ni = -1;
                size_t slash1 = vertex.find('/');
                if (slash1 == string::npos) {
                    vi = stoi(vertex) - 1;
                } else {
                    vi = stoi(vertex.substr(0, slash1)) - 1;
                    size_t slash2 = vertex.find('/', slash1 + 1);
                    if (slash2 == string::npos) {
                        if (slash1 + 1 < vertex.length())
                            ti = stoi(vertex.substr(slash1 + 1)) - 1;
                    } else {
                        if (slash2 > slash1 + 1)
                            ti = stoi(vertex.substr(slash1 + 1, slash2 - slash1 - 1)) - 1;
                        if (slash2 + 1 < vertex.length())
                            ni = stoi(vertex.substr(slash2 + 1)) - 1;
                    }
                }
                vertIndices.push_back(vi);
                texIndices.push_back(ti);
                normalIndices.push_back(ni);
            }

            for (size_t i = 1; i < vertIndices.size() - 1; ++i) {
                for (int j : {0, (int)i, (int)i + 1}) {
                    Vertex v;
                    if (vertIndices[j] >= 0 && vertIndices[j] < (int)positions.size())
                        v.pos = positions[vertIndices[j]];
                    if (texIndices[j] >= 0 && texIndices[j] < (int)texCoords.size())
                        v.uv = texCoords[texIndices[j]];
                    else
                        v.uv = Vec2(0, 0);
                    if (normalIndices[j] >= 0 && normalIndices[j] < (int)normals.size())
                        v.normal = normals[normalIndices[j]];
                    else
                        v.normal = Vec3(0, 1, 0);
                    meshes[cur].verts.push_back(v);
                    meshes[cur].indices.push_back((int)meshes[cur].indices.size());
                }
            }
        }
    }

    for (auto& m : meshes) m.localBounds = AABB::fromVertices(m.verts);
    model->meshes = move(meshes);

    modelCache_[path] = model;   // 存缓存（隐式转 shared_ptr<const Model>）
    return model;
}
