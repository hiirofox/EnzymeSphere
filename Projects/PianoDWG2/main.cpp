#include <stdio.h>
#include <string>
#include <vector>
#include "PianoDWG2.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"
#include "ui.h"
#include "estimationDelayK.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/tasks/";
std::string targetfile = "022Bb";//.wav
bool trainMode = false;

Adam<float> optV2(.001);
//LBFGS<float> optV2(.0001);
WavReader wr;
WavWriter ww;

int numSamples;
std::vector<float> trainTargetL;
std::vector<float> trainTargetR;
std::vector<float> targetL;
std::vector<float> targetR;

std::vector<float> tmpL, tmpR;

constexpr static int NumParams = PianoDWG2::PianoDWG2Voice<float>::NumParams;

float params[NumParams];
float grads[NumParams];

PianoDWG2::PianoDWG2Voice<float> instance1;

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
	tmpL.resize(targetL.size());
	tmpR.resize(targetR.size());
	ww.CreateWAV(root + "now-" + targetfile + ".wav", PianoDWG2::SampleRate);
	instance1.VecToParams(params);
	instance1.Reset();
	instance1.NoteOn(1.0);
	instance1.ProcessBlock(tmpL.data(), tmpR.data(), numSamples);
	ww.WriteBlock(tmpL.data(), tmpR.data(), numSamples);
	ww.Close();
	ww.CreateWAV(root + "target-" + targetfile + ".wav", PianoDWG2::SampleRate);
	ww.WriteBlock(targetL.data(), targetR.data(), numSamples);
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
	targetL.resize(numSamples);
	targetR.resize(numSamples);
	wr.ReadBlock(targetL.data(), targetR.data(), numSamples);
	printf("numSamples=%d\n", numSamples);
	if (numSamples <= 0) return 1;

	//辨识泛音频率直接用左声道行了
	EstimationDelayK(targetL.data(), numSamples, dispFreqs, NumDispFreqs);
	tui.UpdateTarget(targetL.data(), targetR.data(), numSamples, dispFreqs, NumDispFreqs);

	if (!LoadCheckpoint())
	{
		instance1.InitParams(params, dispFreqs[0]);
	}

	int tCompensate = PianoDWG2::SampleRate / dispFreqs[0];//补偿基频一个周期延迟
	trainTargetL.resize(tCompensate, 0.);
	trainTargetR.resize(tCompensate, 0.);
	trainTargetL.insert(trainTargetL.end(), targetL.begin(), targetL.end());
	trainTargetR.insert(trainTargetR.end(), targetR.begin(), targetR.end());

	for (int iter = 0, haveBest = 0; iter <= 2000; ++iter)
	{
		float loss = gradient(params, grads, NumParams, trainTargetL.data(), trainTargetR.data(), trainTargetL.size(), dispFreqs, NumDispFreqs);

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