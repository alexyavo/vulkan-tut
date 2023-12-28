#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <unordered_map>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <optional>
#include <set>
#include <limits>
#include <fstream>
#include <chrono>
#include <array>
#include <algorithm>

#include <fmt/core.h>

// when moving vertex data from shader code to an array in the code of the application, we
// include this to provide us with linear algebra related types like vectors and matrices
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

// wasn't able to get stb lib with vcpkg (although its still present in vcpkg.json manifest)
// so installed the apt package instead:
// sudo apt install libstb-dev
#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION

#include <tiny_obj_loader.h>

// being explicit about alignment requirements
struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

// hash function for Vertex (for unordered_map)
// implemented by specifying template specialization for std::hash<T>


struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
  glm::vec2 texCoord;

  // needed for unordered_map
  bool operator==(const Vertex &other) const {
    return pos == other.pos &&
           color == other.color &&
           texCoord == other.texCoord;

  }

  // why the following static methods are needed:
  // we need to tell vulkan how to pass vertex data format to the vertex shader
  // once it's been uploaded into GPU memory. both structures are needed to convey
  // this information

  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
  }

  // TODO(cpp): do I get a compile error if i use array<..., > and do arr[x] / arr[x+1]?
  static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
  }
};

namespace std {
  template<>
  struct hash<Vertex> {
    size_t operator()(Vertex const &vertex) const {
      return ((hash<glm::vec3>()(vertex.pos) ^
               (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
             (hash<glm::vec2>()(vertex.texCoord) << 1);
    }
  };
}

std::vector<char> readf(const std::string &filename) {
  std::ifstream f(
      filename,
      // start reading at the end of the file
      // we do this because we can determine the size of the file and allocate a buffer for it
      std::ios::ate |
      std::ios::binary
  );

  if (!f.is_open()) {
    throw std::runtime_error(fmt::format("failed to open file: {}", filename));
  }

  size_t fsize = static_cast<size_t>(f.tellg());
  std::vector<char> buffer(fsize);

  f.seekg(0);
  f.read(buffer.data(), fsize);
  f.close();

  return buffer;  // TODO(cpp): how bad is this in terms of redundant copies?
}

// in vulkan anything from drawing to uploading textures, requires COMMANDS to be submitted to a QUEUE
// there are different types of QUEUEs that originate from different QUEUE FAMILIES
// each family allows only a subset of commands
// e.g. a family that allows processing of compute commands only
//      or one that allows memory transfer related commands
struct QueueFamilyIndices {
  // any value of uint32_t could in theory be a valid queue family index including 0
  std::optional<uint32_t> graphicsFamily;

  // TODO(vulkan): difference between graphics and present family?
  //               graphics == compute, present == purely about presenting on the Surface object?
  std::optional<uint32_t> presentFamily;

  bool isComplete() {
    return graphicsFamily.has_value() && presentFamily.has_value();
  }
};

struct SwapChainSupportDetails {
  // min/max number of images in swap chain
  // min/max width and height of images
  VkSurfaceCapabilitiesKHR capabilities;

  // pixel format, color space
  std::vector<VkSurfaceFormatKHR> formats;

  // available presentation modes
  // TODO(vulkan) presentation modes == ?
  std::vector<VkPresentModeKHR> presentModes;
};


// The
// VkDebugUtilsMessengerCreateInfoEXT struct should be passed to the
// vkCreateDebugUtilsMessengerEXT function
// but because this function is an extension function it is not automatically loaded
// ==> we have to look up its address ourselves using vkGetInstanceProcAddr
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger
) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
      instance,
      "vkCreateDebugUtilsMessengerEXT"
  );

  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator
) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
      instance,
      "vkDestroyDebugUtilsMessengerEXT"
  );

  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}


class HelloTriangleApplication {
public:
  const uint32_t WIDTH = 800;
  const uint32_t HEIGHT = 600;

  const std::string MODEL_PATH = "models/viking_room.obj";
  const std::string TEXTURE_PATH = "textures/viking_room.png";

  // each frame should have its own command buffer, set of semaphores and fence.
  const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

  const std::vector<const char *> validationLayers = {
      "VK_LAYER_KHRONOS_validation"
  };

  const std::vector<const char *> deviceExtensions = {
      // vulkan has no defauilt framebuffer
      // it requires an infrastructure that iwll own the buffers we will render to before we visualize them on the screen
      // this infrastructure is the swap chain
      // the swap chain is a queue of images that are waiting to be presented on the screen
      VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

  // default: one sample per pixel, which is equivalent to no multisampling
  VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  void initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // don't create OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // last parameter relevant only for OpenGL
    // monitor param controls which monitor the window will be created on
    window = glfwCreateWindow((int) WIDTH, (int) HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    createColorResources();
    createDepthResources();
    createFramebuffers(); // must be after createDepthResources
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    loadModel();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      drawFrame();
    }

    // all operations in drawFrame are async
    // so when we exit the loop drawing and presentation opeartions mayh still be going on[
    //
    // it is also possible to wait for operations in a specific command queue to be finished with
    // vkQueueWaitIdle
    vkDeviceWaitIdle(device);
  }

  void cleanup() {
    cleanupSwapchain();

    vkDestroySampler(device, textureSampler, nullptr);

    vkDestroyImageView(device, textureImageView, nullptr);

    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroyBuffer(device, uniformBuffers[i], nullptr);
      vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }

