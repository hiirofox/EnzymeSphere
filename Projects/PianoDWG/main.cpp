#include <stdio.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include "PianoDWG.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"
#include "ui.h"
#include "estimationDelayK.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/tasks/";
std::string targetfile = "048C";//.wav
bool trainMode = false;


Adam<float> optV2(.0001);
//LBFGS<float> optV2(.0001);
WavReader wr;
WavWriter ww;

int numSamples;
std::vector<float> trainTargetL, trainTargetR;
std::vector<float> targetL, targetR;
std::vector<float> tmpL, tmpR;

constexpr static int NumParams = PianoDWG::PianoWaveguide2<float>::NumParams;

float params[NumParams];
float grads[NumParams];

PianoDWG::PianoWaveguide2<float> instance1;

float bestloss = 9999999.;
float bestparams[NumParams];
void SaveCheckpoint()
{
	printf("save checkpoint!\n");
	FILE* pf = fopen((root + "checkpoint-dwg2-" + targetfile + ".txt").c_str(), "w");
	for (int i = 0; i < NumParams; ++i)
		fprintf(pf, "%f, ", bestparams[i]);
	fclose(pf);
}
void RenderNow()
{
	printf("render wav!\n");
	tmpL.resize(targetL.size());
	tmpR.resize(targetR.size());
	ww.CreateWAV(root + "now-dwg2-" + targetfile + ".wav", PianoDWG::SampleRate);
	instance1.VecToParams(params);
	instance1.Reset();
	instance1.NoteOn(1.0);
	instance1.ProcessBlock(tmpL.data(), tmpR.data(), numSamples);
	ww.WriteBlock(tmpL.data(), tmpR.data(), numSamples);
	ww.Close();
	ww.CreateWAV(root + "target-" + targetfile + ".wav", PianoDWG::SampleRate);
	ww.WriteBlock(targetL.data(), targetR.data(), numSamples);
	ww.Close();
}
int LoadCheckpoint()
{
	FILE* pf = fopen((root + "checkpoint-dwg2-" + targetfile + ".txt").c_str(), "r");
	if (!pf)return 0;
	std::array<float, NumParams> loaded{};
	for (float& value : loaded)
	{
		if (fscanf(pf, "%f, ", &value) != 1 || !std::isfinite(value))
		{
			fclose(pf);
			return 0;
		}
	}
	char extra;
	const bool complete = fscanf(pf, " %c", &extra) == EOF;
	fclose(pf);
	if (complete)
		std::copy(loaded.begin(), loaded.end(), params);
	return complete;
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
	if (numSamples < 4096) return 1;
	targetL.resize(numSamples);
	targetR.resize(numSamples);
	if (wr.ReadBlock(targetL.data(), targetR.data(), numSamples) != numSamples)
		return 1;
	printf("numSamples=%d\n", numSamples);


	EstimationDelayK(targetL.data(), numSamples, dispFreqs, NumDispFreqs);
	tui.UpdateTarget(targetL.data(), targetR.data(), numSamples, dispFreqs, NumDispFreqs);

	if (!LoadCheckpoint())
		instance1.InitParams(params, targetL.data(), targetR.data(), numSamples, dispFreqs, NumDispFreqs);

	const int tCompensate = static_cast<int>(PianoDWG::SampleRate / dispFreqs[0]);
	trainTargetL.resize(tCompensate, 0.0f);
	trainTargetR.resize(tCompensate, 0.0f);
	trainTargetL.insert(trainTargetL.end(), targetL.begin(), targetL.end());
	trainTargetR.insert(trainTargetR.end(), targetR.begin(), targetR.end());

	for (int iter = 0, haveBest = 0; iter <= 2000; ++iter)
	{
		float loss = gradientV2(params, grads, NumParams, trainTargetL.data(), trainTargetR.data(), static_cast<int>(trainTargetL.size()));

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
		printf("%s | iter:%d loss=%f losg=%f\n", targetfile.c_str(), iter, loss * 30., losg);
	}
	return 1;
}