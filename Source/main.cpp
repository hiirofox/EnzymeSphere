#include "loss.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
}

std::string executableDirectory()
{
    std::vector<char> path(32768, '\0');
    const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        throw std::runtime_error("Could not determine executable directory.");
    const std::string fullPath(path.data(), length);
    return fullPath.substr(0, fullPath.find_last_of("\\/"));
}

struct VulkanRun
{
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer inputBuffer = VK_NULL_HANDLE;
    VkBuffer outputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory inputMemory = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    ~VulkanRun()
    {
        if (device != VK_NULL_HANDLE)
        {
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, nullptr);
            if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            if (inputBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, inputBuffer, nullptr);
            if (outputBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, outputBuffer, nullptr);
            if (inputMemory != VK_NULL_HANDLE) vkFreeMemory(device, inputMemory, nullptr);
            if (outputMemory != VK_NULL_HANDLE) vkFreeMemory(device, outputMemory, nullptr);
            vkDestroyDevice(device, nullptr);
        }
    }
};

uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((mask & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    throw std::runtime_error("No host-visible coherent Vulkan memory type was found.");
}

void createStorageBuffer(VulkanRun& run, VkPhysicalDevice physical, VkDeviceSize size,
                         VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(run.device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(run.device, buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(run.device, &allocation, nullptr, &memory), "vkAllocateMemory");
    check(vkBindBufferMemory(run.device, buffer, memory, 0), "vkBindBufferMemory");
}

std::array<float, 6> runOnDevice(VkPhysicalDevice physical, const std::vector<uint32_t>& spirv,
                                const std::array<float, 3>& inputs)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "\nVulkan device: " << properties.deviceName << "\n";

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    uint32_t computeFamily = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i)
    {
        if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
        {
            computeFamily = i;
            break;
        }
    }
    if (computeFamily == UINT32_MAX)
        throw std::runtime_error("The Vulkan device has no compute queue.");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = computeFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VulkanRun run;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &run.device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(run.device, computeFamily, 0, &queue);

    createStorageBuffer(run, physical, sizeof(inputs), run.inputBuffer, run.inputMemory);
    std::array<float, 6> gradients{};
    createStorageBuffer(run, physical, sizeof(gradients), run.outputBuffer, run.outputMemory);
    void* mapped = nullptr;
    check(vkMapMemory(run.device, run.inputMemory, 0, sizeof(inputs), 0, &mapped), "vkMapMemory(inputs)");
    std::memcpy(mapped, inputs.data(), sizeof(inputs));
    vkUnmapMemory(run.device, run.inputMemory);

    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = 2;
    descriptorInfo.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(run.device, &descriptorInfo, nullptr, &run.descriptorLayout),
          "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &run.descriptorLayout;
    check(vkCreatePipelineLayout(run.device, &pipelineLayoutInfo, nullptr, &run.pipelineLayout),
          "vkCreatePipelineLayout");

    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderInfo.pCode = spirv.data();
    check(vkCreateShaderModule(run.device, &shaderInfo, nullptr, &run.shader), "vkCreateShaderModule");

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = run.shader;
    stageInfo.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = run.pipelineLayout;
    check(vkCreateComputePipelines(run.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &run.pipeline),
          "vkCreateComputePipelines");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    check(vkCreateDescriptorPool(run.device, &poolInfo, nullptr, &run.descriptorPool),
          "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = run.descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &run.descriptorLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(run.device, &setInfo, &descriptorSet), "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo bufferInfo[2]{{run.inputBuffer, 0, sizeof(inputs)},
                                         {run.outputBuffer, 0, sizeof(gradients)}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfo[i];
    }
    vkUpdateDescriptorSets(run.device, 2, writes, 0, nullptr);

    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = computeFamily;
    check(vkCreateCommandPool(run.device, &commandPoolInfo, nullptr, &run.commandPool),
          "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = run.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(run.device, &commandInfo, &commandBuffer), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, run.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, run.pipelineLayout,
                            0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
    check(vkQueueWaitIdle(queue), "vkQueueWaitIdle");

    check(vkMapMemory(run.device, run.outputMemory, 0, sizeof(gradients), 0, &mapped), "vkMapMemory(gradients)");
    std::memcpy(gradients.data(), mapped, sizeof(gradients));
    vkUnmapMemory(run.device, run.outputMemory);
    return gradients;
}
}

int main()
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    constexpr int numTasks = 2;
    constexpr int numParams = 3;
    constexpr std::array<float, 3> inputs{2.0f, 3.0f, 5.0f};
    try
    {
        const std::string shaderPath = executableDirectory() + "\\loss_gradient_vulkan.spv";
        std::ifstream shaderFile(shaderPath, std::ios::binary | std::ios::ate);
        if (!shaderFile)
            throw std::runtime_error("Cannot open Vulkan SPIR-V module: " + shaderPath);
        const std::streamsize byteCount = shaderFile.tellg();
        if (byteCount <= 0 || byteCount % 4 != 0)
            throw std::runtime_error("Vulkan SPIR-V module has an invalid size.");
        std::vector<uint32_t> spirv(static_cast<std::size_t>(byteCount) / 4);
        shaderFile.seekg(0);
        shaderFile.read(reinterpret_cast<char*>(spirv.data()), byteCount);
        if (!shaderFile || spirv.front() != 0x07230203u)
            throw std::runtime_error("Vulkan SPIR-V module is invalid or truncated.");

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "EnzymeSphere Vulkan gradient test";
        appInfo.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &appInfo;
        VkInstance instance = VK_NULL_HANDLE;
        check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

        uint32_t deviceCount = 0;
        check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (deviceCount == 0)
        {
            vkDestroyInstance(instance, nullptr);
            throw std::runtime_error("No Vulkan physical devices were found.");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        check(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

        bool allMatched = true;
        uint32_t tested = 0;
        std::set<std::array<uint8_t, VK_UUID_SIZE>> testedDeviceUuids;
        for (VkPhysicalDevice physical : devices)
        {
            VkPhysicalDeviceIDProperties idProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties2.pNext = &idProperties;
            vkGetPhysicalDeviceProperties2(physical, &properties2);
            const VkPhysicalDeviceProperties& properties = properties2.properties;
            if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                continue;
            std::array<uint8_t, VK_UUID_SIZE> uuid{};
            std::copy_n(idProperties.deviceUUID, uuid.size(), uuid.begin());
            if (!testedDeviceUuids.insert(uuid).second)
                continue;
            try
            {
                const auto gradients = runOnDevice(physical, spirv, inputs);
                std::array<float, 3> expectedSum{};
                std::array<float, 3> scratch{};
                AllTaskGradCalm(const_cast<float*>(inputs.data()), expectedSum.data(),
                                numTasks, numParams, scratch.data());
                bool matched = true;
                for (int task = 0; task < numTasks; ++task)
                {
                    std::array<float, 3> expectedTask{};
                    TaskGradCalm(task, numTasks, const_cast<float*>(inputs.data()),
                                 expectedTask.data(), numParams);
                    for (int parameter = 0; parameter < numParams; ++parameter)
                    {
                        const std::size_t index = static_cast<std::size_t>(task * numParams + parameter);
                        const float error = std::abs(gradients[index] - expectedTask[parameter]);
                        matched = matched && error < 1.0e-4f;
                        std::cout << "  task=" << task << " param=" << parameter
                                  << " Vulkan gradient=" << gradients[index]
                                  << " host Enzyme gradient=" << expectedTask[parameter] << '\n';
                    }
                }
                std::cout << "  summed gradients:";
                for (int parameter = 0; parameter < numParams; ++parameter)
                {
                    float gpuSum = 0.0f;
                    for (int task = 0; task < numTasks; ++task)
                        gpuSum += gradients[static_cast<std::size_t>(task * numParams + parameter)];
                    const float error = std::abs(gpuSum - expectedSum[parameter]);
                    matched = matched && error < 1.0e-4f;
                    std::cout << " " << gpuSum << " (host " << expectedSum[parameter] << ")";
                }
                std::cout << '\n' << (matched ? "  PASS" : "  FAIL") << ": gradient comparison\n";
                allMatched = allMatched && matched;
                ++tested;
            }
            catch (const std::exception& error)
            {
                std::cerr << "  DEVICE TEST FAILED: " << error.what() << '\n';
                allMatched = false;
            }
        }
        vkDestroyInstance(instance, nullptr);
        if (tested == 0)
            throw std::runtime_error("No discrete or integrated Vulkan GPU was tested.");
        if (!allMatched)
            return 2;
        std::cout << "PASS: Vulkan Enzyme gradients match the host Enzyme results on all tested GPUs.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
