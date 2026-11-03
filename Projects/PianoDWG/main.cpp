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
// Default to the appended Enzyme v1.3 objective. Define DWG_TRAIN_V2=0
// to preserve the original AdamW + Train::LossSTFT training path.

AdamW<float> opt(0.0001, 0.01, 0.9, 0.999);

WavReader wr;
WavWriter ww;

int numSamples;
std::vector<float> target;
std::vector<float> tmp;

constexpr static int NumParams = PianoDWG::PianoWaveguide<float>::NumParams;
Adam<float> optV2(NumParams);
float params[NumParams];
float grads[NumParams];

PianoDWG::PianoWaveguide<float> instance1;

float bestloss = 9999999.;
float bestparams[NumParams];
void SaveCheckpoint()
{
	printf("save checkpoint!\n");
	FILE* pf = fopen((root + "checkpoint.txt").c_str(), "w");
	for (int i = 0; i < NumParams; ++i)
		fprintf(pf, "%f, ", bestparams[i]);
	fclose(pf);
}
void RenderNow()
{
	printf("render wav!\n");
	tmp.resize(target.size());
	ww.CreateWAV(root + "now.wav", PianoDWG::SampleRate);
	instance1.VecToParams(params);
	instance1.Reset();
	instance1.NoteOn(1.0);
	instance1.ProcessBlock(tmp.data(), numSamples);
	ww.WriteBlock(tmp.data(), tmp.data(), numSamples);
	ww.Close();
	ww.CreateWAV(root + "target.wav", PianoDWG::SampleRate);
	ww.WriteBlock(target.data(), target.data(), numSamples);
	ww.Close();
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

constexpr int DispFreqCapacity = 40;
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

	//基频色散估计
	//mkEstim();

	if (!LoadCheckpoint())
	{
		// C3 model seed at PianoDWG::SampleRate; optional target-head Hammer seed.
		instance1.InitParams(params, target.data(), numSamples);

	}
	for (int iter = 0, haveBest = 0;; ++iter)
	{
		float loss = gradientV2(params, grads, NumParams, target.data(), numSamples);
		MaskGradients(grads, -1);

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
		if (iter % 100 == 0)
		{
			RenderNow();
		}
		if (iter % 5 == 0)
		{
			tui.UpdateAndRepaint(params, NumParams);
		}
		optV2.Update(params, grads, NumParams, 0.0004f, 5.f);
		float lr = 0.0004f;
		float losg = 0.;
		for (int i = 0; i < NumParams; ++i)losg += grads[i] * grads[i];
		printf("iter:%d loss=%f losg=%f lr=%f\n", iter, loss, losg, lr);
	}
}