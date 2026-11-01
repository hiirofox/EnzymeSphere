#pragma once

void loss(float* params, float* lossOut, int numParams, float* target, int numSamples);
float gradient(const float* params, float* grad, int numParams, float* target, int numSamples);