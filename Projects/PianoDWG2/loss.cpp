#include "loss.h"
#include "PianoDWG2.h"
#include "estimationDelayK.h"

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

void loss(float* params, float* lossOut, int numParams, float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones)
{
}
float gradient(const float* params, float* grad, int numParams, float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones)
{
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	float lossOut, dLoss = 1.;
	__enzyme_autodiff(
		(void*)loss,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams,
		enzyme_const, targetL,
		enzyme_const, targetR,
		enzyme_const, numSamples,
		enzyme_const, overtoneFreqs,
		enzyme_const, numOvertones
		);
	return lossOut;
}
