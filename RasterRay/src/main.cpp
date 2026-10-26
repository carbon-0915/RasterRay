#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>

#include "render_core.h"
#include "AssetManager.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

using namespace std;

enum InteractionMode {
    MODE_CAMERA,
    MODE_OBJECT,
    MODE_LIGHT
};

// 编辑器状态：相机 + 选中 + 交互模式
struct EditorState {
    Camera camera;                 // 相机
    int    selectedObject = -1;    // 数组下标，-1 = 未选中
    int    selectedLight = 0;
    InteractionMode mode = MODE_CAMERA;
};

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow(
        "3D Scene | 1:Camera 2:Object 3:Light | Tab:Select | R:Rotate | F1-F3:Raytracing",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        WINDOW_WIDTH,
        WINDOW_HEIGHT);

    EditorState editor;
    Scene scene;

    // 本次：写死加载一个外部模型（具体路径待定，之后换成场景描述文件/命令行）
    const std::string modelPath = "models/RenWu/RenWu.obj";
    std::string modelName = modelPath;
    size_t slash = modelName.find_last_of("/\\");
    if (slash != std::string::npos) {
        modelName = modelName.substr(slash + 1);
    }

    auto model = assets().loadOBJ(modelPath);
    if (!model) {
        cerr << "[ERROR] 加载模型失败，退出: " << modelPath << endl;
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SceneObject obj;
    obj.name = modelName;
    obj.model = model;
    scene.add(obj);   // 只加入场景，不选中（selectedObject 保持 -1）

    editor.camera.frame(scene.bounds());

    Rasterizer rasterRenderer;
    RayTracer rayTracer;

    bool running = true;
    Uint64 lastTime = SDL_GetTicks();
    bool mouseDown = false;

    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "1: Camera mode | 2: Object mode | 3: Light mode" << std::endl;
    std::cout << "Tab: Select next object/light" << std::endl;
    std::cout << "WASD/QE: Move camera | Arrow keys: Rotate view" << std::endl;
    std::cout << "R + Mouse: Rotate selected object" << std::endl;
    std::cout << "Mouse drag: Move selected object/light" << std::endl;
    std::cout << "+/-: Adjust light intensity" << std::endl;
    std::cout << "F1/F2/F3: Render raytraced images from preset views" << std::endl;
    std::cout << "================\n" << std::endl;

    while (running) {
        Uint64 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                mouseDown = true;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                mouseDown = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                if (event.key.key == SDLK_1) {
                    editor.mode = MODE_CAMERA;
                    std::cout << "Mode: Camera" << std::endl;
                }
                if (event.key.key == SDLK_2) {
                    editor.mode = MODE_OBJECT;
                    std::cout << "Mode: Object" << std::endl;
                }
                if (event.key.key == SDLK_3) {
                    editor.mode = MODE_LIGHT;
                    std::cout << "Mode: Light" << std::endl;
                }

                if (event.key.key == SDLK_TAB) {
                    if (editor.mode == MODE_OBJECT) {
                        ++editor.selectedObject;
                        if (editor.selectedObject >= static_cast<int>(scene.objects.size())) {
                            editor.selectedObject = -1;
                        }
                        if (editor.selectedObject >= 0) {
                            std::cout << "Selected: " << scene.objects[editor.selectedObject].name << std::endl;
                        } else {
                            std::cout << "Selection cleared" << std::endl;
                        }
                    } else if (editor.mode == MODE_LIGHT && !scene.lights.empty()) {
                        editor.selectedLight = (editor.selectedLight + 1) % static_cast<int>(scene.lights.size());
                        std::cout << "Selected light: " << editor.selectedLight << std::endl;
                    }
                }

                if (editor.mode == MODE_LIGHT && !scene.lights.empty()) {
                    if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS) {
                        scene.lights[editor.selectedLight].intensity += 0.2f;
                    }
                    if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
                        scene.lights[editor.selectedLight].intensity =
                            std::max(0.1f, scene.lights[editor.selectedLight].intensity - 0.2f);
                    }
                }

                if (event.key.key == SDLK_F1) {
                    Camera cam;
                    cam.position = Vec3(8, 10, 10);
                    cam.target = Vec3(0, 3, 0);
                    rayTracer.render(scene, cam, "raytraced_view1.png");
                }
                if (event.key.key == SDLK_F2) {
                    Camera cam;
                    cam.position = Vec3(-10, 6, 5);
                    cam.target = Vec3(0, 3, 0);
                    rayTracer.render(scene, cam, "raytraced_view2.png");
                }
                if (event.key.key == SDLK_F3) {
                    rayTracer.render(scene, editor.camera, "raytraced_view3.png");
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseDown) {
                float dx = event.motion.xrel * 0.05f;
                float dy = event.motion.yrel * 0.05f;

                // 相机水平基（把鼠标拖拽映射到 XZ 平面）
                Vec3 right = editor.camera.right();
                Vec3 forwardFlat = Vec3(0, 1, 0).cross(right).normalize();
                Vec3 moveVector = right * dx - forwardFlat * dy;

                if (editor.mode == MODE_OBJECT && editor.selectedObject >= 0) {
                    const bool* keys = SDL_GetKeyboardState(NULL);
                    if (keys[SDL_SCANCODE_R]) {
                        scene.objects[editor.selectedObject].transform.rotation.y += dx * 0.5f;
                    } else {
                        scene.objects[editor.selectedObject].transform.position =
                            scene.objects[editor.selectedObject].transform.position + moveVector;
                    }
                } else if (editor.mode == MODE_LIGHT && !scene.lights.empty()) {
                    if (scene.lights[editor.selectedLight].type != LIGHT_DIRECTIONAL) {
                        scene.lights[editor.selectedLight].pos =
                            scene.lights[editor.selectedLight].pos + moveVector;
                    } else {
                        scene.lights[editor.selectedLight].dir.x += dx * 0.1f;
                        scene.lights[editor.selectedLight].dir.z += dy * 0.1f;
                        scene.lights[editor.selectedLight].dir =
                            scene.lights[editor.selectedLight].dir.normalize();
                    }
                }
            }
        }

        const bool* keys = SDL_GetKeyboardState(NULL);
        Vec3 forward = editor.camera.forward();
        Vec3 right = editor.camera.right();

        float moveSpeed = 8.0f * deltaTime;
        float rotateSpeed = 2.0f * deltaTime;

        // WASD/QE：平移相机（position 和 target 一起动，保持视角）
        Vec3 move(0, 0, 0);
        if (keys[SDL_SCANCODE_W]) move = move + forward;
        if (keys[SDL_SCANCODE_S]) move = move - forward;
        if (keys[SDL_SCANCODE_A]) move = move - right;
        if (keys[SDL_SCANCODE_D]) move = move + right;
        if (keys[SDL_SCANCODE_Q]) move.y -= 1;
        if (keys[SDL_SCANCODE_E]) move.y += 1;
        editor.camera.position = editor.camera.position + move * moveSpeed;
        editor.camera.target = editor.camera.target + move * moveSpeed;

        // 方向键：position 绕 target 环绕
        float dyaw = 0.0f, dpitch = 0.0f;
        if (keys[SDL_SCANCODE_LEFT]) dyaw -= rotateSpeed;
        if (keys[SDL_SCANCODE_RIGHT]) dyaw += rotateSpeed;
        if (keys[SDL_SCANCODE_UP]) dpitch += rotateSpeed;
        if (keys[SDL_SCANCODE_DOWN]) dpitch -= rotateSpeed;
        if (dyaw != 0.0f || dpitch != 0.0f) {
            Vec3 offset = editor.camera.position - editor.camera.target;
            float dist = offset.length();
            float yaw = std::atan2(offset.z, offset.x);
            float pitch = std::asin(std::max(-1.0f, std::min(1.0f, offset.y / dist)));
            yaw += dyaw;
            pitch = std::max(-1.5f, std::min(1.5f, pitch + dpitch));
            editor.camera.position = editor.camera.target + dist * Vec3(
                std::cos(pitch) * std::cos(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::sin(yaw));
        }

        rasterRenderer.render(
            scene,
            editor.camera,
            editor.selectedObject,
            currentTime / 1000.0f);

        SDL_UpdateTexture(
            texture,
            NULL,
            rasterRenderer.colorBuffer().data(),
            WINDOW_WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
