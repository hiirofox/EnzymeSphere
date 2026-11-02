#include <stdio.h>
#include <string>
#include <vector>
#include "PianoDWG.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"
#include "ui.h"
#include "estimationDelayK.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/";
AdamW<float> opt(0.001, 0.01, 0.9, 0.999);
AdamW<float> optmk(0.001, 0.01, 0.9, 0.999);
WavReader wr;

int numSamples;
std::vector<float> target;

constexpr static int NumParams = PianoDWG::PianoWaveguide<float>::NumParams;
float params[NumParams];
float grads[NumParams];

PianoDWG::PianoWaveguide<float> instance1;

float bestloss = 9999999.;
float bestparams[NumParams];
void SaveCheckpoint()
{
	FILE* pf = fopen((root + "checkpoint.txt").c_str(), "w");
	for (int i = 0; i < NumParams; ++i)
		fprintf(pf, "%f, ", bestparams[i]);
	fclose(pf);
}
int LoadCheckpoint()
{
	FILE* pf = fopen((root + "checkpoint.txt").c_str(), "r");
	if (!pf)return 0;
	for (int i = 0; i < NumParams; ++i)
		fscanf(pf, "%f, ", &params[i]);
	fclose(pf);
	return 1;
}
// maskId：-1=全部，1=hammer，2=disperser，3=bridgeMatrix，4=board
// 其他值：全部梯度清零。
inline void MaskGradients(float* grads, int maskId)
{
	if (!grads || maskId == -1) return;

	constexpr int NumStrings = PianoDWG::PianoWaveguide<float>::NumStrings;
	constexpr int GroupSizes[] = {
		PianoDWG::HammerModelStatic<float>::NumParams,
		PianoDWG::LatticeAPF<float, 4>::NumParams,
		NumStrings * PianoDWG::LatticeIIR<float, 2>::NumParams,
		PianoDWG::LatticeIIR<float, 2>::NumParams
	};

	int offset = 0;
	for (int string = 0; string < NumStrings; ++string)
	{
		for (int group = 0; group < 4; ++group)
		{
			const int count = GroupSizes[group];
			if (maskId != group + 1)
			{
				for (int i = 0; i < count; ++i)
					grads[offset + i] = 0.0f;
			}
			offset += count;
		}
	}
}

constexpr int DispFreqCapacity = 400;
int NumDispFreqs = DispFreqCapacity;
float dispFreqs[DispFreqCapacity]{};
TrainStateUI tui;
int main()
{
	tui.Create();

	wr.OpenWAV(root + "C4.wav");
	numSamples = (int)wr.GetNumSamples();
	target.resize(numSamples);
	wr.ReadBlock(target.data(), target.data(), numSamples);
	printf("numSamples=%d\n", numSamples);
	if (numSamples <= 0) return 1;

	EstimationDelayK(target.data(), numSamples, dispFreqs, NumDispFreqs);
	tui.UpdateTarget(target.data(), numSamples, dispFreqs, NumDispFreqs);
	//先进行色散估计
	float mkparams[5] = { 300.,0.01,0.02,0.03,0.04 };
	float mkgrads[5];
	for (int iter = 0;; ++iter)
	{
		float loss = gradientMK(mkparams, mkgrads, 5, dispFreqs, NumDispFreqs);
		float lr = optmk.Update(mkparams, mkgrads, 5, loss);
		printf("iter:%d loss:%.5f\n", iter, loss);
	}

	if (!LoadCheckpoint())
		instance1.InitParams(params);

	for (int iter = 0, haveBest = 0;; ++iter)
	{
		float loss = gradient(params, grads, NumParams, target.data(), numSamples);
		MaskGradients(grads, 2);

		if (loss < bestloss)
		{
			bestloss = loss;
			haveBest = 1;
			memcpy(bestparams, params, sizeof(params));
		}
		if (iter % 20 == 0 && haveBest)
		{
			haveBest = 0;
			SaveCheckpoint();
		}
		if (iter % 5 == 0)
		{
			tui.UpdateAndRepaint(params, NumParams);
		}
		float lr = opt.Update(params, grads, NumParams, loss);
		float losg = 0.;
		for (int i = 0; i < NumParams; ++i)losg += grads[i] * grads[i];
		printf("iter:%d loss=%f losg=%f lr=%f\n", iter, loss, losg, lr);
	}
}