    // descriptor sets are automatically freed when descriptor pool is destroyed
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    // descriptor layout should stick around while we may create new graphics pipelines
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    // should be available for use in rendering commands until the end of the program and it does
    // not depend on the swap chain
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);

    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
      vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
      vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    // queues are automatically cleaned up when their logical device is destroyed
    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  void createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
      throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo = {};

    // many structs in VUlkan require you to explicitly specify the type in the sType member.
    // This is also one of the many structs with a pNext member that can point to extension information in the future.
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // tells the Vulkan driver which *global availableVkExtensions* and *validation layers* we want to use
    // global == they apply to the entire program and not a specific device
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // TODO(cpp) difference between empty decleration and '= {}'?
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

    // validation layers
    if (enableValidationLayers) {
      createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
      createInfo.ppEnabledLayerNames = validationLayers.data();

      populateDebugMessengerCreateInfo(debugCreateInfo);
      createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *) &debugCreateInfo;
    } else {
      createInfo.enabledLayerCount = 0;
      createInfo.pNext = nullptr;
    }

    // retrieve a list of supported availableVkExtensions before creating an instance
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableVkExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableVkExtensions.data());

    std::cout << "available extensions:\n";
    for (const auto &extension: availableVkExtensions) {
      std::cout << "\t" << extension.extensionName << "\n";
    }

    // Vulkan is platform-agnostic, which means that you need an extension to interface with the window system
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    // the extensions specified by GLFW are always required
    std::vector<const char *> enabledExtensionNames(glfwExtensions, glfwExtensions + glfwExtensionCount);

    // the debug messenger extension is conditionally added
    if (enableValidationLayers) {
      enabledExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    uint32_t supportedExtensionCount = 0;
    std::vector<const char *> unsupportedExtensions;
    std::cout << "required availableVkExtensions:\n";
    for (auto &enabledExtension: enabledExtensionNames) {
      std::cout << "\t" << enabledExtension << "\n";
      for (const auto &extension: availableVkExtensions) {
        if (strcmp(enabledExtension, extension.extensionName) == 0) {
          ++supportedExtensionCount;
        } else {
          unsupportedExtensions.push_back(enabledExtension);
        }
      }
    }

    if (supportedExtensionCount != enabledExtensionNames.size()) {
      std::cout << "missing support for following required availableVkExtensions:\n";
      for (const auto &extension: unsupportedExtensions) {
        std::cout << "\t" << extension << "\n";
      }

      throw std::runtime_error("not all required availableVkExtensions are supported!");
    } else {
      std::cout << "all required availableVkExtensions are supported ...\n";
    }

    createInfo.enabledExtensionCount = enabledExtensionNames.size();
    createInfo.ppEnabledExtensionNames = enabledExtensionNames.data();

    // this follows the general pattern for object creation in Vulkan:
    // - pointer to struct w/ creation info
    // - pointer to custom allocator callbacks
    // - pointer to variable that will store the newly created object
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to create instance!",
          static_cast<int>(result)));
    }
  }

  bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName: validationLayers) {
      std::cout << fmt::format("checking support for validation layer: {}\n", layerName);

      bool layerFound = false;
      for (const auto &layerProperties: availableLayers) {
        if (strcmp(layerName, layerProperties.layerName) == 0) {
          layerFound = true;
          break;
        }
      }

      if (!layerFound) {
        std::cout << fmt::format("validation layer {} not found!\n", layerName);
        return false;
      }
    }

    return true;
  }

  static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
    auto app = reinterpret_cast<HelloTriangleApplication *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageType,
      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
      void *pUserData
  ) {
    std::cerr << fmt::format("[validation layer callback] {}\n", pCallbackData->pMessage);
    return VK_FALSE;
  }

  // we have two places where we need to set the fields of this "create info" type:
  // once in the setupdebugmessenger function which is called from initvulkan
  // then in createinstance
  // the reason for creating this in createinstance is if we want the debug messenger layer thing to
  // output information during the create instance phase
  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    // all except for VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    // all types are enabled here
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    createInfo.pfnUserCallback = debugCallback;

    // can, for example, pass pointer to this (HelloTriangleApplication) here
    createInfo.pUserData = nullptr;
  }

  void setupDebugMessenger() {
    if (!enableValidationLayers) {
      return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    populateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(
        instance,
        &createInfo,
        nullptr,
        &debugMessenger
    ) != VK_SUCCESS) {
      throw std::runtime_error("failed to set up debug messenger!");
    }
  }

  // need to check which queue families are supported by the device
  // and which of these families supports the commands that we want to use
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());


    int i = 0;
    for (const auto &queueFamily: queueFamilies) {
      if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        indices.graphicsFamily = i;
      }

      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
      if (presentSupport) {
        indices.presentFamily = i;
      }

      if (indices.isComplete()) {
        break;
      }

      ++i;
    }

    return indices;
  }

  // look for and select a graphics card in the systme that supports the features we need
  // we can select any number of graphics cards and use them simultaneously
  void pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto &device: devices) {
      VkPhysicalDeviceProperties deviceProperties;
      vkGetPhysicalDeviceProperties(device, &deviceProperties);
      VkPhysicalDeviceFeatures deviceFeatures;
      vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

      QueueFamilyIndices indices = findQueueFamilies(device);
      SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
      bool swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();

      // isDeviceSuitable
      if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
          deviceFeatures.geometryShader &&
          deviceFeatures.samplerAnisotropy &&
          indices.isComplete() &&
          checkDeviceExtensionSupport(device) &&
          swapChainAdequate) {
        physicalDevice = device;
        msaaSamples = getMaxUsableSampleCount();
        break;
      }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
  }

  void createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };
    float queuePriority = 1.0f;

    for (uint32_t queueFamily: uniqueQueueFamilies) {
      VkDeviceQueueCreateInfo queueCreateInfo = {};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = queueFamily;
      queueCreateInfo.queueCount = 1;
      queueCreateInfo.pQueuePriorities = &queuePriority;
      queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    // TODO(cpp): difference between "x;", "x{};", "x = {};" ?
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;  // enable anisotropy, physical device must support this
    deviceFeatures.sampleRateShading = VK_TRUE; // enable sample shading feature
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    // similar to VkInstanceCreateInfo, but these are now device specific
    // e.g. VKK_KHR_swapchain is device specific extension that allows you to present rendered images
    // from that device to windows

    // vulkan _no longer_ makes a distinction between instance and device specific validation layers
    // ==> enabledLayerCount and ppEnabledLayerNames are ignored
    //    createInfo.enabledLayerCount
    //    createInfo.ppEnabledLayerNames

    createInfo.enabledExtensionCount = deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult res = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (res != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to create logical device!",
          static_cast<int>(res)
      ));
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
  }

  void createSurface() {
    VkResult res = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (res != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to create window surface!",
          static_cast<int>(res)
      ));
    }
  }

  bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableVkExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableVkExtensions.data());

    uint32_t actuallySupportedCount = 0;
    for (const auto &requiredExtension: deviceExtensions) {
      for (const auto &availableExtension: availableVkExtensions) {
        if (strcmp(requiredExtension, availableExtension.extensionName) == 0) {
          ++actuallySupportedCount;
          break;
        }
      }
    }

    return actuallySupportedCount == deviceExtensions.size();
  }

  SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
      details.presentModes.resize(presentModeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
  }

  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat: availableFormats) {
      // format specifies the color channels and types
      // VK_FORMAT_B8G8R8A8_SRGB means that we store the B, G, R and alpha channels in that order with
      // an 8 bit unsigned integer for a total of 32 bits per pixel

      // the colorSpace member indicates if the SRGB color space is supported or not using the
      // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR flag

      // for color space we want to use SRGB color format, because it results in more accurate perceived colors
      // it is also the standard color space for images (like textures)
      // & the most common SRGB format is VK_FORMAT_B8G8R8A8_SRGB
      if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
          availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return availableFormat;
      }
    }

    return availableFormats[0];
  }

  // TODO(vulkan) meaning of KHR suffix?
  // ANSWER: vulkan has a bunch of extensions that add functionality beyond the core specification.
  // these extensions are typically denoted with dfifferent suffixes.
  // KHR extensions are ratified by the Khronos Group, often cross-vendor and widely supported,
  // forming a part of the standard but not included in the core Vulkan specification.
  // other examples of extension suffixes: EXT, NV, AMD, INTEL


  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto &presentMode: availablePresentModes) {
      // helps avoid tearing (which happens if we choose VK_PRESENT_MODE_IMMEDIATE_KHR)
      // but avoids the latency issues of VK_PRESENT_MODE_FIFO_KHR
      // on mobile devices, VK_PRESENT_MODE_FIFO_KHR is better because it consumes less power
      if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return presentMode;
      }
    }

    // the only mode that is guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  // swap extent == resolution of the swap chain images and it's almost always exactly equal
  // to the resolution of the window that we're drawing to in pixels
  // range of possible resolutions is defined in the VkSurfaceCapabilitiesKHR struct
  //
  // also: two measuring sizes: pixels and screen coordinates.
  // with high DPI displays, screen coordinates don't correspond to pixels
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    // if the width isn't the maximum value of uint32_t it means the window manager (GLFW) does not
    // allow us to differ from the window size, and so we just use that.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    }

    int w, h;

    // query the resolution of the window in pixel
    glfwGetFramebufferSize(window, &w, &h);
    VkExtent2D actualExtent = {
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h),
    };

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);

    return actualExtent;
  }

  void createSwapChain() {
    SwapChainSupportDetails details = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(details.presentModes);
    VkExtent2D extent = chooseSwapExtent(details.capabilities);

    // decide how many images we would like to have in the swap chain
    // if we stick to the minimum we may sometimes have to wait on the driver to complete
    // internal operation before we can acquire another image to render to
    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
      imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;

    // specifies amount of layers each image consists of. this is always 1 unless you are developing
    // a stereoscopic 3D application
    createInfo.imageArrayLayers = 1;

    // specifies what kind of operations we'll us the images in the swap chain for
    // we'll be rendering directly oto them, which means that they're used as color attachment
    //
    // you may render images to a sepaarate image first to perform operations like post-processing
    // in that case you may want to use a value like VK_IMAGE_USAGE_TRANSFER_DST_BIT instead
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
      // avoid having to do the ownership thing to transfer images from one queue to another
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
      // best performance
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      createInfo.queueFamilyIndexCount = 0;
      createInfo.pQueueFamilyIndices = nullptr;
    }

    // apply certain transform to images in the swap chain (see supportedTransforms in capabilities)
    // e.g. 90-degree rotation or horizontal flip
    // here we specify we don't want any transformation
    createInfo.preTransform = details.capabilities.currentTransform;

    // used for blending with other windows in the window system. here we want to ignore this ability
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    createInfo.presentMode = presentMode;

    // if VK_TRUE, we don't care about the color of p[ixels that are obscured, for example because another window
    // is in front of them
    // clipping enabled == better performance
    createInfo.clipped = VK_TRUE;

    // this might be used when a window resizes for example and we need to create a new swap chain while
    // still keeping the old one alive until we can move on to the new one
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult res = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain);
    if (res != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to create swap chain!",
          static_cast<int>(res)
      ));
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
  }

  void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); ++i) {
      swapChainImageViews[i] = createImageView(
          swapChainImages[i],
          swapChainImageFormat,
          VK_IMAGE_ASPECT_COLOR_BIT,
          1
      );
    }
  }

  // (Vertex/Index buffer fed into)
  // - input assembler
  // - vertex shader
  // - tessellation
  // - geometry shader
  // - rasterization
  // - fragment shader
  // - color blending
  // comes out to a framebuffer
  void createGraphicsPipeline() {
    // shader code in vulkan has to be specified in bytecode format called SPIR-V
    // which is designed to be used with both Vulkan and OpenCL
    //   OpenCL == open standard for general purpose parallel programming of heterogeneous systems
    //
    // with human readable syntax like GLSL some GPU vendors were somewhat loose in their interpretations, an issue
    // which SPPIR-V bytecode should solve
    //
    // there exists a compiler from GLSL to SPIR-V
    // we'll be using glslc by google
    // GLSL is a shading language with C-style syntax
    // main function that is invoked for every object
    //
    // we need a "vertex shader" and a "fragment shader" to get a triangle on the screen
    auto vertShaderCode = readf("shaders/vert.spv");
    auto fragShaderCode = readf("shaders/frag.spv");

    // compilation of the SPIR-V bytecode to machine code for execution by the GPU doesn't happen
    // until the graphcis pipeline is created
    // ==> we can destroy shader modules as soon as pipeline creation is finished
    // ==> we use locals
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;

    // it's possible to combine multiple fragment shaders into a single shader module
    // and use different entry points to differentiate between their behaviors
    vertShaderStageInfo.pName = "main";

    // allows to specifyt values for shader constants
    // you can use a single shader module where its behavior can be configured at pipeline
    // creation by specifying different values for the constants used in it.
    // this way is more efficient than configuring the shader using variables at render time because
    // the compiler can do optimizations like eliminating if statements that depend on these values
    vertShaderStageInfo.pSpecializationInfo = nullptr;

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo,
        fragShaderStageInfo
    };

    // ********************************************************************************
    //
    // ********************************************************************************
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    // describes format of the vertex data that will be passed to the vertex shader
    // we're hard coding vertex data directly to vertex shader, we fill this to specify
    // that there is no vertex data to load for now
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t) attributeDescriptions.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // ********************************************************************************
    //
    // ********************************************************************************

    // describes what kind of geometry will be drawn from the vertices
    // and if primitive restart (== ?? ) should be enabled
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;


    // ********************************************************************************
    //
    // ********************************************************************************

    // describes the region of the framebuffer that the output will be rendered to
    // almost always (0,0) to (width, height)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    // viewports define transformation from iamge to framebuffer
    // scissor rectangles define in which regions pixels will actually be stored
    // the following draws the entire framebuffer
    //
    // viewports and scissor rectangles can either be specified as static part of the pipeline
    // or as a dynamic state set in the command buffer
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;

    // ********************************************************************************
    //
    // ********************************************************************************

    // limited amount of the state can actually be changed without recreating the pipeline at draw time
    // exampoles: viewport size, line width, blend constants
    // to use dynamic state at and keep these properties out you'll need to:
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // ********************************************************************************
    //
    // ********************************************************************************

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // ********************************************************************************
    //
    // ********************************************************************************

    // rasterizer takes geometry that is shaped by the vertices from the vertex shader
    // and turns it into fragments to be colored by the fragment shader
    // also performs depth testing, face culling, and the scissor test, and it can be
    // configured to output fragments that fill the entire polygons or just the edges
    // (aka wireframe rendering)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

    // if set to true then fragments that are beyond the near and far planes are clamped to them
    // as opposed to discrding them,. usefull in some special cases like shadow maps.
    // requires enabling GPU feature
    rasterizer.depthClampEnable = VK_FALSE;

    // if true, geometry never passes through the rasterizer stage. basically disables any output
    // to the framebuffer
    rasterizer.rasterizerDiscardEnable = VK_FALSE;

    // using any other mode than fill requires a GPU feature
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;

    rasterizer.lineWidth = 1.0f;

    // determines type of face culling to use
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;

    // specifies vertex order for faces to be considered front-facing
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // sometimes used for shadow mapping (??)
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // optional
    rasterizer.depthBiasClamp = 0.0f; // optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // optional

    // ********************************************************************************
    //
    // ********************************************************************************

    // one of the ways to perform anti-aliasing
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE; // enable sample shading
    multisampling.rasterizationSamples = msaaSamples;
    multisampling.minSampleShading = 0.2f; // min fraction for sample shading: closer to 1 == smoother
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    // ********************************************************************************
    //
    // ********************************************************************************

    /*
     *

if (blendEnable) {
    finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
    finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
} else {
    finalColor = newColor;
}

finalColor = finalColor & colorWriteMask;

     */
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

    // ********************************************************************************
    //
    // ********************************************************************************
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    // ********************************************************************************
    //
    // ********************************************************************************

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    // ********************************************************************************
    //
    // ********************************************************************************

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {}; // Optional
    depthStencil.back = {}; // Optional

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    // it is less expensive to set up pipelines when they have much functionality in common with an existing
    // pipeline and switchin between pielines from the same parent can also be done quicker
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    if (vkCreateGraphicsPipelines(
        device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
  }

  void createRenderPass() {
    // frame buffer attachments that will be used while rendering
    // need to specify how manmy color and epth buffers there will be, how many
    // samples to use for each of them and how their contents should be handled throughout the
    // rendering operations

    VkAttachmentDescription colorAttachment{};
    // format of color attachment should match format of the swap chain images
    colorAttachment.format = swapChainImageFormat;

    // not doing anything with multi sampling yet
    colorAttachment.samples = msaaSamples;

    // loadOp and storeOp determine what to do with the data in the attachment before rendering
    // and after rendering
    // CLEAR meeans to clear the values to a connstant at the start (for loadOp)
    // STORE for storeOp means rendered contents will be stored in memory and can be read later
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // loadOp/storeOp apply to color annd depth data
    // the following apply for stencil data
    // we don't do anything with the stencil buffer
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    // textures and framebuffers in Vulkan are represented by VkImage
    // the layout of the pixels in memory can change based on what you're trying to do with an image
    // LAYOUT_PRSENT_SRC_LHR means "images to be presented in the swap chain"
    //
    // "images need to be transitioned to specific layouts that are suitable for the opeartion that they're
    //  going to be involved in next"
    //
    // initialLayout specifies which layout the image will have before the render pass begins
    // finalLayout specifies the layout to automatically transition to when the render pass finishes
    // UNDEFINED means we don't care
    // as for final, we want the image to be ready for presentation using the swap chain after rendering
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    // we have a single attachment, described above
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = msaaSamples;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = swapChainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};

    // vulkan may support compute subpasses in the future
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // [!] the index of the attachment in this array is directly referenced from the fragment
    // shader with the
    // layout(location = 0) out vec4 outColor
    // line
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 3> attachments = {
        colorAttachment,
        depthAttachment,
        colorAttachmentResolve
    };

    // read here: https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachments.size();
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(
        device,
        &renderPassInfo,
        nullptr,
        &renderPass
    ) != VK_SUCCESS) {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  // a thin wrapper around the shader bytecode
  VkShaderModule createShaderModule(const std::vector<char> &code) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &shaderModule);
    if (res != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to create shader module!",
          static_cast<int>(res)
      ));
    }
    return shaderModule;
  }

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
      std::array<VkImageView, 3> attachments = {
          colorImageView,
          depthImageView,
          swapChainImageViews[i],
      };

      VkFramebufferCreateInfo framebufferInfo = {};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

      // first need to specify with which renderPass the framebuffer needs to be compatible
      // you can only use a framebuffer with the render passes that it is compatible with
      // which roughly means that they use the same number and type of attachments
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = attachments.size();
      framebufferInfo.pAttachments = attachments.data();
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
      }
    }
  }

  void createCommandPool() {
    // drawing operations and memory transfers are not executed directly using function calls
    // you have to record all of the operations you want to perform in command buffer objects
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    // Allow command buffers to be rerecorded individually, without this flag they all have to be reset together
    // we will be recording a command buffer every frame, so we want to be able to reset and rerecord over it.
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create command pool!");
    }
  }

  void createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    // TODO(cpp): what kind of cast is this? reasons to be strict? (TYPE) VAR
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    // can be submitted to a queue for execution, but cannot be called from other command buffers
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];

    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

