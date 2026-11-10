#include <stdio.h>
#include <string>
#include <vector>
#include "PianoDWG.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"
#include "ui.h"
#include "estimationDelayK.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/tasks/";
std::string targetfile = "022Bb";//.wav
bool trainMode = true;


// Default to the appended Enzyme v1.3 objective. Define DWG_TRAIN_V2=0
// to preserve the original AdamW + Train::LossSTFT training path.

Adam<float> optV2(.00001);
//LBFGS<float> optV2(.0001);
WavReader wr;
WavWriter ww;

int numSamples;
std::vector<float> trainTarget;
std::vector<float> target;
std::vector<float> tmp;

constexpr static int NumParams = PianoDWG::PianoWaveguide<float>::NumParams;

float params[NumParams];
float grads[NumParams];

PianoDWG::PianoWaveguide<float> instance1;

float bestloss = 9999999.;
float bestparams[NumParams];
void SaveCheckpoint()
{
	printf("save checkpoint!\n");
	FILE* pf = fopen((root + "checkpoint-" + targetfile + ".txt").c_str(), "w");
	for (int i = 0; i < NumParams; ++i)
		fprintf(pf, "%f, ", bestparams[i]);
	fclose(pf);
}
void RenderNow()
{
	printf("render wav!\n");
	tmp.resize(target.size());
	ww.CreateWAV(root + "now-" + targetfile + ".wav", PianoDWG::SampleRate);
	instance1.VecToParams(params);
	instance1.Reset();
	instance1.NoteOn(1.0);
	instance1.ProcessBlock(tmp.data(), numSamples);
	ww.WriteBlock(tmp.data(), tmp.data(), numSamples);
	ww.Close();
	ww.CreateWAV(root + "target-" + targetfile + ".wav", PianoDWG::SampleRate);
	ww.WriteBlock(target.data(), target.data(), numSamples);
	ww.Close();
}
int LoadCheckpoint()
{
	FILE* pf = fopen((root + "checkpoint-" + targetfile + ".txt").c_str(), "r");
	if (!pf)return 0;
	for (int i = 0; i < NumParams; ++i)
		fscanf(pf, "%f, ", &params[i]);
	fclose(pf);
	return 1;
}

constexpr int DispFreqCapacity = 40;
int NumDispFreqs = DispFreqCapacity;
float dispFreqs[DispFreqCapacity]{};

TrainStateUI tui;
int main(int argc, char* argv[])
{
	if (!trainMode)tui.Create();
	if (trainMode)
	{
		if (argc > 1) targetfile = argv[1];
		else return 0;
	}

	wr.OpenWAV(root + targetfile + ".wav");
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
		instance1.InitParams(params, target.data(), numSamples);
	}

	int tCompensate = PianoDWG::SampleRate / dispFreqs[0];//补偿一个周期延迟
	trainTarget.resize(tCompensate, 0.);
	trainTarget.insert(trainTarget.end(), target.begin(), target.end());

	for (int iter = 0, haveBest = 0; iter <= 2000; ++iter)
	{
		float loss = gradientV2(params, grads, NumParams, trainTarget.data(), trainTarget.size());

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
			if (!trainMode)RenderNow();
		}
		if (iter % 5 == 0)
		{
			if (!trainMode)tui.UpdateAndRepaint(params, NumParams);
		}
		optV2.Update(params, grads, NumParams, loss);
		float losg = 0.;
		for (int i = 0; i < NumParams; ++i)losg += grads[i] * grads[i];
		printf("%s | iter:%d loss=%f losg=%f\n", targetfile.c_str(), iter, loss, losg);
	}
	return 1;
}