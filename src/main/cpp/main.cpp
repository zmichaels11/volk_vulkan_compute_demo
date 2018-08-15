#include "volk.h"

#include <cstdint>

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

struct context {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memoryProperties;

    context();

    ~context();

    std::uint32_t getMemoryTypeIndex(std::uint32_t typeBits, unsigned int requirementsMask);

    VkDeviceMemory bindMemory(VkBuffer buffer);
};

std::string translateVulkanResult(VkResult result);

void vkAssert(VkResult result);

void bindMemory(context& ctx, VkBuffer buffer);

int main(int argc, char** argv) {
    context ctx;

    auto inputData = std::vector<float>();
    for (int i = 0; i < 32; i++) {
        inputData.push_back(static_cast<float> (2 << i));
    }

    VkBufferCreateInfo bufferCI {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferCI.size = inputData.size() * sizeof(float);

    VkBuffer inputBuffer = VK_NULL_HANDLE;
    vkAssert(vkCreateBuffer(ctx.device, &bufferCI, nullptr, &inputBuffer));
    auto inputMemory = ctx.bindMemory(inputBuffer);

    float *pData = nullptr;
    vkAssert(vkMapMemory(ctx.device, inputMemory, 0, inputData.size() * sizeof(float), 0, reinterpret_cast<void **> (&pData)));

    std::copy(inputData.begin(), inputData.end(), pData);

    vkUnmapMemory(ctx.device, inputMemory);

    VkBuffer outputBuffer = VK_NULL_HANDLE;
    vkAssert(vkCreateBuffer(ctx.device, &bufferCI, nullptr, &outputBuffer));
    auto outputMemory = ctx.bindMemory(outputBuffer);

    vkFreeMemory(ctx.device, outputMemory, nullptr);
    vkDestroyBuffer(ctx.device, outputBuffer, nullptr);
    vkFreeMemory(ctx.device, inputMemory, nullptr);
    vkDestroyBuffer(ctx.device, inputBuffer, nullptr);

    return 0;
}

context::context() {
    if (VK_SUCCESS != volkInitialize()) {
        throw std::runtime_error("Volk could not be initialized!");
    }

    auto instanceLayers = std::vector<const char *> ();
    auto instanceExtensions = std::vector<const char *> ();
    auto deviceExtensions = std::vector<const char *> ();

    instanceLayers.push_back("VK_LAYER_LUNARG_standard_validation");
    instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    
    VkApplicationInfo appCI {};
    appCI.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appCI.apiVersion = VK_MAKE_VERSION(1, 0, 2);
    appCI.applicationVersion = 1;
    appCI.pApplicationName = "Vulkan Compute Test";
    appCI.pEngineName = "Vulkan Compute Test";
    appCI.engineVersion = 1;

    VkInstanceCreateInfo instanceCI {};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appCI;
    instanceCI.enabledLayerCount = instanceLayers.size();
    instanceCI.ppEnabledLayerNames = instanceLayers.data();
    instanceCI.enabledExtensionCount = instanceExtensions.size();
    instanceCI.ppEnabledExtensionNames = instanceExtensions.data();

    vkAssert(vkCreateInstance(&instanceCI, nullptr, &instance));
    volkLoadInstance(instance);

    std::uint32_t nGPUs = 0;
    vkAssert(vkEnumeratePhysicalDevices(instance, &nGPUs, nullptr));
    auto pGPUs = std::make_unique<VkPhysicalDevice[]>(nGPUs);
    vkAssert(vkEnumeratePhysicalDevices(instance, &nGPUs, pGPUs.get()));

    std::uint32_t selectedGPU = 0;
    for (std::uint32_t i = 0; i < nGPUs; i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(pGPUs[i], &properties);

        if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == properties.deviceType) {
            selectedGPU = i;
        }
    }

    physicalDevice = pGPUs[selectedGPU];

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    std::uint32_t nQueueFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &nQueueFamilies, nullptr);
    auto familyProperties = std::make_unique<VkQueueFamilyProperties[]> (nQueueFamilies);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &nQueueFamilies, familyProperties.get());

    auto computeQueueFamilyIds = std::vector<std::uint32_t>();
    for (std::uint32_t i = 0; i < nQueueFamilies; i++) {
        if (familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeQueueFamilyIds.push_back(i);
            break;
        }
    }

    if (computeQueueFamilyIds.empty()) {
        throw std::runtime_error("GPU does not support any Compute Queues!");
    }

    // using the first Compute Family Id; can modify to select multiple...
    auto queueCIs = std::vector<VkDeviceQueueCreateInfo>();
    {
        auto queuePriorities = std::vector<float> ();
        queuePriorities.push_back(1.0F);

        VkDeviceQueueCreateInfo queueCI {};
        queueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCI.queueCount = queuePriorities.size();
        queueCI.pQueuePriorities = queuePriorities.data();

        queueCIs.push_back(queueCI);
    }

    VkDeviceCreateInfo deviceCI {};
    deviceCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCI.queueCreateInfoCount = queueCIs.size();
    deviceCI.pQueueCreateInfos = queueCIs.data();
    deviceCI.enabledExtensionCount = deviceExtensions.size();
    deviceCI.ppEnabledExtensionNames = deviceExtensions.data();

    vkAssert(vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device));
    volkLoadDevice(device);
}

