#include "graphics/vulkan/bootstrap.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <string_view>
#include <vector>

namespace kurisu::graphics::vulkan {

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/
) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::println(stderr, "[Vulkan Validation] {}", callbackData->pMessage);
    }
    return VK_FALSE;
}

auto loadShaderModule(VkDevice device, const std::filesystem::path& path)
    -> VkShaderModule {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::println(stderr, "Failed to open shader file: {}", path.string());
        return VK_NULL_HANDLE;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()),
              static_cast<std::streamsize>(fileSize));

    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fileSize,
        .pCode = buffer.data(),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
        VK_SUCCESS) {
        std::println(
            stderr, "Failed to create shader module for: {}", path.string());
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

auto resolveShaderPath(std::string_view filename) -> std::filesystem::path {
#ifdef KURISU_SHADERS_DIR
    const auto buildDirShader =
        std::filesystem::path(KURISU_SHADERS_DIR) / filename;
    if (std::filesystem::exists(buildDirShader)) {
        return buildDirShader;
    }
#endif
    const auto localShader = std::filesystem::path("shaders") / filename;
    if (std::filesystem::exists(localShader)) {
        return localShader;
    }
    return filename;
}

auto checkValidationLayerSupport() -> bool {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layer : availableLayers) {
        if (std::strcmp(layer.layerName, kValidationLayerName) == 0) {
            return true;
        }
    }
    return false;
}

auto createInstance(VulkanContext& ctx) -> bool {
    uint32_t sdlExtensionCount = 0;
    char const* const* sdlExtensions =
        SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions) {
        std::println(
            stderr, "Failed to get SDL Vulkan extensions: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> extensions(sdlExtensions,
                                        sdlExtensions + sdlExtensionCount);

    std::vector<const char*> layers;
    bool enableValidation = false;
    if constexpr (kEnableValidationLayers) {
        if (checkValidationLayerSupport()) {
            layers.push_back(kValidationLayerName);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            enableValidation = true;
        } else {
            std::println(
                stderr, "Khronos validation layer requested but not available");
        }
    }

    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Kurisu",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (vkCreateInstance(&createInfo, nullptr, &ctx.instance) != VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan instance");
        return false;
    }

    if (enableValidation) {
        auto createDebugUtilsMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(ctx.instance,
                                      "vkCreateDebugUtilsMessengerEXT"));

        if (createDebugUtilsMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{
                .sType =
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                .pfnUserCallback = debugCallback,
            };
            createDebugUtilsMessenger(
                ctx.instance, &debugInfo, nullptr, &ctx.debugMessenger);
        }
    }

    return true;
}

auto pickPhysicalDevice(VulkanContext& ctx) -> bool {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::println(stderr, "No Vulkan physical devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queueFamilyCount, queueFamilies.data());

        int graphicsIndex = -1;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device, i, ctx.surface, &presentSupport);

            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                presentSupport) {
                graphicsIndex = static_cast<int>(i);
                break;
            }
        }

        if (graphicsIndex < 0) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features feat13{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };
        VkPhysicalDeviceFeatures2 feat2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &feat13,
        };
        vkGetPhysicalDeviceFeatures2(device, &feat2);

        if (!feat13.dynamicRendering || !feat13.synchronization2) {
            continue;
        }

        ctx.physicalDevice = device;
        ctx.graphicsQueueFamily = static_cast<uint32_t>(graphicsIndex);
        return true;
    }

    std::println(
        stderr,
        "Failed to find a suitable Vulkan 1.3 physical device supporting "
        "dynamic rendering and synchronization2");
    return false;
}

auto createLogicalDevice(VulkanContext& ctx) -> bool {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx.graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan13Features feat13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &feat13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = deviceExtensions,
    };

    if (vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device) !=
        VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan logical device");
        return false;
    }

    vkGetDeviceQueue(
        ctx.device, ctx.graphicsQueueFamily, 0, &ctx.graphicsQueue);
    return true;
}

auto createSwapchain(SDL_Window* window, VulkanContext& ctx) -> bool {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx.physicalDevice, ctx.surface, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        ctx.physicalDevice, ctx.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        ctx.physicalDevice, ctx.surface, &formatCount, formats.data());

    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selectedFormat = availableFormat;
            break;
        }
    }
    ctx.swapchainFormat = selectedFormat.format;

    if (capabilities.currentExtent.width != UINT32_MAX) {
        ctx.swapchainExtent = capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        ctx.swapchainExtent = {
            .width = std::clamp(static_cast<uint32_t>(width),
                                capabilities.minImageExtent.width,
                                capabilities.maxImageExtent.width),
            .height = std::clamp(static_cast<uint32_t>(height),
                                 capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height),
        };
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 &&
        imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx.surface,
        .minImageCount = imageCount,
        .imageFormat = selectedFormat.format,
        .imageColorSpace = selectedFormat.colorSpace,
        .imageExtent = ctx.swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vkCreateSwapchainKHR(
            ctx.device, &createInfo, nullptr, &ctx.swapchain) != VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan swapchain");
        return false;
    }

    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imageCount, nullptr);
    ctx.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(
        ctx.device, ctx.swapchain, &imageCount, ctx.swapchainImages.data());

    ctx.swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx.swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx.swapchainFormat,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        if (vkCreateImageView(
                ctx.device, &viewInfo, nullptr, &ctx.swapchainImageViews[i]) !=
            VK_SUCCESS) {
            std::println(stderr,
                         "Failed to create Vulkan swapchain image view");
            return false;
        }
    }

    ctx.renderFinishedSemaphores.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i) {
        VkSemaphoreCreateInfo semInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (vkCreateSemaphore(ctx.device,
                              &semInfo,
                              nullptr,
                              &ctx.renderFinishedSemaphores[i]) != VK_SUCCESS) {
            std::println(stderr, "Failed to create render finished semaphore");
            return false;
        }
    }

    return true;
}

