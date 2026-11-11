#include "loss.h"
#include "PianoDWG.h"
#include "estimationDelayK.h"

#include <cstring>
#include <type_traits>

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

void lossMK(float* params, float* lossOut, int numParams, float* target, int numTargets)
{
	*lossOut = numParams == OptimizeMK::StringOrder
		? OptimizeMK::string_loss(params, target, numTargets)
		: LossMK(params, target, numTargets);
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

namespace
{
	using Voice = PianoDWG::PianoWaveguide2<float>;
	using PianoDWG::TrainV2::LossKernel;
	using PianoDWG::TrainV2::LossState;
	using PianoDWG::TrainV2::PrepareTarget;

	Voice voice, dVoice;
	LossState left, right;

	void render(Voice* model, float* params, float* audioL, float* audioR, int count)
	{
		model->VecToParams(params);
		model->NoteOn(1.0f);
		model->ProcessBlock(audioL, audioR, count);
	}

	void prepare(float* targetL, float* targetR, int count, int numParams)
	{
		if (numParams != Voice::NumParams)
			throw std::runtime_error("PianoWaveguide2 parameter count does not match");
		PrepareTarget(targetL, count, left);
		PrepareTarget(targetR, count, right);
	}

	float audioGradient(LossState& channel)
	{
		std::fill(channel.audioGradient.begin(), channel.audioGradient.end(), 0.0f);
		std::fill(channel.workGradient.begin(), channel.workGradient.end(), 0.0);
		channel.componentsGradient.fill(0.0f);
		channel.componentsGradient[0] = 1.0f;
		__enzyme_autodiff(
			(void*)LossKernel,
			enzyme_dup, channel.audio.data(), channel.audioGradient.data(),
			enzyme_dup, channel.components.data(), channel.componentsGradient.data(),
			enzyme_dup, channel.work.data(), channel.workGradient.data(),
			enzyme_const, &channel.cache);
		return channel.components[0];
	}
}

void lossV2(float* params, float* lossOut, int numParams, float* targetL, float* targetR, int numSamples)
{
	prepare(targetL, targetR, numSamples, numParams);
	voice.Reset();
	render(&voice, params, left.audio.data(), right.audio.data(), numSamples);
	LossKernel(left.audio.data(), left.components.data(), left.work.data(), &left.cache);
	LossKernel(right.audio.data(), right.components.data(), right.work.data(), &right.cache);
	*lossOut = left.components[0] + right.components[0];
}

float gradientV2(const float* params, float* grad, int numParams,
	float* targetL, float* targetR, int numSamples)
{
	prepare(targetL, targetR, numSamples, numParams);
	voice.Reset();
	render(&voice, const_cast<float*>(params), left.audio.data(), right.audio.data(), numSamples);
	const float lossL = audioGradient(left);
	const float lossR = audioGradient(right);

	std::fill_n(grad, numParams, 0.0f);
	voice.Reset();
	// The initial DSP state is independent of the parameters. Start its adjoint at zero.
	static_assert(std::is_trivially_copyable_v<Voice>);
	std::memset(&dVoice, 0, sizeof(dVoice));
	__enzyme_autodiff(
		(void*)render,
		enzyme_dup, &voice, &dVoice,
		enzyme_dup, params, grad,
		enzyme_dup, left.audio.data(), left.audioGradient.data(),
		enzyme_dup, right.audio.data(), right.audioGradient.data(),
		enzyme_const, numSamples);
	return lossL + lossR;
}