context::~context() {
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}

VkDeviceMemory context::bindMemory(VkBuffer buffer) {
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory memory;
    vkAssert(vkAllocateMemory(device, &allocInfo, nullptr, &memory));

    vkAssert(vkBindBufferMemory(device, buffer, memory, 0));

    return memory;
}

std::uint32_t context::getMemoryTypeIndex(std::uint32_t typeBits, unsigned int requirementsMask) {
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if (0 != (typeBits & (1 << i))) {
            if (memoryProperties.memoryTypes[i].propertyFlags & requirementsMask) {
                return i;
            }
        }
    }

    throw std::runtime_error("No MemoryType exists with the requested features!");
}

std::string translateVulkanResult(VkResult result) {
    switch (result) {
        // Success codes
        case VK_SUCCESS:
            return "Command successfully completed.";
        case VK_NOT_READY:
            return "A fence or query has not yet completed.";
        case VK_TIMEOUT:
            return "A wait operation has not completed in the specified time.";
        case VK_EVENT_SET:
            return "An event is signaled.";
        case VK_EVENT_RESET:
            return "An event is unsignaled.";
        case VK_INCOMPLETE:
            return "A return array was too small for the result.";
        case VK_SUBOPTIMAL_KHR:
            return "A swapchain no longer matches the surface properties exactly, but can still be used to present to the surface successfully.";

        // Error codes
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "A host memory allocation has failed.";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "A device memory allocation has failed.";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "Initialization of an object could not be completed for implementation-specific reasons.";
        case VK_ERROR_DEVICE_LOST:
            return "The logical or physical device has been lost.";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "Mapping of a memory object has failed.";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "A requested layer is not present or could not be loaded.";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "A requested extension is not supported.";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "A requested feature is not supported.";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "The requested version of Vulkan is not supported by the driver or is otherwise incompatible for implementation-specific reasons.";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "Too many objects of the type have already been created.";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "A requested format is not supported on this device.";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "A surface is no longer available.";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "The requested window is already connected to a VkSurfaceKHR, or to some other non-Vulkan API.";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "A surface has changed in such a way that it is no longer compatible with the swapchain, and further presentation requests using the "
                    "swapchain will fail. Applications must query the new surface properties and recreate their swapchain if they wish to continue"
                    "presenting to the surface.";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "The display used by a swapchain does not use the same presentable image layout, or is incompatible in a way that prevents sharing an"
            " image.";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "A validation layer found an error.";
        default: {
            auto msg = std::stringstream();

            msg << "Unknown VkResult: 0x" << std::hex << result;

            return msg.str();
        }
    }
}

void vkAssert(VkResult result) {
    if (VK_SUCCESS != result) {
        throw std::runtime_error(translateVulkanResult(result));
    }
}
