#include <stdio.h>
#include <string>
#include <vector>
#include "PianoDWG.h"
#include "optimizer.h"
#include "wavfile.h"
#include "loss.h"

std::string root = "D:/Projects/c++/EnzymeSphere/test/";
AdamW<float> opt(0.001, 0.01, 0.9, 0.999);
WavReader wr;

int numSamples;
std::vector<float> target;

constexpr static int NumParams = PianoDWG::PianoWaveguide<float>::NumParams;
float params[NumParams];
float grads[NumParams];

int main()
{
	wr.OpenWAV(root + "C4.wav");
	numSamples = (int)wr.GetNumSamples();
	target.resize(numSamples);
	wr.ReadBlock(target.data(), target.data(), numSamples);

	for (int iter = 0;; ++iter)
	{
		float loss = gradient(params, grads, NumParams, target.data(), numSamples);
		opt.Update(params, grads, NumParams, loss);
		printf("iter:%d loss=%f\n", iter, loss);
	}
}