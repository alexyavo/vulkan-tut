#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>

#include <fmt/core.h>

class HelloTriangleApplication {
public:
  const uint32_t WIDTH = 800;
  const uint32_t HEIGHT = 600;

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
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
  }

  void cleanup() {
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
  }

private:
  void createInstance() {
    VkApplicationInfo appInfo = {};

    // many structs in VUlkan require you to explicitly specify the type in the sType member.
    // This is also one of the many structs with a pNext member that can point to extension information in the future.
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // tells the Vulkan driver which *global extensions* and *validation layers* we want to use
    // global == they apply to the entire program and not a specific device
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // retrieve a list of supported extensions before creating an instance
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    std::cout << "available extensions:\n";
    for (const auto &extension : extensions) {
      std::cout << "\t" << extension.extensionName << "\n";
    }

    // Vulkan is platform-agnostic, which means that you need an extension to interface with the window system
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    uint32_t supportedExtensionCount = 0;
    std::vector<const char*> unsupportedExtensions;
    std::cout << "required extensions:\n";
    for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
      std::cout << "\t" << glfwExtensions[i] << "\n";
      for (const auto &extension : extensions) {
        if (strcmp(glfwExtensions[i], extension.extensionName) == 0) {
          ++supportedExtensionCount;
        } else {
          unsupportedExtensions.push_back(glfwExtensions[i]);
        }
      }
    }

    if (supportedExtensionCount != glfwExtensionCount) {
      std::cout << "missing support for following required extensions:\n";
      for (const auto &extension : unsupportedExtensions) {
        std::cout << "\t" << extension << "\n";
      }

      throw std::runtime_error("not all required extensions are supported!");
    } else {
      std::cout << "all required extensions are supported ...\n";
    }

    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    // determine the global validation layers to enable
    createInfo.enabledLayerCount = 0;

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

private:
  GLFWwindow* window;
  VkInstance instance;
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
