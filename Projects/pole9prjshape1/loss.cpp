#include "loss.h"
#include <complex>
#include "sm.h"

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

sm::SynthModel syn;
void loss(float* params, float* lossOut, int numParams)
{
	syn.LoadModel(params);
	*lossOut = syn.Loss();
}

float gradient(const float* params, float* grad, int numParams)
{
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
