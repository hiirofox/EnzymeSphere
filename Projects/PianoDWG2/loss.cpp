#include "loss.h"
#include "PianoDWG2.h"
#include "namespaceTrainV2.h"
#include "estimationDelayK.h"

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

TrainPianoDWG2::TrainPianoDWG2 train;
int isInit = 0;
void loss(float* params, float* lossOut, int numParams)
{
	*lossOut = train.loss(params, numParams);
}

float gradient(const float* params, float* grad, int numParams, float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones)
{
	if (!isInit)
	{
		train.InitTrainState(targetL, targetR, numSamples, overtoneFreqs, numOvertones);
		isInit = 1;
	}
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	float lossOut, dLoss = 1.;
	__enzyme_autodiff(
		(void*)loss,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams
	);
	return lossOut;
}
