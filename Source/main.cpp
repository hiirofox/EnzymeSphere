#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t kElementCount = 4;

std::string getDeviceName(cl_device_id device)
{
    std::size_t size = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_NAME, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return "(unknown device)";

    std::vector<char> name(size);
    if (clGetDeviceInfo(device, CL_DEVICE_NAME, size, name.data(), nullptr) != CL_SUCCESS)
        return "(unknown device)";
    return name.data();
}
}

int main()
{
    const std::array<float, kElementCount> a{ 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, kElementCount> b{ 10.0f, 20.0f, 30.0f, 40.0f };
    std::array<float, kElementCount> result{};

    const char* kernelSource = R"CLC(
        __kernel void vector_add(
            __global const float* a,
            __global const float* b,
            __global float* result,
            const uint count)
        {
            const size_t i = get_global_id(0);
            if (i < count)
                result[i] = a[i] + b[i];
        }
    )CLC";

    cl_int status = CL_SUCCESS;
    cl_uint platformCount = 0;
    status = clGetPlatformIDs(0, nullptr, &platformCount);
    if (status != CL_SUCCESS || platformCount == 0)
    {
        std::cerr << "No OpenCL platform is available (clGetPlatformIDs returned "
                  << status << "). Install an OpenCL ICD/driver for a device on this machine.\n";
        return 1;
    }

    std::vector<cl_platform_id> platforms(platformCount);
    status = clGetPlatformIDs(platformCount, platforms.data(), nullptr);
    if (status != CL_SUCCESS)
    {
        std::cerr << "clGetPlatformIDs failed: " << status << '\n';
        return 1;
    }

    cl_device_id device = nullptr;
    for (const cl_device_type type : { CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_CPU })
    {
        for (cl_platform_id platform : platforms)
        {
            cl_uint deviceCount = 0;
            status = clGetDeviceIDs(platform, type, 0, nullptr, &deviceCount);
            if (status == CL_SUCCESS && deviceCount > 0)
            {
                std::vector<cl_device_id> devices(deviceCount);
                status = clGetDeviceIDs(platform, type, deviceCount, devices.data(), nullptr);
                if (status == CL_SUCCESS)
                {
                    device = devices.front();
                    break;
                }
            }
        }
        if (device != nullptr)
            break;
    }

    if (device == nullptr)
    {
        std::cerr << "OpenCL platforms were found, but no GPU or CPU device was available.\n";
        return 1;
    }

    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    cl_mem bufferA = nullptr;
    cl_mem bufferB = nullptr;
    cl_mem bufferResult = nullptr;
    int exitCode = 1;

    do
    {
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &status);
        if (status != CL_SUCCESS || context == nullptr)
        {
            std::cerr << "clCreateContext failed: " << status << '\n';
            break;
        }

        queue = clCreateCommandQueue(context, device, 0, &status);
        if (status != CL_SUCCESS || queue == nullptr)
        {
            std::cerr << "clCreateCommandQueue failed: " << status << '\n';
            break;
        }

        const std::size_t sourceLength = std::char_traits<char>::length(kernelSource);
        program = clCreateProgramWithSource(context, 1, &kernelSource, &sourceLength, &status);
        if (status != CL_SUCCESS || program == nullptr)
        {
            std::cerr << "clCreateProgramWithSource failed: " << status << '\n';
            break;
        }

        status = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
        if (status != CL_SUCCESS)
        {
            std::size_t logSize = 0;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::vector<char> buildLog(logSize + 1, '\0');
            if (logSize > 0)
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
            std::cerr << "clBuildProgram failed: " << status << "\n" << buildLog.data() << '\n';
            break;
        }

        kernel = clCreateKernel(program, "vector_add", &status);
        if (status != CL_SUCCESS || kernel == nullptr)
        {
            std::cerr << "clCreateKernel failed: " << status << '\n';
            break;
        }

        bufferA = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 sizeof(a), const_cast<float*>(a.data()), &status);
        if (status != CL_SUCCESS || bufferA == nullptr)
        {
            std::cerr << "Creating input buffer A failed: " << status << '\n';
            break;
        }

        bufferB = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 sizeof(b), const_cast<float*>(b.data()), &status);
        if (status != CL_SUCCESS || bufferB == nullptr)
        {
            std::cerr << "Creating input buffer B failed: " << status << '\n';
            break;
        }

        bufferResult = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(result), nullptr, &status);
        if (status != CL_SUCCESS || bufferResult == nullptr)
        {
            std::cerr << "Creating output buffer failed: " << status << '\n';
            break;
        }

        status = clSetKernelArg(kernel, 0, sizeof(bufferA), &bufferA);
        status |= clSetKernelArg(kernel, 1, sizeof(bufferB), &bufferB);
        status |= clSetKernelArg(kernel, 2, sizeof(bufferResult), &bufferResult);
        const cl_uint count = static_cast<cl_uint>(kElementCount);
        status |= clSetKernelArg(kernel, 3, sizeof(count), &count);
        if (status != CL_SUCCESS)
        {
            std::cerr << "clSetKernelArg failed: " << status << '\n';
            break;
        }

        const std::size_t globalSize = kElementCount;
        status = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &globalSize,
                                        nullptr, 0, nullptr, nullptr);
        if (status != CL_SUCCESS)
        {
            std::cerr << "clEnqueueNDRangeKernel failed: " << status << '\n';
            break;
        }

        status = clEnqueueReadBuffer(queue, bufferResult, CL_TRUE, 0, sizeof(result),
                                     result.data(), 0, nullptr, nullptr);
        if (status != CL_SUCCESS)
        {
            std::cerr << "clEnqueueReadBuffer failed: " << status << '\n';
            break;
        }

        std::cout << "OpenCL device: " << getDeviceName(device) << '\n';
        std::cout << "Vector sum: ";
        for (std::size_t i = 0; i < kElementCount; ++i)
            std::cout << result[i] << (i + 1 == kElementCount ? '\n' : ' ');
        exitCode = 0;
    } while (false);

    if (bufferResult != nullptr) clReleaseMemObject(bufferResult);
    if (bufferB != nullptr) clReleaseMemObject(bufferB);
    if (bufferA != nullptr) clReleaseMemObject(bufferA);
    if (kernel != nullptr) clReleaseKernel(kernel);
    if (program != nullptr) clReleaseProgram(program);
    if (queue != nullptr) clReleaseCommandQueue(queue);
    if (context != nullptr) clReleaseContext(context);
    return exitCode;
}