//    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};

    // used to bind vertex buffers to bindings (in shader code(?))
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // difference between binding index and vertex buffer: you can only have a single index buffer.
    // its not possible to use different indices for each vertex attribute, so you have to completely duplicate
    // vertex data even if just one attributes varies
    //
    // two types that are possible: UINT16, UINT32
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // the old draw command that did not use index buffer
    //vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);

    // bind the right descriptor set for each frame to the descriptors in the shader
    //
    // unlike vertex and index buffers, descriptor sets are not unique to graphics pipelines
    // therefore we need to specify if we want to bind descriptor sets to the graphics or compute pipeline
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, 1,
        &descriptorSets[currentFrame],
        0, nullptr
    );

    vkCmdDrawIndexed(
        commandBuffer,
        (uint32_t) indices.size(),
        1, 0, 0, 0
    );

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to record command buffer!");
    }
  }

  void createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    // look at drawFrame: we want to fence to be created with signaled state s.t. on the first call to
    // drawFrame the wait the is performed on this fence at the start of the function works as expected
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // TODO(cpp): "canonical" way to iterate i 0 to 10?
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
          vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphores!");
      }
    }
  }

  void drawFrame() {
    // a core design philosophy in vulkan is that sync of execution on the GPU is explicit
    // the order of operations is up to us to define
    // many vulkan API calls start executing work on the GPU are async

    // we want to wait until the previous frame has finished, so that the command buffer and
    // semaphores are available to use
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // ****
    // this block of code, until the call to vkResetFences, used to be after it
    // this could cause a deadlock, see https://vulkan-tutorial.com/en/Drawing_a_triangle/Swap_chain_recreation

    // acquire an image from the swap chain
    // swap chain is an extension feature
    // imageIndex refers to position in swapChainImages
    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(
        device,
        swapChain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex
    );

    // VK_ERROR_OUT_OF_DATE_KHR is triggered on window resize, but it is not guranteed to happen

    // both VK_SUCCESS and VK_SUBOPTIMAL_KHR are considered "success" return codes
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
      recreateSwapchain();
      return;
    } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to acquire swap chain image!",
          static_cast<int>(res)
      ));
    }

    // TODO: what happens if this is performed after the reset fences?
    updateUniformBuffer(currentFrame);

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // now record the command buffer
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit draw command buffer!");
    }

    // last step of drawing a frame is submitting the result back to the swap chain to have it eventually show
    // up on the screen

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    res = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) {
      framebufferResized = false;
      recreateSwapchain();
    } else if (res != VK_SUCCESS) {
      throw std::runtime_error(fmt::format(
          "[err={}] failed to present swap chain image!",
          static_cast<int>(res)
      ));
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  void updateUniformBuffer(uint32_t currentImage) {
    // TODO(cpp): static local variable: when does this run?
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};

    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        time * glm::radians(90.0f),  // rotation angle
        glm::vec3(0.0f, 0.0f, 1.0f)  // rotation axis
    );

    ubo.view = glm::lookAt(
        glm::vec3(2.0f, 2.0f, 2.0f), // eye position
        glm::vec3(0.0f, 0.0f, 0.0f), // center (origin?) position
        glm::vec3(0.0f, 0.0f, 1.0f)  // up axis
    );

    // perspective projection with 45 degree vertical FOV
    // important to use the swap chain extent for asepct ratio calculation in order to take
    // into account the new width and height of the window after resizes
    ubo.proj = glm::perspective(
        glm::radians(45.0f), // vertical field of view
        (float) swapChainExtent.width / (float) swapChainExtent.height, // aspect ratio
        0.1f,
        10.0f
    );

    // GLM was designed for OpenGL where the Y coordinate of the clip coordinates is inverted
    // flip the sign on the scaling factor of the Y axis in the projection matrix
    // if you don't do this the rendered image will be upside down
    ubo.proj[1][1] *= -1;

    memcpy(uniformBuffersMappped[currentImage], &ubo, sizeof(ubo));
  }

  void recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    while (width == 0 || height == 0) {
      glfwGetFramebufferSize(window, &width, &height);
      glfwWaitEvents();
    }

    std::cout << fmt::format("recreating swap chain w={}, h={}\n", width, height);
    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    createSwapChain();

    // image views based directly on the swap chain imagesjA
    createImageViews();

    createColorResources();

    createDepthResources();

    // depend directly on the swap chain images
    createFramebuffers();

    // we don't recreate renderpass
    // it is possible for the swap chain image format to change during an applications lifetime
    // for example when moving a window from a standard range to a high dynamic range monitor
    // this would require the application to recreate the renderpass
  }

  void cleanupSwapchain() {
    vkDestroyImageView(device, colorImageView, nullptr);
    vkDestroyImage(device, colorImage, nullptr);
    vkFreeMemory(device, colorImageMemory, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    for (auto framebuffer: swapChainFramebuffers) {
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView: swapChainImageViews) {
      vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
  }

  void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,   // <---
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // <---
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // <---
        vertexBuffer,
        vertexBufferMemory
    );

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  // almost identical to createVertexBuffer
  // two notable differences:
  // bufferSize is now equal to number of indices * size of index type
  // indexBuffer should be USAGE_INDEX_BUFFER_BIT
  void createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory
    );

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t) bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indexBuffer,
        indexBufferMemory
    );

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
  }

  void createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }

  // TODO: base layout, then you have sets of such layouts, what
  void createDescriptorSets() {
    // specify descriptor layout to base these on
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    // will allocate descriptor sets, each with one uniform buffer descriptor
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      VkDescriptorBufferInfo bufferInfo{};
      bufferInfo.buffer = uniformBuffers[i];
      bufferInfo.offset = 0;
      bufferInfo.range = sizeof(UniformBufferObject);

      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = textureImageView;
      imageInfo.sampler = textureSampler;

      std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

      descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[0].dstSet = descriptorSets[i];
      descriptorWrites[0].dstBinding = 0;
      descriptorWrites[0].dstArrayElement = 0;
      descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      descriptorWrites[0].descriptorCount = 1;
      descriptorWrites[0].pBufferInfo = &bufferInfo;
      descriptorWrites[0].pImageInfo = nullptr;
      descriptorWrites[0].pTexelBufferView = nullptr;

      descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      descriptorWrites[1].dstSet = descriptorSets[i];
      descriptorWrites[1].dstBinding = 1;
      descriptorWrites[1].dstArrayElement = 0;
      descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      descriptorWrites[1].descriptorCount = 1;
      descriptorWrites[1].pImageInfo = &imageInfo; // used instead of pBufferInfo

      vkUpdateDescriptorSets(
          device,
          descriptorWrites.size(),
          descriptorWrites.data(),
          0,
          nullptr
      );
    }
  }

  void createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMappped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      createBuffer(
          bufferSize,
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          uniformBuffers[i],
          uniformBuffersMemory[i]
      );

      // we map the buffer right after creation to get pointer to which we can write teh data later on.
      // the buffer stays mapped to this pointer for the applications whole lifetime.
      // technique is called perssistent mapping and works on all Vulkan implementations
      // not having to map the buffer every time we need to update it increases performance as mapping is not free
      vkMapMemory(
          device,
          uniformBuffersMemory[i],
          0, bufferSize, 0,
          &uniformBuffersMappped[i]
      );
    }
  }

  void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(
        commandBuffer,
        src,
        dst,
        1,
        &copyRegion
    );

    endSingleTimeCommands(commandBuffer);
  }

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    // has two arrays memoryTypes and memoryHeaps
    // heaps are distinct memory resources like dedicated VRAM and swap space in RAM (for when VRAM runs out)
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type!");
  }

  void createBuffer(
      VkDeviceSize size,
      VkBufferUsageFlags usage,
      VkMemoryPropertyFlags properties,
      VkBuffer &buffer,
      VkDeviceMemory &bufferMemory
  ) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    // you're not supposed to actually call vkAllocateMemory for every individual buffer
    // the maximum number of simultaneous memory allocations is limited by the
    // maxMemoryAllocationCount physical device limit which may be as low as 4096 even
    // on high end hardware like GTX 1080.
    // the right way to allocate memory for large number of objects at the same time is to create
    // a custom allocator that spliuts up a single allocation among many different objects using
    // the offset parameters that we've seen in many functions
    //
    // you can use the VulkanMemoryAllocator library provided by the GPUOpen initiative
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
  }

  void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    // the binding used in the shader
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    // possible for shader variable to represent an array of uniform buiffer objects
    // this could be used to specify a transformation for each of the bones in a skeleton
    // for skeletal animation, for example
    uboLayoutBinding.descriptorCount = 1;

    // need to spciufyt in which shader stages the descriptor is going to be references
    // the field can be a combination of VkShaderStageFlagBits
    // or the value VK_SHADER_STAGE_ALL_GRAPHICS
    // in our case we'll be refercing the decriptor from the vertex shader
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;


    // ********************************************************************************
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // ********************************************************************************

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
        uboLayoutBinding,
        samplerLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(
        device,
        &layoutInfo,
        nullptr,
        &descriptorSetLayout
    ) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor set layout!");
    }
  }

  void createImage(
      uint32_t width,
      uint32_t height,
      uint32_t mipLevels,
      VkSampleCountFlagBits numSamples,
      VkFormat format,
      VkImageTiling tiling,
      VkImageUsageFlags usage,
      VkMemoryPropertyFlags properties,
      VkImage &out_image,
      VkDeviceMemory &out_imageMemory
  ) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    // 1D used to store an array of data or gradient
    // 2D store textures
    // 3D store voxel volumes
    imageInfo.imageType = VK_IMAGE_TYPE_2D;  // possible: 1D, 2D, 3D
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;

    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = numSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &out_image) != VK_SUCCESS) {
      throw std::runtime_error("failed to create out_image!");
    }

    // allocating memory for an out_image works in exactly the same way as allocation memoryh for a buffer
    // vkGetImageMemoryRequirements instead of vkGetBufferMemoryRequirements
    // vkBindImageMemory instead of vkBindBufferMemory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, out_image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &out_imageMemory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate out_image memory!");
    }

    vkBindImageMemory(device, out_image, out_imageMemory, 0);
  }

  void createTextureImage() {
    int texWidth, texHeight, texChannels;

    // uc == unsigned char
    // TODO(cpp): char == byte ALWAYS? why isn't there a "byte" type?
    // answer: since C++17 there exists a std::byte
    // like unsigned char it can be used to access raw memory occupied by other objects but unlike
    // unsigned char it is not a character type and is not an arithmetic type
    // std::byte models a mere collection of bits, supporting only bitwise and comparison operations
    // https://en.cppreference.com/w/cpp/types/byte
    stbi_uc *pixels = stbi_load(
        TEXTURE_PATH.c_str(),
        &texWidth,
        &texHeight,
        &texChannels,
        STBI_rgb_alpha
    );
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
      throw std::runtime_error(fmt::format("failed to load texture image: {}", stbi_failure_reason()));
    }

    // max selects largest dimension
    // log2 to see how many times that dimension can be divided by 2
    // and then floor
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    // create buffer in host visible memory so we can use vkMapMemory and copy
    // pixels to it
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    void *data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    stbi_image_free(pixels);

    // ********************************************************************************

    createImage(
        texWidth,
        texHeight,
        mipLevels,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,

        // vkCmdBlitImage (for mipmapping) is considered a transfer operation
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,

        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        textureImage,
        textureImageMemory
    );

    // image was create with VK_IMAGE_LAYOUT_UNDEFINED
    transitionImageLayout(
        textureImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels
    );

    // fyi the order of calling copy and then another transition is CORRECT
    // tried to reverse it and it explicitly told me the expected layout was incorrect
    copyBufferToImage(
        stagingBuffer,
        textureImage,
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight)
    );

    // to be able to start sampling from the texture image in the shader we need one last transition to
    // prepare it for shader access:
    ///
