#pragma once
#include "NLModeling.h"

void loss_wrapper(
	float* params,
	int n, int startPos, int batchLen,
	const float* testX,
	const float* targetY,
	float* outLoss,
	float* specPeak,
	float* outRMS,
	float* outMax,
	float* outAlias,
	float* outStruct);

std::tuple<float, float, float, float, float, float > get_gradient(const float* params, float* grad, int n,
	int startPos, int batchLen);


#define SelectedNL NLModeling3
constexpr static int NumParams = SelectedNL::NumParams;
constexpr static int bootSize = 48000 / 480;//留一些采样供响应稳定
constexpr static int delaySample = SelectedNL::NLModelProcess::GetTargetDelaySample();
//延迟一些采样让模型好优化，而不是学预测
//典型值：灰盒1sample，gru 2sample
int BatchSampleLen = 65536;

constexpr static int WindowSize = 1024;
constexpr static int HopSize = 512;
constexpr static int AliasTestSize = 64;

//float* testX, * targetY;
std::vector<float> testX, targetY;

float window[WindowSize];//Harris-Blackman