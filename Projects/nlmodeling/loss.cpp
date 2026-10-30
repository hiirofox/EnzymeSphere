#include "loss.h"

extern int enzyme_dup;
extern int enzyme_const;
template <typename... Args>
void __enzyme_autodiff(Args...);

template<int N>
void FFT(float* re, float* im, int inv)
{
	static_assert((N & (N - 1)) == 0, "N must be power of 2");
	for (int i = 1, j = 0; i < N; ++i) {
		int bit = N >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (int len = 2; len <= N; len <<= 1) {
		float ang = 2.0f * 3.14159265358979323846f / len
			* (inv ? 1.0f : -1.0f);
		float wlen_re = std::cos(ang);
		float wlen_im = std::sin(ang);
		for (int i = 0; i < N; i += len) {
			float wr = 1.0f;
			float wi = 0.0f;
			for (int j = 0; j < len / 2; ++j) {
				int a = i + j;
				int b = a + len / 2;
				float vr = re[b] * wr - im[b] * wi;
				float vi = re[b] * wi + im[b] * wr;
				float ur = re[a];
				float ui = im[a];
				re[a] = ur + vr;
				im[a] = ui + vi;
				re[b] = ur - vr;
				im[b] = ui - vi;
				float t = wr;
				wr = t * wlen_re - wi * wlen_im;
				wi = t * wlen_im + wi * wlen_re;
			}
		}
	}
	if (inv) {
		for (int i = 0; i < N; ++i) {
			re[i] /= N;
			im[i] /= N;
		}
	}
}

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
	float* outStruct)
{
	SelectedNL::NLModelParams& nlParams = *new SelectedNL::NLModelParams;
	SelectedNL::NLModelProcess& nlproc = *new SelectedNL::NLModelProcess;
	float* y = new float[batchLen];
	float* tmpre1 = new float[WindowSize];
	float* tmpim1 = new float[WindowSize];
	float* tmpre2 = new float[WindowSize];
	float* tmpim2 = new float[WindowSize];

	nlParams.VecToParams(params);
	nlproc.Init();
	nlproc.ProcessBlock(nlParams, &testX[startPos], y, batchLen);

	float sumd8 = 0;
	float sumt8 = 0;
	for (int i = bootSize; i < batchLen - WindowSize; i += HopSize)
	{
		for (int j = 0; j < WindowSize; ++j)
		{
			tmpre1[j] = y[i + j] * window[j];
			tmpim1[j] = 0.0f;
			tmpre2[j] = targetY[i + j + startPos - delaySample] * window[j];
			tmpim2[j] = 0.0f;
		}
		FFT<WindowSize>(tmpre1, tmpim1, 0);
		FFT<WindowSize>(tmpre2, tmpim2, 0);
		for (int j = 1; j < WindowSize / 2 - 1; ++j)
		{
			//float mag1 = sqrtf(tmpre1[j] * tmpre1[j] + tmpim1[j] * tmpim1[j] + 1e-12f);
			//float mag2 = sqrtf(tmpre2[j] * tmpre2[j] + tmpim2[j] * tmpim2[j] + 1e-12f);
			float mag1 = tmpre1[j] * tmpre1[j] + tmpim1[j] * tmpim1[j];
			float mag2 = tmpre2[j] * tmpre2[j] + tmpim2[j] * tmpim2[j];

			float d = logf(mag1 + 1e-2) - logf(mag2 + 1e-2);
			float d2 = d * d * 0.000001;
			sumd8 += d2;

			/*
			float d = mag1 - mag2;
			float d2 = d * d * 0.0001f;
			float d4 = d2 * d2;
			float d8 = d4 * d4;
			sumd8 += d8;
			float t = mag2;
			float t2 = t * t * 0.0001f;
			float t4 = t2 * t2;
			float t8 = t4 * t4;
			sumt8 += t8;
			*/
		}
	}
	//sumd8 = powf(sumd8, 1.0 / 8.0);
	//sumt8 = powf(sumt8, 1.0 / 8.0);
	//float specSoftPeak = sumd8 / (sumt8 + 1e-3);
	float specSoftPeak = sumd8;

	float errSq = 0.0f;
	float errMax = 0.0f;
	float targetSq = 0.0f;
	float targetMax = 0.0f;

	float err8 = 0.0;
	float target8 = 0.0;
	for (int i = bootSize; i < batchLen; ++i)
	{
		int targIdx = i + startPos - delaySample;
		float vty = targetY[targIdx] - 0.85 * targetY[targIdx - 1];//hp
		float vy = y[i] - 0.85 * y[i - 1];//hp
		//float vty = targetY[targIdx];
		//float vy = y[i];
		const float t = vty;
		const float d = vy - vty;
		errSq += d * d;
		errMax = std::max(errMax, fabsf(d));
		targetSq += t * t;
		targetMax = std::max(targetMax, fabsf(t));

		float d2 = d * d * 0.0001;
		float d4 = d2 * d2;
		float d8 = d4 * d4;
		float t2 = t * t * 0.0001;
		float t4 = t2 * t2;
		float t8 = t4 * t4;
		err8 += d8;
		target8 += t8;
	}
	const float invN = 1.0f / static_cast<float>(batchLen - bootSize);
	const float errRMS = sqrtf(errSq * invN);
	const float targetRMS = sqrtf(targetSq * invN);
	constexpr float eps = 1e-3f;
	const float rms = errRMS / (targetRMS + eps);
	const float max = errMax / (targetMax + eps);
	const float errR8 = powf(err8, 1.0 / 8.0);
	const float targetR8 = powf(target8, 1.0 / 8.0);
	const float timeSoftPeak = errR8 / (targetR8 + eps);

	//aliasing analyze
	float aTotalEnergy = 0;
	float aliasEnergy = 0;
	for (int i = 2; i < AliasTestSize / 2 - 1; ++i)
	{
		nlproc.Init();
		for (int j = 0; j < AliasTestSize; ++j)
		{
			float x = (float)j / AliasTestSize;
			//float w = 0.5 - 0.5 * std::cos(2.0 * 3.1415926535 * x);
			float w = 1.0;
			tmpre2[j] = std::sin(2.0 * 3.1415926535 * x * i) * w * 0.25;
		}
		nlproc.ProcessBlock(nlParams, tmpre2, tmpre1, AliasTestSize);//boot
		nlproc.ProcessBlock(nlParams, tmpre2, tmpre1, AliasTestSize);
		for (int j = 0; j < AliasTestSize; ++j)
		{
			float x = (float)j / AliasTestSize;
			//float w = 0.5 - 0.5 * std::cos(2.0 * 3.1415926535 * x);
			float w = 1.0;
			tmpre1[j] *= w;
			tmpim1[j] = 0;
		}
		FFT<AliasTestSize>(tmpre1, tmpim1, 0);
		for (int j = 1; j < AliasTestSize / 2; ++j)
		{
			float mag = tmpre1[j] * tmpre1[j] + tmpim1[j] * tmpim1[j];
			aTotalEnergy += mag;
			if (j < i) aliasEnergy += mag;
		}
	}
	float cleanEnergy = aTotalEnergy - aliasEnergy;
	if (cleanEnergy < 0)cleanEnergy = 0;
	float aliasloss = aliasEnergy / (cleanEnergy + 1e-4) * 100.0;

	float structloss = nlproc.GetSructureLoss(nlParams);

	//loss eval
	//float loss = rms * 50.0 + timeSoftPeak * 100.0 + specSoftPeak * 50.0;
	//float loss = rms * 10.0 + timeSoftPeak * 10.0 + specSoftPeak * 180.0;
	float loss = rms * 10.0 +
		timeSoftPeak * 10.0 +
		aliasloss * 10.0 +
		specSoftPeak * 170.0 +
		structloss * 10.0;
	//float loss = specSoftPeak * 200.0;
	//float loss = timeSoftPeak * 200.0;
	//float loss = aliasloss * 200.0;

	*outLoss = loss;
	*specPeak = specSoftPeak;
	*outRMS = rms;
	*outMax = max;
	*outAlias = aliasloss;
	*outStruct = structloss;
}

