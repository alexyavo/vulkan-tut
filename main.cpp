#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <optional>
#include <set>

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
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
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
          swapChainAdequate
          )
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
