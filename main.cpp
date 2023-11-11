#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <optional>
#include <set>
#include <limits>

#include <fmt/core.h>


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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // disable window resizing

    // last parameter relevant only for OpenGL
    // monitor param controls which monitor the window will be created on
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
  }

  void initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    for (auto imageView: swapChainImageViews) {
      vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);

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
    if (!enableValidationLayers) return;

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
