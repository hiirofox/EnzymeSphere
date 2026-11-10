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

// -----------------------------------------------------------------------------
// TrainV2 persistent state. These are the only model/cache instances used by V2.
// Same design as original loss.cpp: one persistent model and one persistent batch.
// -----------------------------------------------------------------------------
static PianoDWG::PianoWaveguide<float> instanceV2;
static PianoDWG::TrainV2::LossState dataV2;

// Renderer reverse pass: directly reuses instanceV2. No local PianoWaveguide.
static void renderV2_AD(float* params, float* audio, int numSamples)
{
	instanceV2.VecToParams(params);
	instanceV2.Reset();
	instanceV2.NoteOn(1.0f);
	instanceV2.ProcessBlock(audio, numSamples);
}

void lossV2(float* params, float* lossOut, int numParams, float* target, int numSamples)
{
	auto [totalLoss, sc, logMagnitude, harmonicEnvelope, ripple, modulation,
		attackBandSC, attackWave, shortRms, highBandFlux, positiveSlope, energyTiming] =
		PianoDWG::TrainV2::Loss(params, numParams, target, numSamples, instanceV2, dataV2);
	*lossOut = totalLoss;
}

float gradientV2(const float* params, float* grad, int numParams,
	float* target, int numSamples)
{
	if (numParams != PianoDWG::PianoWaveguide<float>::NumParams)
		throw std::runtime_error("TrainV2 gradient requires original PianoDWG parameter count: 1959");

	// Loss() prepares Target once, renders through instanceV2, and fills all 12 components.
	const PianoDWG::TrainV2::LossResult result = PianoDWG::TrainV2::Loss(
		const_cast<float*>(params), numParams, target, numSamples, instanceV2, dataV2);
	const float value = std::get<0>(result);

	std::fill(dataV2.audioGradient.begin(), dataV2.audioGradient.end(), 0.0f);
	std::fill(dataV2.workGradient.begin(), dataV2.workGradient.end(), 0.0);
	dataV2.componentsGradient.fill(0.0f);
	dataV2.componentsGradient[0] = 1.0f;
	for (int i = 0; i < numParams; ++i) grad[i] = 0.0f;

	// Pass 1: Enzyme differentiates the one canonical LossKernel dL/dAudio.
	__enzyme_autodiff(
		(void*)PianoDWG::TrainV2::LossKernel,
		enzyme_dup, dataV2.audio.data(), dataV2.audioGradient.data(),
		enzyme_dup, dataV2.components.data(), dataV2.componentsGradient.data(),
		enzyme_dup, dataV2.work.data(), dataV2.workGradient.data(),
		enzyme_const, &dataV2.cache);

	//auto [total, sc, logMagnitude, harmonicEnvelope, ripple, modulation,
	//	attackBandSC, attackWave, shortRms, highBandFlux, positiveSlope, energyTiming] = dataV2.components;
	//printf("gradientV2: total=%.5f sc=%.5f logMagnitude=%.5f harmonicEnvelope=%.5f ripple=%.5f modulation=%.5f attackBandSC=%.5f attackWave=%.5f shortRms=%.5f highBandFlux=%.5f positiveSlope=%.5f energyTiming=%.5f\n",
	//	total, sc, logMagnitude, harmonicEnvelope, ripple, modulation,
	//	attackBandSC, attackWave, shortRms, highBandFlux, positiveSlope, energyTiming);

	// Pass 2: Enzyme differentiates the original renderer through persistent instanceV2.
	__enzyme_autodiff(
		(void*)renderV2_AD,
		enzyme_dup, const_cast<float*>(params), grad,
		enzyme_dup, dataV2.audio.data(), dataV2.audioGradient.data(),
		enzyme_const, PianoDWG::TrainV2::TrainingSamples(numSamples));//, dataV2

	return value * 30.;
}
