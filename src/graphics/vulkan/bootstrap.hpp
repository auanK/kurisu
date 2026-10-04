#pragma once

#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace kurisu::graphics::vulkan {

inline constexpr size_t kMaxFramesInFlight = 2;

struct VulkanContext {
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily{0};
    VkQueue graphicsQueue{VK_NULL_HANDLE};

    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat swapchainFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent{0, 0};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline graphicsPipeline{VK_NULL_HANDLE};

    VkCommandPool commandPool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame{0};
};

bool initVulkan(SDL_Window* window, VulkanContext& ctx);
void recreateSwapchain(SDL_Window* window, VulkanContext& ctx);
bool renderFrame(SDL_Window* window, VulkanContext& ctx);
void cleanupVulkan(SDL_Window* window, VulkanContext& ctx);

} // namespace kurisu::graphics::vulkan
