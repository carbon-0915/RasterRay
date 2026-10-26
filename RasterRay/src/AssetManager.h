#pragma once

#include "render_core.h"

#include <string>
#include <unordered_map>

// 资源管理器：贴图/模型的统一加载入口 + 去重缓存（方法全公开，缓存私有）
class AssetManager {
public:
    std::shared_ptr<const Texture> loadTexture(const std::string& path);
    std::unordered_map<std::string, Material> loadMTL(const std::string& filename,
                                                      const std::string& basePath);
    std::shared_ptr<const Model> loadOBJ(const std::string& path);

private:
    std::unordered_map<std::string, std::shared_ptr<const Texture>> textureCache_;  // 贴图缓存
    std::unordered_map<std::string, std::shared_ptr<const Model>>   modelCache_;     // 模型缓存
};

// 全局唯一实例访问器（Meyers 单例；inline 让声明与定义集中在此处）
inline AssetManager& assets() {
    static AssetManager instance;
    return instance;
}
