#pragma once

void loss(float* params, float* lossOut, int numParams);
float gradient(const float* params, float* grad, int numParams, float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones);