//    transitionImageLayout(
//        textureImage,
//        VK_FORMAT_R8G8B8A8_SRGB,
//        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//        mipLevels
//    );

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    generateMipmaps(
        textureImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        texWidth,
        texHeight,
        mipLevels
    );
  }

  VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
  }

  void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
  }

  void transitionImageLayout(
      VkImage image,
      VkFormat format,
      VkImageLayout oldLayout,
      VkImageLayout newLayout,
      uint32_t mipLevels
  ) {
    // TODO: this is a scoped object:
    // {
    //   VkCommandBuffer bla = beingSingleTimeCommands();
    //   ...
    //   endSingleTimeCommands(bla);
    // }
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // barriers used to synchronize access to resources like ensuireing that a write
    // to a buffer completes before reading from it
    // also can be used to transition image layouts
    // and transfer queue family ownership
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    // VK_IMAGE_LAYOUT_UNDEFINED if we don't care about existing contents of the image
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;

    // used when transfering queue family ownership
    // note: this value is not the default one, and must be set if we wish to ignore the
    // queue family thing
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = image;

    // the condition came when adding support for depth image
    // the else branch was the default one before this
    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      if (hasStencilComponent(format)) {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      }
    } else {
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // barrier.srcAccessMask
    // barrier.dstAccessMask
    //
    // must specify t ypes of operations that involve the resource must happen before the barrier
    // and which operations that involve the resource must wait on the barrier
    // we do that despite already using vkQueueWaitIdle to manually synchronize (!)


    // see https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#synchronization-access-types-supported
    // for list of valujes
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
      throw std::invalid_argument("unsupported layout transition!");
    }

    // all pipeline barriers are submitted using the same function
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
  }

  void copyBufferToImage(
      VkBuffer buffer,
      VkImage image,
      uint32_t width,
      uint32_t height
  ) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    endSingleTimeCommands(commandBuffer);
  }

  VkImageView createImageView(
      VkImage image,
      VkFormat format,
      VkImageAspectFlags aspectFlags,
      uint32_t mipLevels
  ) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;

    // viewType and format specify how the image data should be interpreted
    // viewType allows to treat images as 1D textures, 2D texture, 3D textures and cube maps
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    // you can map all of the channels to the red channel for a monochrome texture
    // you can also map constant value of 0 or 1 to a channel
    // here we're sticking to the default mapping
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // describes what the image purpose is and which part of the image should be accessed
    // our image is used as color target
    // no mpmapping levels or multiple layers
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture image view!");
    }

    return imageView;
  }

  // basically the same as create image view, which was the reason for createImageView
  void createTextureImageView() {
    textureImageView = createImageView(
        textureImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_ASPECT_COLOR_BIT,
        mipLevels
    );
  }

  // texel == TEXture ELement / texture pixel
  // it is possible for shaders to read texels directly from images, but they are usually
  // accessed through samplers, which will apply filtering & transformations to compute the final color
  void createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    // specify how to interpolate texels that are magnified or minified
    // manification for oversampling,
    // minification for undersampling
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    // uvw = xyz (convention for TEXTURE space coordinates)
    // REPEAT == repeat thetexture when going beyond the image dimensions
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
    // TODO we check this at "isDeviceSuitable" on the physical device and then also implicitly
    // assume that it is supported when creating the logical device
    // and here we use it for the sampler
    // so the code is sloppy af
    if (supportedFeatures.samplerAnisotropy) {
      // no reaason not to use this unless performance is a concern
      samplerInfo.anisotropyEnable = VK_TRUE;

      // samplerInfo.maxAnisotropy
      // limits amount of texel samples that  can be used to calculate the final color
      // lower  value = better performance & worse quality
      // to figure out which value we can use:
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(physicalDevice, &properties);
      samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    } else {
      samplerInfo.anisotropyEnable = VK_FALSE;
      samplerInfo.maxAnisotropy = 1.0f;
    }

    // what color used when sampling beyond the image
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    // if true, can use [0, texWidth] [0, texHeight]
    // if false [0, 1)
    // real world uses normalized coordinates because then its possible to use textures
    // of varying resolutions with the same coordinates
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // if comparison is enabled, texels are first compared to a value and the results is then used in
    // filtering operations
    // used for percentage-closer filtering on shadow maps
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    // mipmapping is another type of filter
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
//    samplerInfo.minLod = (float) mipLevels / 2;  // simulate what it will look like from far away
    samplerInfo.maxLod = (float) mipLevels;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture sampler!");
    }
  }

  // for depth image
  VkFormat findSupportedFormat(
      const std::vector<VkFormat> &candidates,
      VkImageTiling tiling,
      VkFormatFeatureFlags features
  ) {
    for (VkFormat format: candidates) {
      VkFormatProperties props;
      vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

      if (tiling == VK_IMAGE_TILING_LINEAR &&
          (props.linearTilingFeatures & features) == features) {
        return format;
      } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                 (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error("failed to find supported format!");
  }

  VkFormat findDepthFormat() {
    return findSupportedFormat(
        {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
  }

  bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT;
  }

  void createDepthResources() {
    VkFormat depthFormat = findDepthFormat();

    createImage(
        swapChainExtent.width,
        swapChainExtent.height,
        1,
        msaaSamples,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = createImageView(
        depthImage,
        depthFormat,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        1
    );

    transitionImageLayout(
        depthImage,
        depthFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        1
    );

  }

  void loadModel() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // object file consists of positions, normals, texture coords, and faces
    // faces consist of arbitrary amount of vertices, where each vertex refers to a position
    // normal and/or texture coordinate by index
    //
    // attrib holds all of the positions normals and texture coords in
    // attrib.vertices
    // attrib.texcoords
    // attrib.normals
    //
    // shapes contains all separate objects and their faces
    // each face is an array of vertices
    // each vertex contaions indices of the position, normal, and texture coord attributes
    // (obj can also define a material and texture per face but we ignore this)
    bool ok = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warn,
        &err,
        MODEL_PATH.c_str()
    );

    if (!ok) {
      throw std::runtime_error(warn + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    // combine alll faces into a single model
    for (const auto &shape: shapes) {
      for (const auto &index: shape.mesh.indices) {
        Vertex vertex{};

        // attrib.vertices is an array of float values instead of something like vec3, thus
        // we need to multiply by 3
        vertex.pos = {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2]
        };

        vertex.texCoord = {
            attrib.texcoords[2 * index.texcoord_index + 0],

            // obj format assumes vertical coord of 0 means bottom of the image
            // while we use top to bottom orientation where 0 means top of image
            // so we need to flip it
            1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
        };

        vertex.color = {1.0f, 1.0f, 1.0f};

        // reduces indices from 1,500,00 to 265,645 which saves a lot of GPU memory
        if (uniqueVertices.count(vertex) == 0) {
          uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
          vertices.push_back(vertex);
        }

        vertices.push_back(vertex);
        indices.push_back(uniqueVertices[vertex]);  // assume every vertex is unique
      }
    }
  }

  void generateMipmaps(
      VkImage image,
      VkFormat imageFormat,
      int32_t texWidth,
      int32_t texHeight,
      uint32_t mipLevels
  ) {

    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      // there are two alternatives in this case
      // 1. implement a function that searches common texture image formats for one that does support
      //    linear blitting
      // 2. implement mipmap generation in software with a library like stb_image_resize
      //
      // usually mipmap levels aren't generated @ runtime anyway
      // they are pregenerated and stored in the texture file alongside the base level to improve loading speed
      //
      // possible exercise: resize in software & load multiple levels from a file
      throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    // we'll make several transitions, and we'll re-use this barrier
    // subresourceRange.mipLevel
    // oldLayout,
    // newLayout,
    // srcAccessMask,
    // dstAccessMask
    // will change foir each transition
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; ++i) {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      vkCmdPipelineBarrier(
          commandBuffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier
      );

      VkImageBlit blit{};
      blit.srcOffsets[0] = { 0, 0, 0 };
      blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = 0;
      blit.srcSubresource.layerCount = 1;
      blit.dstOffsets[0] = { 0, 0, 0 };
      blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 1;

      // convenient function to generate all mip levels, but it is not guranteed to
      // be supported on all platforms
      // it requires texture image format we use to support linear filtering
      // which can be checked with vkGetPhysicalDeviceFormatProperties
      vkCmdBlitImage(
          commandBuffer,
          image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &blit,
          VK_FILTER_LINEAR
      );

      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                           0, nullptr,
                           0, nullptr,
                           1, &barrier);

      if (mipWidth > 1) mipWidth /= 2;
      if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // transitions last mip level from VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL to
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);

    endSingleTimeCommands(commandBuffer);
  }

  VkSampleCountFlagBits getMaxUsableSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts =
        physicalDeviceProperties.limits.framebufferColorSampleCounts &
        physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
  }

  void createColorResources() {
    VkFormat colorFormat = swapChainImageFormat;

    createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
    colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
  }