auto createGraphicsPipeline(VulkanContext& ctx) -> bool {
    const auto vertPath = resolveShaderPath("triangle.vert.spv");
    const auto fragPath = resolveShaderPath("triangle.frag.spv");

    VkShaderModule vertShaderModule = loadShaderModule(ctx.device, vertPath);
    VkShaderModule fragShaderModule = loadShaderModule(ctx.device, fragPath);

    if (vertShaderModule == VK_NULL_HANDLE ||
        fragShaderModule == VK_NULL_HANDLE) {
        if (vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(ctx.device, vertShaderModule, nullptr);
        }
        if (fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(ctx.device, fragShaderModule, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };

    if (vkCreatePipelineLayout(
            ctx.device, &pipelineLayoutInfo, nullptr, &ctx.pipelineLayout) !=
        VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan pipeline layout");
        vkDestroyShaderModule(ctx.device, vertShaderModule, nullptr);
        vkDestroyShaderModule(ctx.device, fragShaderModule, nullptr);
        return false;
    }

    VkPipelineRenderingCreateInfo renderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ctx.swapchainFormat,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCreateInfo,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = ctx.pipelineLayout,
    };

    const VkResult result = vkCreateGraphicsPipelines(ctx.device,
                                                      VK_NULL_HANDLE,
                                                      1,
                                                      &pipelineInfo,
                                                      nullptr,
                                                      &ctx.graphicsPipeline);

    vkDestroyShaderModule(ctx.device, vertShaderModule, nullptr);
    vkDestroyShaderModule(ctx.device, fragShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan graphics pipeline");
        return false;
    }

    return true;
}

auto createCommandObjects(VulkanContext& ctx) -> bool {
    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx.graphicsQueueFamily,
    };

    if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool) !=
        VK_SUCCESS) {
        std::println(stderr, "Failed to create Vulkan command pool");
        return false;
    }

    ctx.commandBuffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(kMaxFramesInFlight),
    };

    if (vkAllocateCommandBuffers(
            ctx.device, &allocInfo, ctx.commandBuffers.data()) != VK_SUCCESS) {
        std::println(stderr, "Failed to allocate Vulkan command buffers");
        return false;
    }

    return true;
}

auto createSyncObjects(VulkanContext& ctx) -> bool {
    ctx.imageAvailableSemaphores.resize(kMaxFramesInFlight);
    ctx.inFlightFences.resize(kMaxFramesInFlight);

    for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        if (vkCreateSemaphore(ctx.device,
                              &semaphoreInfo,
                              nullptr,
                              &ctx.imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(
                ctx.device, &fenceInfo, nullptr, &ctx.inFlightFences[i]) !=
                VK_SUCCESS) {
            std::println(
                stderr,
                "Failed to create Vulkan frame synchronization objects");
            return false;
        }
    }

    return true;
}

} // namespace

bool initVulkan(SDL_Window* window, VulkanContext& ctx) {
    if (!createInstance(ctx)) {
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(
            window, ctx.instance, nullptr, &ctx.surface)) {
        std::println(
            stderr, "Failed to create Vulkan surface: {}", SDL_GetError());
        return false;
    }

    if (!pickPhysicalDevice(ctx)) {
        return false;
    }

    if (!createLogicalDevice(ctx)) {
        return false;
    }

    if (!createSwapchain(window, ctx)) {
        return false;
    }

    if (!createGraphicsPipeline(ctx)) {
        return false;
    }

    if (!createCommandObjects(ctx)) {
        return false;
    }

    if (!createSyncObjects(ctx)) {
        return false;
    }

    return true;
}

void recreateSwapchain(SDL_Window* window, VulkanContext& ctx) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    vkDeviceWaitIdle(ctx.device);

    for (auto semaphore : ctx.renderFinishedSemaphores) {
        vkDestroySemaphore(ctx.device, semaphore, nullptr);
    }
    ctx.renderFinishedSemaphores.clear();

    for (auto imageView : ctx.swapchainImageViews) {
        vkDestroyImageView(ctx.device, imageView, nullptr);
    }
    ctx.swapchainImageViews.clear();

    if (ctx.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
        ctx.swapchain = VK_NULL_HANDLE;
    }

    createSwapchain(window, ctx);
}

