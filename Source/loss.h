#pragma once

float loss(int task, int NumTasks, float* params, int NumParams);

void TaskGradCalm(int task, int NumTasks, float* params, float* pGradOut, int NumParams);
void AllTaskGradCalm(float* paramsIn, float* pGradOut, int NumTasks, int NumParams,
                     float* taskGradScratch);
