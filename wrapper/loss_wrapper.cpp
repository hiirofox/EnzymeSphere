#include "loss.cpp"

template<typename R, typename... A>
R __enzyme_autodiff(void*, A...);

extern "C" {
extern int enzyme_const;
extern int enzyme_dup;
extern int enzyme_out;
}

#if defined(LOSS_WRAPPER_CPU)

#include "loss.h"

void TaskGradCalm(int task, int NumTasks, float* params, float* pGradOut, int NumParams)
{
    __enzyme_autodiff<void>((void*)loss,
        enzyme_const, task,
        enzyme_const, NumTasks,
        enzyme_dup, params, pGradOut,
        enzyme_const, NumParams);
}

void AllTaskGradCalm(float* paramsIn, float* pGradOut, int NumTasks, int NumParams,
                     float* taskGradScratch)
{
    for (int parameter = 0; parameter < NumParams; ++parameter)
        pGradOut[parameter] = 0.0f;
    for (int task = 0; task < NumTasks; ++task)
    {
        for (int parameter = 0; parameter < NumParams; ++parameter)
            taskGradScratch[parameter] = 0.0f;
        TaskGradCalm(task, NumTasks, paramsIn, taskGradScratch, NumParams);
        for (int parameter = 0; parameter < NumParams; ++parameter)
            pGradOut[parameter] += taskGradScratch[parameter];
    }
}

#elif defined(LOSS_WRAPPER_VULKAN) || defined(LOSS_WRAPPER_OPENCL)

#if defined(LOSS_WRAPPER_OPENCL)
#pragma OPENCL EXTENSION __cl_clang_function_pointers : enable
#endif

static float loss_component_primal(float active, int task, int NumTasks, int component,
                                   float p0, float p1, float p2)
{
    float params[3] = {p0, p1, p2};
    params[component] = active;
    return loss(task, NumTasks, params, 3);
}

#if defined(LOSS_WRAPPER_VULKAN)
static float loss_for_vulkan(int task, const float* params, int NumParams)
{
    return loss(task, 2, const_cast<float*>(params), NumParams);
}

float gradient_component(int task, float p0, float p1, float p2, int component)
{
    const float active = component == 0 ? p0 : (component == 1 ? p1 : p2);
    float params[3] = {p0, p1, p2};
    float pGrad[3] = {0.0f, 0.0f, 0.0f};
    __enzyme_autodiff<void>((void*)loss_for_vulkan,
        enzyme_const, task, enzyme_dup, params, pGrad, enzyme_const, 3);
    return pGrad[component];
}
#else

kernel void loss_gradient_kernel(global const float* params,
                                 global float* taskGradients,
                                 uint NumTasks)
{
    const int task = (int)get_global_id(0);
    if (task >= (int)NumTasks)
        return;
    const float p0 = params[0];
    const float p1 = params[1];
    const float p2 = params[2];
    taskGradients[task * 3] = __enzyme_autodiff<float>((void*)loss_component_primal,
        enzyme_out, p0, enzyme_const, task, enzyme_const, (int)NumTasks,
        enzyme_const, 0, enzyme_const, p0, enzyme_const, p1, enzyme_const, p2);
    taskGradients[task * 3 + 1] = __enzyme_autodiff<float>((void*)loss_component_primal,
        enzyme_out, p1, enzyme_const, task, enzyme_const, (int)NumTasks,
        enzyme_const, 1, enzyme_const, p0, enzyme_const, p1, enzyme_const, p2);
    taskGradients[task * 3 + 2] = __enzyme_autodiff<float>((void*)loss_component_primal,
        enzyme_out, p2, enzyme_const, task, enzyme_const, (int)NumTasks,
        enzyme_const, 2, enzyme_const, p0, enzyme_const, p1, enzyme_const, p2);
}
#endif

#else
#error Define one of LOSS_WRAPPER_CPU, LOSS_WRAPPER_VULKAN, or LOSS_WRAPPER_OPENCL
#endif
