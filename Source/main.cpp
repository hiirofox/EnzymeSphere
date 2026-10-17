#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

#include "loss.h"

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace
{
std::string executableDirectory()
{
    std::vector<char> path(32768, '\0');
    const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::string(path.data(), length).substr(
        0, std::string(path.data(), length).find_last_of("\\/"));
}

std::string getDeviceString(cl_device_id device, cl_device_info key)
{
    std::size_t size = 0;
    if (clGetDeviceInfo(device, key, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return {};
    std::vector<char> text(size, '\0');
    if (clGetDeviceInfo(device, key, size, text.data(), nullptr) != CL_SUCCESS)
        return {};
    return text.data();
}

std::string getPlatformString(cl_platform_id platform, cl_platform_info key)
{
    std::size_t size = 0;
    if (clGetPlatformInfo(platform, key, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return {};
    std::vector<char> text(size, '\0');
    if (clGetPlatformInfo(platform, key, size, text.data(), nullptr) != CL_SUCCESS)
        return {};
    return text.data();
}

bool check(cl_int status, const char* operation)
{
    if (status == CL_SUCCESS)
        return true;
    std::cerr << operation << " failed with OpenCL error " << status << '\n';
    return false;
}
}

int main()
{
    constexpr std::array<float, 5> inputs{ -2.0f, -0.5f, 0.0f, 1.5f, 3.0f };
    std::array<float, inputs.size()> gradients{};

    cl_uint platformCount = 0;
    if (!check(clGetPlatformIDs(0, nullptr, &platformCount), "clGetPlatformIDs") || platformCount == 0)
    {
        std::cerr << "No OpenCL platform is available.\n";
        return 1;
    }
    std::vector<cl_platform_id> platforms(platformCount);
    if (!check(clGetPlatformIDs(platformCount, platforms.data(), nullptr), "clGetPlatformIDs"))
        return 1;

    std::cout << "OpenCL device inventory:\n";
    for (cl_platform_id platform : platforms)
    {
        std::cout << "  Platform: " << getPlatformString(platform, CL_PLATFORM_NAME) << '\n';
        cl_uint allDeviceCount = 0;
        const cl_int countStatus = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &allDeviceCount);
        if (countStatus != CL_SUCCESS || allDeviceCount == 0)
            continue;
        std::vector<cl_device_id> allDevices(allDeviceCount);
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, allDeviceCount, allDevices.data(), nullptr) != CL_SUCCESS)
            continue;
        for (cl_device_id item : allDevices)
        {
            cl_device_type type = 0;
            clGetDeviceInfo(item, CL_DEVICE_TYPE, sizeof(type), &type, nullptr);
            const std::string itemIL = getDeviceString(item, CL_DEVICE_IL_VERSION);
            const std::string itemExtensions = getDeviceString(item, CL_DEVICE_EXTENSIONS);
            std::cout << "    " << ((type & CL_DEVICE_TYPE_GPU) ? "GPU" : (type & CL_DEVICE_TYPE_CPU) ? "CPU" : "other")
                      << ": " << getDeviceString(item, CL_DEVICE_NAME)
                      << " | IL=" << (itemIL.empty() ? "(none)" : itemIL)
                      << " | cl_khr_il_program="
                      << (itemExtensions.find("cl_khr_il_program") != std::string::npos ? "yes" : "no") << '\n';
        }
    }

    cl_device_id device = nullptr;
    for (cl_platform_id platform : platforms)
    {
        cl_uint count = 0;
        const cl_int query = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &count);
        if (query != CL_SUCCESS || count == 0)
            continue;
        std::vector<cl_device_id> devices(count);
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, count, devices.data(), nullptr) == CL_SUCCESS)
        {
            for (cl_device_id candidate : devices)
            {
                if (getDeviceString(candidate, CL_DEVICE_IL_VERSION).find("SPIR-V") != std::string::npos)
                {
                    device = candidate;
                    break;
                }
            }
        }
        if (device != nullptr)
            break;
    }
    if (device == nullptr)
    {
        std::cerr << "No OpenCL GPU with SPIR-V IL support is available.\n";
        return 1;
    }

    const std::string ilVersion = getDeviceString(device, CL_DEVICE_IL_VERSION);
    std::cout << "OpenCL GPU: " << getDeviceString(device, CL_DEVICE_NAME) << '\n';
    std::cout << "OpenCL version: " << getDeviceString(device, CL_DEVICE_VERSION) << '\n';
    std::cout << "SPIR-V IL version: " << (ilVersion.empty() ? "not reported" : ilVersion) << '\n';
    const std::string extensions = getDeviceString(device, CL_DEVICE_EXTENSIONS);
    std::cout << "cl_khr_il_program: "
              << (extensions.find("cl_khr_il_program") != std::string::npos ? "yes" : "no") << '\n';

    const std::string spirvPath = executableDirectory() + "\\enzyme_loss_gradient.spv";
    std::ifstream file(spirvPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Cannot open SPIR-V module: " << spirvPath << '\n';
        return 1;
    }
    const std::vector<unsigned char> spirv(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>{});
    if (spirv.empty())
    {
        std::cerr << "SPIR-V module is empty.\n";
        return 1;
    }

    cl_int status = CL_SUCCESS;
    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &status);
    if (!check(status, "clCreateContext") || context == nullptr)
        return 1;

    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &status);
    if (!check(status, "clCreateCommandQueue") || queue == nullptr)
    {
        clReleaseContext(context);
        return 1;
    }

    cl_program program = clCreateProgramWithIL(context, spirv.data(), spirv.size(), &status);
    if (!check(status, "clCreateProgramWithIL") || program == nullptr)
    {
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    status = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (!check(status, "clBuildProgram"))
    {
        std::size_t logSize = 0;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> log(logSize + 1, '\0');
        if (logSize > 0)
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
        std::cerr << log.data() << '\n';
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    cl_kernel kernel = clCreateKernel(program, "loss_gradient_kernel", &status);
    if (!check(status, "clCreateKernel") || kernel == nullptr)
    {
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    cl_mem inputBuffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        sizeof(inputs), const_cast<float*>(inputs.data()), &status);
    if (!check(status, "clCreateBuffer(inputs)"))
    {
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }
    cl_mem gradientBuffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(gradients), nullptr, &status);
    if (!check(status, "clCreateBuffer(gradients)"))
    {
        clReleaseMemObject(inputBuffer);
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    const cl_uint count = static_cast<cl_uint>(inputs.size());
    status = clSetKernelArg(kernel, 0, sizeof(inputBuffer), &inputBuffer);
    status |= clSetKernelArg(kernel, 1, sizeof(gradientBuffer), &gradientBuffer);
    status |= clSetKernelArg(kernel, 2, sizeof(count), &count);
    if (!check(status, "clSetKernelArg"))
    {
        clReleaseMemObject(gradientBuffer);
        clReleaseMemObject(inputBuffer);
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    const std::size_t globalSize = inputs.size();
    status = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &globalSize, nullptr, 0, nullptr, nullptr);
    if (status == CL_SUCCESS)
        status = clEnqueueReadBuffer(queue, gradientBuffer, CL_TRUE, 0, sizeof(gradients), gradients.data(),
                                     0, nullptr, nullptr);
    if (!check(status, "OpenCL kernel execution"))
    {
        clReleaseMemObject(gradientBuffer);
        clReleaseMemObject(inputBuffer);
        clReleaseKernel(kernel);
        clReleaseProgram(program);
        clReleaseCommandQueue(queue);
        clReleaseContext(context);
        return 1;
    }

    bool matches = true;
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        const double hostGradient = gradient(static_cast<double>(inputs[i]));
        const double difference = std::abs(static_cast<double>(gradients[i]) - hostGradient);
        std::cout << "x=" << inputs[i] << " GPU gradient=" << gradients[i]
                  << " host Enzyme gradient=" << hostGradient << '\n';
        matches = matches && difference < 1.0e-5;
    }

    clReleaseMemObject(gradientBuffer);
    clReleaseMemObject(inputBuffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    if (!matches)
    {
        std::cerr << "GPU SPIR-V gradients differ from host Enzyme gradients.\n";
        return 3;
    }
    std::cout << "PASS: Enzyme-generated SPIR-V gradients ran on the OpenCL GPU and match host results.\n";
    return 0;
}