private: // members
  GLFWwindow *window;
  VkInstance instance;

  // even the debug callback in vulkan ins managed with a handle that is created/destroyed.
  // you can have as many of these as you want.
  VkDebugUtilsMessengerEXT debugMessenger;

  // graphics card
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

  // logical device
  // interfaces with the physical device
  VkDevice device;

  VkQueue graphicsQueue;

  // to establish connection between vulkan and the window system to present results to the screen we need to use
  // the WSI (window system integration) extensions
  // VK_KHR_surface is one
  // VkSurfaceKHR is an object that represents an abstract type of surface to prseent rendered images to
  // the surface will be backed by the window that we've opened with GLFW
  // instance level extension
  // returned by the glfwGetRequiredInstanceExtensions
  // the surface needs to be created right after the instance creation because it can influence the physical device selection
  VkSurfaceKHR surface;

  VkQueue presentQueue;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;

  // image view is sufficient to start using an image as a texture, but it's not quite ready
  // to be used as a render target
  std::vector<VkImageView> swapChainImageViews;

  VkRenderPass renderPass;
  VkDescriptorSetLayout descriptorSetLayout;
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexBufferMemory;

  VkBuffer indexBuffer;
  VkDeviceMemory indexBufferMemory;

  VkDescriptorPool descriptorPool;
  std::vector<VkDescriptorSet> descriptorSets;

  // we're going to copyh new data to the uniform buffer every frame, so it doesn't make sense
  // to have a staging buffer: it would add extra overhead in this case and likely degrade performance
  //
  // we should have multiple buffers, because multiple frames may be in flight at the same time and we dont
  // want to update the buffer in preparation of the next frame while a previous one is still reading from it
  std::vector<VkBuffer> uniformBuffers;
  std::vector<VkDeviceMemory> uniformBuffersMemory;
  std::vector<void *> uniformBuffersMappped;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  uint32_t currentFrame = 0;
  bool framebufferResized = false;

  // shader can use raw pixel buffer, but it's better to use image objects
  // they make it easier and faster to retrieve colors by using 2D coordinated for example
  // (terminology) pixels within an image object == TEXTURE
  //
  // mip level 0 == original image
  // higher the level, less detail / smaller the image
  // (also, somehow helps avoid artfiacts such as Moire patterns (?))
  uint32_t mipLevels;
  VkImage textureImage;
  VkDeviceMemory textureImageMemory;

  VkImageView textureImageView;
  VkSampler textureSampler;

  // depth attachment
  // dpeth image requires the trifecta:L image, memory and image view
  VkImage depthImage;
  VkDeviceMemory depthImageMemory;
  VkImageView depthImageView;

  // msaa
  // image will store the desired number of samples per pixel
  VkImage colorImage;
  VkDeviceMemory colorImageMemory;
  VkImageView colorImageView;
};

int main() {
  HelloTriangleApplication app;


  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
