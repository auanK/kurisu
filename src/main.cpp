#include "graphics/vulkan/bootstrap.hpp"

#include <SDL3/SDL.h>
#include <print>

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "Failed to initialize SDL: {}", SDL_GetError());
        return 1;
    }

    constexpr int kInitialWidth = 800;
    constexpr int kInitialHeight = 600;

    SDL_Window* window =
        SDL_CreateWindow("Kurisu - Vulkan Triangle",
                         kInitialWidth,
                         kInitialHeight,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!window) {
        std::println(stderr, "Failed to create SDL window: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    kurisu::graphics::vulkan::VulkanContext vkCtx{};
    if (!kurisu::graphics::vulkan::initVulkan(window, vkCtx)) {
        std::println(stderr, "Failed to initialize Vulkan bootstrap");
        kurisu::graphics::vulkan::cleanupVulkan(window, vkCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                kurisu::graphics::vulkan::recreateSwapchain(window, vkCtx);
            }
        }

        if (!kurisu::graphics::vulkan::renderFrame(window, vkCtx)) {
            break;
        }
    }

    kurisu::graphics::vulkan::cleanupVulkan(window, vkCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