std::tuple<float, float, float, float, float, float > get_gradient(const float* params, float* grad, int n,
	int startPos, int batchLen)
{
	std::fill(grad, grad + n, 0.0f);

	float lossValue = 0.0f;
	float specPeakValue = 0.0f;
	float rmsValue = 0.0f;
	float maxValue = 0.0f;
	float aliasValue = 0.0f;
	float structValue = 0.0f;

	float dLoss = 1.0f;
	float dSpecPeak = 0.0f;
	float dRMS = 0.0f;
	float dMax = 0.0f;
	float dAlias = 0.0f;
	float dStruct = 0.0f;
	__enzyme_autodiff(
		(void*)loss_wrapper,
		enzyme_dup, params, grad,
		enzyme_const, n,
		enzyme_const, startPos,
		enzyme_const, batchLen,
		enzyme_const, testX.data(),
		enzyme_const, targetY.data(),
		enzyme_dup, &lossValue, &dLoss,
		enzyme_dup, &specPeakValue, &dSpecPeak,
		enzyme_dup, &rmsValue, &dRMS,
		enzyme_dup, &maxValue, &dMax,
		enzyme_dup, &aliasValue, &dAlias,
		enzyme_dup, &structValue, &dStruct
	);
	return {
		lossValue,
		specPeakValue,
		rmsValue,
		maxValue,
		aliasValue,
		structValue
	};
}