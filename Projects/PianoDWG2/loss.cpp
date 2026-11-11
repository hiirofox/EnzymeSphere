#include "loss.h"
#include "PianoDWG2.h"
#include "namespaceTrainV2.h"
#include "estimationDelayK.h"

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	extern int enzyme_dupnoneed;
	void __enzyme_autodiff(void*, ...);
}

TrainPianoDWG2::TrainPianoDWG2 train;
static TrainPianoDWG2::TrainPianoDWG2 shadowTrain, initialShadow;
int isInit = 0;
static void lossAD(TrainPianoDWG2::TrainPianoDWG2* state, float* params, float* lossOut, int numParams)
{
	*lossOut = state->loss(params, numParams) * 300.;
}
float gradient(const float* params, float* grad, int numParams, float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones)
{
	if (!isInit)
	{
		train.InitTrainState(targetL, targetR, numSamples, overtoneFreqs, numOvertones);
		initialShadow = train;
		isInit = 1;
	}
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	float lossOut, dLoss = 1.;
	train.Reset();
	// Restore independent backing storage and discard adjoints from the previous call.
	shadowTrain = initialShadow;
	shadowTrain.Reset();
	__enzyme_autodiff(
		(void*)lossAD,
		enzyme_dup, &train, &shadowTrain,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams
	);
	return lossOut;
}
