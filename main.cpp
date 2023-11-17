#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <optional>
#include <set>
#include <limits>
#include <fstream>

#include <fmt/core.h>


std::vector<char> readf(const std::string& filename) {
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  // disable window resizing

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
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
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

  static void framebufferResizeCallback(GLFWwindow * window, int width, int height) {
    auto app = reinterpret_cast<HelloTriangleApplication *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageType,
      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
      void *pUserData) {
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
          indices.isComplete() &&
          checkDeviceExtensionSupport(device) &&
          swapChainAdequate)
      {
        physicalDevice = device;
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

    for (uint32_t queueFamily : uniqueQueueFamilies) {
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
    for (const auto & requiredExtension : deviceExtensions) {
      for (const auto & availableExtension : availableVkExtensions) {
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

  VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
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


  VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> & availablePresentModes) {
    for (const auto & presentMode: availablePresentModes) {
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
  VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR & capabilities) {
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

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

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
      VkImageViewCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = swapChainImages[i];

      // viewType and format specify how the image data should be interpreted
      // viewType allows to treat images as 1D textures, 2D texture, 3D textures and cube maps
      createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      createInfo.format = swapChainImageFormat;

      // you can map all of the channels to the red channel for a monochrome texture
      // you can also map constant value of 0 or 1 to a channel
      // here we're sticking to the default mapping
      createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

      // describes what the image purpose is and which part of the image should be accessed
      // our image is used as color target
      // no mpmapping levels or multiple layers
      createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = 1;

      VkResult res = vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]);
      if (res != VK_SUCCESS) {
        throw std::runtime_error(fmt::format(
            "[err={}] failed to create image views!",
            static_cast<int>(res)
        ));
      }
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

    // describes format of the vertex data that will be passed to the vertex shader
    // we're hard coding vertex data directly to vertex shader, we fill this to specify
    // that there is no vertex data to load for now
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

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
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

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
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
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
    pipelineLayoutInfo.setLayoutCount = 0; // Optional
    pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    // ********************************************************************************
    //
    // ********************************************************************************

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // Optional
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
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

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
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    // we have a single attachment, described above
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};

    // vulkan may support compute subpasses in the future
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // [!] the index of the attachment in this array is directly referenced from the fragment
    // shader with the
    // layout(location = 0) out vec4 outColor
    // line
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // read here: https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Rendering_and_presentation
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
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
  VkShaderModule createShaderModule(const std::vector<char> & code) {
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
      VkImageView attachments[] = {
          swapChainImageViews[i]
      };

      VkFramebufferCreateInfo framebufferInfo = {};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

      // first need to specify with which renderPass the framebuffer needs to be compatible
      // you can only use a framebuffer with the render passes that it is compatible with
      // which roughly means that they use the same number and type of attachments
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = 1;
      framebufferInfo.pAttachments = attachments;
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

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
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

    // depend directly on the swap chain images
    createFramebuffers();

    // we don't recreate renderpass
    // it is possible for the swap chain image format to change during an applications lifetime
    // for example when moving a window from a standard range to a high dynamic range monitor
    // this would require the application to recreate the renderpass
  }

  void cleanupSwapchain() {
    for (auto framebuffer : swapChainFramebuffers) {
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView: swapChainImageViews) {
      vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
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
  VkPipelineLayout pipelineLayout;
  VkPipeline graphicsPipeline;

  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;

  uint32_t currentFrame = 0;
  bool framebufferResized = false;
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
