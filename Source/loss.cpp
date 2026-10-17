template<class T>
T loss_core(T x)
{
    return x * x * x + T(2) * x;
}

#if defined(__OPENCL_CPP_VERSION__)

// C++ for OpenCL's builtin header from the bundled ClangEnzyme package is
// currently absent. The experiment supplies only the size_t typedef it needs.
#pragma OPENCL EXTENSION __cl_clang_function_pointers : enable

extern "C" float __enzyme_autodiff(void*, float);

__attribute__((noinline)) float loss_device(float x)
{
    return loss_core(x);
}

kernel void loss_gradient_kernel(global const float* inputs,
                                 global float* gradients,
                                 uint count)
{
    const size_t task = get_global_id(0);
    if (task < count)
        gradients[task] = __enzyme_autodiff((void*)loss_device, inputs[task]);
}

#else

#include "loss.h"

extern "C" double __enzyme_autodiff(void*, double);

double loss(double x)
{
    return loss_core(x);
}

double gradient(double x)
{
    return __enzyme_autodiff((void*)loss, x);
}

#endif
