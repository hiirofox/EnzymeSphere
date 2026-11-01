#include <stdio.h>
#include <string>
#include <vector>
#include "PianoDWG.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"
#include "ui.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/";
AdamW<float> opt(0.0001, 0.01, 0.1, 0.1);
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

int main()
{
	wr.OpenWAV(root + "C4.wav");
	numSamples = (int)wr.GetNumSamples();
	target.resize(numSamples);
	wr.ReadBlock(target.data(), target.data(), numSamples);
	printf("numSamples=%d\n", numSamples);

	if (!LoadCheckpoint())
		instance1.InitParams(params);

	for (int iter = 0, haveBest = 0;; ++iter)
	{
		float loss = gradient(params, grads, NumParams, target.data(), numSamples);
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
		float lr = opt.Update(params, grads, NumParams, loss);
		float losg = 0.;
		for (int i = 0; i < NumParams; ++i)losg += grads[i] * grads[i];
		printf("iter:%d loss=%f losg=%f lr=%f\n", iter, loss, losg, lr);
	}
}