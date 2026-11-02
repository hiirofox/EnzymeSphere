#include "loss.h"
#include "PianoDWG.h"
#include "estimationDelayK.h"

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

PianoDWG::PianoWaveguide<float> instance;
PianoDWG::Train::LossSTFTData batch;
void loss(float* params, float* lossOut, int numParams, float* target, int numSamples)
{
	//*lossOut = PianoDWG::TrainSimple::LossSimple(params, numParams, target, numSamples, &instance) * 10000.;
	*lossOut = PianoDWG::Train::LossSTFT(params, numParams, target, numSamples, &instance, &batch);
}
float gradient(const float* params, float* grad, int numParams, float* target, int numSamples)
{
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	float lossOut, dLoss = 1.;
	__enzyme_autodiff(
		(void*)loss,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams,
		enzyme_const, target,
		enzyme_const, numSamples
	);
	return lossOut;
}

void lossMK(float* params, float* lossOut, int numParams, float* target, int numTargets)
{
	*lossOut = LossMK(params, target, numTargets);
}
float gradientMK(const float* params, float* grad, int numParams, float* target, int numTargets)
{
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	float lossOut, dLoss = 1.;
	__enzyme_autodiff(
		(void*)lossMK,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams,
		enzyme_const, target,
		enzyme_const, numTargets
	);
	return lossOut;
}