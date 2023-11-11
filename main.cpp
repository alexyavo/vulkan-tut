#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <optional>

#include <fmt/core.h>


// in vulkan anything from drawing to uploading textures, requires COMMANDS to be submitted to a QUEUE
// there are different types of QUEUEs that originate from different QUEUE FAMILIES
// each family allows only a subset of commands
// e.g. a family that allows processing of compute commands only
//      or one that allows memory transfer related commands
struct QueueFamilyIndices {
  // any value of uint32_t could in theory be a valid queue family index including 0
  std::optional<uint32_t> graphicsFamily;
};

// need to check which queue families are supported by the device
// and which of these families supports the commands that we want to use
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

  int i = 0;
  for (const auto& queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
      break;
    }

    ++i;
  }

  return indices;
}


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
    pickPhysicalDevice();
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    if (enableValidationLayers) {
      DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

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

      if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
          deviceFeatures.geometryShader &&
          indices.graphicsFamily.has_value())
      {
        physicalDevice = device;
        break;
      }
    }
  }

private:
  GLFWwindow *window;
  VkInstance instance;

  // even the debug callback in vulkan ins managed with a handle that is created/destroyed.
  // you can have as many of these as you want.
  VkDebugUtilsMessengerEXT debugMessenger;

  // graphics card
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
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