bool renderFrame(SDL_Window* window, VulkanContext& ctx) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return true;
    }

    vkWaitForFences(ctx.device,
                    1,
                    &ctx.inFlightFences[ctx.currentFrame],
                    VK_TRUE,
                    UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireResult =
        vkAcquireNextImageKHR(ctx.device,
                              ctx.swapchain,
                              UINT64_MAX,
                              ctx.imageAvailableSemaphores[ctx.currentFrame],
                              VK_NULL_HANDLE,
                              &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(window, ctx);
        return true;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::println(stderr, "Failed to acquire Vulkan swapchain image");
        return false;
    }

    vkResetFences(ctx.device, 1, &ctx.inFlightFences[ctx.currentFrame]);

    VkCommandBuffer cmd = ctx.commandBuffers[ctx.currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier2 barrierToColor{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = ctx.swapchainImages[imageIndex],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo dependencyToColor{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierToColor,
    };
    vkCmdPipelineBarrier2(cmd, &dependencyToColor);

    VkClearValue clearColor = {
        .color = {.float32 = {0.08f, 0.09f, 0.12f, 1.0f}}};
    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = ctx.swapchainImageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = ctx.swapchainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindPipeline(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.graphicsPipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(ctx.swapchainExtent.width),
        .height = static_cast<float>(ctx.swapchainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = ctx.swapchainExtent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 barrierToPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = ctx.swapchainImages[imageIndex],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkDependencyInfo dependencyToPresent{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrierToPresent,
    };
    vkCmdPipelineBarrier2(cmd, &dependencyToPresent);

    vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo waitSemaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = ctx.imageAvailableSemaphores[ctx.currentFrame],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkCommandBufferSubmitInfo cmdBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };

    VkSemaphoreSubmitInfo signalSemaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = ctx.renderFinishedSemaphores[imageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };

    VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphoreInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdBufferInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphoreInfo,
    };

    if (vkQueueSubmit2(ctx.graphicsQueue,
                       1,
                       &submitInfo,
                       ctx.inFlightFences[ctx.currentFrame]) != VK_SUCCESS) {
        std::println(stderr, "Failed to submit draw command buffer");
        return false;
    }

    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &ctx.renderFinishedSemaphores[imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &ctx.swapchain,
        .pImageIndices = &imageIndex,
    };

    VkResult presentResult = vkQueuePresentKHR(ctx.graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain(window, ctx);
    } else if (presentResult != VK_SUCCESS) {
        std::println(stderr, "Failed to present Vulkan swapchain image");
        return false;
    }

    ctx.currentFrame = (ctx.currentFrame + 1) % kMaxFramesInFlight;

    return true;
}

void cleanupVulkan(SDL_Window* /*window*/, VulkanContext& ctx) {
    if (ctx.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx.device);
    }

    for (auto semaphore : ctx.renderFinishedSemaphores) {
        vkDestroySemaphore(ctx.device, semaphore, nullptr);
    }
    ctx.renderFinishedSemaphores.clear();

    for (auto semaphore : ctx.imageAvailableSemaphores) {
        vkDestroySemaphore(ctx.device, semaphore, nullptr);
    }
    ctx.imageAvailableSemaphores.clear();

    for (auto fence : ctx.inFlightFences) {
        vkDestroyFence(ctx.device, fence, nullptr);
    }
    ctx.inFlightFences.clear();

    if (ctx.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
        ctx.commandPool = VK_NULL_HANDLE;
    }

    if (ctx.graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.device, ctx.graphicsPipeline, nullptr);
        ctx.graphicsPipeline = VK_NULL_HANDLE;
    }
    if (ctx.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, nullptr);
        ctx.pipelineLayout = VK_NULL_HANDLE;
    }

    for (auto imageView : ctx.swapchainImageViews) {
        vkDestroyImageView(ctx.device, imageView, nullptr);
    }
    ctx.swapchainImageViews.clear();

    if (ctx.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
        ctx.swapchain = VK_NULL_HANDLE;
    }

    if (ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx.device, nullptr);
        ctx.device = VK_NULL_HANDLE;
    }

    if (ctx.surface != VK_NULL_HANDLE) {
        SDL_Vulkan_DestroySurface(ctx.instance, ctx.surface, nullptr);
        ctx.surface = VK_NULL_HANDLE;
    }

    if (ctx.debugMessenger != VK_NULL_HANDLE) {
        auto destroyDebugMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(ctx.instance,
                                      "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger) {
            destroyDebugMessenger(ctx.instance, ctx.debugMessenger, nullptr);
        }
        ctx.debugMessenger = VK_NULL_HANDLE;
    }

    if (ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx.instance, nullptr);
        ctx.instance = VK_NULL_HANDLE;
    }
}

} // namespace kurisu::graphics::vulkan
