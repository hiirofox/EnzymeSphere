#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
#include "PianoDWG2.h"

namespace TrainPianoDWG2
{

	class TrainSetup
	{
	public:
		float* targetL, * targetR;
		int numSamples = 0;
		float* otfreqs;
		int numOvertones;
		void Setup(float* targetL, float* targetR, int numSamples,
			float* overtoneFreqs, int numOvertones)
		{
			this->targetL = targetL;
			this->targetR = targetR;
			this->numSamples = numSamples;
			this->otfreqs = overtoneFreqs;
			this->numOvertones = numOvertones;
		}
	};

	class TrainPianoDWG2
	{
	private:
		TrainSetup& setup;
		PianoDWG2::PianoDWG2Voice<float> voice;
	public:
		TrainPianoDWG2(TrainSetup& setup) :setup(setup)
		{
		}

		float loss(float* params, int numParams)
		{
			voice.Reset();
			voice.VecToParams(params);
			voice.NoteOn(1.);

			double mse = 0.;
			for (int i = 0; i < setup.numSamples; ++i)
			{
				auto [rdl, rdr] = voice.ProcessSample();
				float vl = rdl - setup.targetL[i];
				float vr = rdr - setup.targetR[i];
				mse += (vl * vl + vr * vr) / setup.numSamples;
			}

			return mse;
		}
	};
}
