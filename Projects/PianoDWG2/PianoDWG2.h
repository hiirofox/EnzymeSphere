#pragma once
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <limits>
#include <vector>
#include <array>
#include <memory>

#include <fstream>
#include <numeric>
#include <stdexcept>
#include <cstddef>
#include <complex>

#include "define.h"
#include "estimationDelayK.h"

namespace PianoDWG2
{
	template <typename Sample, int NumLayers>
	class LatticeAPF
	{
	private:
		Sample z[NumLayers], k[NumLayers];

		template<int layer>
		inline Sample ProcessLattice(Sample x)
		{
			if constexpr (layer >= NumLayers) return x;
			else
			{
				Sample z1 = z[layer];
				Sample a = x - z1 * k[layer];
				z[layer] = ProcessLattice<layer + 1>(a);
				return z1 + a * k[layer];
			}
		}
	public:
		constexpr static int NumParams = NumLayers;
		void InitParams(Sample* out) //直通(延迟NumLayers个采样)
		{
			for (int i = 0; i < NumLayers; ++i)k[i] = rand() / RAND_MAX * (rand() % 2 ? 1 : -1);
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				k[i] = tanh(in[pos++]);
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				out[pos++] = atanh(k[i]);
		}
		void Reset()
		{
			for (int i = 0; i < NumLayers; ++i)z[i] = 0.;
		}
		inline Sample ProcessSample(Sample x)
		{
			return ProcessLattice<0>(x);
		}
	};
	template <typename Sample, int NumLayers>
	class CascadeAPF
	{
	private:
		Sample z[NumLayers], k[NumLayers];

		inline Sample ProcessLattice(Sample x)
		{
			for (int layer = 0; layer < NumLayers; ++layer)
			{
				Sample z1 = z[layer];
				Sample a = x - z1 * k[layer];
				z[layer] = a;
				x = z1 + a * k[layer];
			}
			return x;
		}
	public:
		constexpr static int NumParams = NumLayers;
		void InitParams(Sample* out) //直通(延迟NumLayers个采样)
		{
			for (int i = 0; i < NumLayers; ++i)k[i] = -0.744342145;
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
			{//k[i] = tanh(in[pos++]);
				float v = in[pos++];
				if (v < -1.)v = -1.;
				if (v > 1.)v = 1.;
				k[i] = v;
			}
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				//out[pos++] = atanh(k[i]);
				out[pos++] = k[i];
		}
		void Reset()
		{
			for (int i = 0; i < NumLayers; ++i)z[i] = 0.;
		}
		inline Sample ProcessSample(Sample x)
		{
			return ProcessLattice(x);
		}
	};
	template <typename Sample, int NumLayers>
	class LatticeIIR
	{
	private:
		Sample z[NumLayers];
		Sample k[NumLayers], g[NumLayers + 1];

		template<int layer>
		inline std::tuple<Sample, Sample> ProcessLattice(Sample x)
		{
			if constexpr (layer >= NumLayers) return { x,x * g[layer] };
			else
			{
				Sample z1 = z[layer];
				Sample a = x - z1 * k[layer];
				auto [next, sum] = ProcessLattice<layer + 1>(a);
				z[layer] = next;
				Sample y = z1 + a * k[layer];
				return { y ,sum + g[layer] * y };
			}
		}
	public:
		constexpr static int NumParams = NumLayers * 2 + 1;
		void InitParams(Sample* out) //直通
		{
			for (int i = 0; i < NumLayers; ++i)k[i] = 0.;
			for (int i = 0; i < NumLayers + 1; ++i)g[i] = 0.;
			g[NumLayers] = 1.;
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				k[i] = tanh(in[pos++]);
			for (int i = 0; i < NumLayers + 1; ++i)
				g[i] = in[pos++];
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				out[pos++] = atanh(k[i]);
			for (int i = 0; i < NumLayers + 1; ++i)
				out[pos++] = g[i];
		}
		void Reset()
		{
			for (int i = 0; i < NumLayers; ++i)z[i] = 0.;
		}
		inline Sample ProcessSample(Sample x)
		{
			auto [next, sum] = ProcessLattice<0>(x);
			return sum;
		}
	};

	template <typename Sample, int NumCells, int NumLatticeLayers>
	class NLCells2
	{
	public:
		constexpr static int NumParams = NumCells * (LatticeIIR<Sample, NumLatticeLayers>::NumParams + 8);
	private:
		LatticeIIR<Sample, NumLatticeLayers> iirs[NumCells];
		Sample gnlout[NumCells], gdry[NumCells];
		Sample a1[NumCells], a2[NumCells], a3[NumCells];
		Sample b1[NumCells], b2[NumCells], b3[NumCells];
		Sample Nonlinear(Sample x, int i)
		{
			Sample absx = fabs(x);
			Sample x2 = absx * absx;
			Sample x3 = x2 * absx;
			Sample num = b1[i] * absx + b2[i] * x2 + b3[i] * x3;
			Sample den = fabs(a1[i] * absx + a2[i] * x2 + a3[i] * x3) + 1.;
			return copysign(num / den, x);
		}
	public:
		void InitParams(Sample* out)
		{
			for (int i = 0; i < NumCells; ++i)
			{
				iirs[i].InitParams(nullptr);
				gnlout[i] = 1.;
				gdry[i] = 0.;
				a1[i] = 0.;
				a2[i] = 0.;
				a3[i] = 0.;
				b1[i] = 1.;
				b2[i] = 0.;
				b3[i] = 0.;
			}
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumCells; ++i)
			{
				iirs[i].VecToParams(in + pos);
				pos += LatticeIIR<Sample, NumLatticeLayers>::NumParams;
				gnlout[i] = in[pos++];
				gdry[i] = in[pos++];
				a1[i] = in[pos++];
				a2[i] = in[pos++];
				a3[i] = in[pos++];
				b1[i] = in[pos++];
				b2[i] = in[pos++];
				b3[i] = in[pos++];
			}
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumCells; ++i)
			{
				iirs[i].ParamsToVec(out + pos);
				pos += LatticeIIR<Sample, NumLatticeLayers>::NumParams;
				out[pos++] = gnlout[i];
				out[pos++] = gdry[i];
				out[pos++] = a1[i];
				out[pos++] = a2[i];
				out[pos++] = a3[i];
				out[pos++] = b1[i];
				out[pos++] = b2[i];
				out[pos++] = b3[i];
			}
		}
		void Reset()
		{
			for (int i = 0; i < NumCells; ++i)
				iirs[i].Reset();
		}
		inline Sample ProcessSample(Sample x)
		{
			for (int i = 0; i < NumCells; ++i)
				x = Nonlinear(iirs[i].ProcessSample(x), i) * gnlout[i] + x * gdry[i];
			return x;
		}
	};

	template<typename Sample, int MaxDelayTime>
	class DelayLine2
	{
	private:
		Sample buffer[MaxDelayTime];
		int pos = 0;
		int delay = 0;
	public:
		constexpr static int NumParams = 1;

		void InitParams(Sample* out, int delaySample)
		{
			delay = delaySample;
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			SetDelay((int)in[0]);
		}
		void ParamsToVec(Sample* out)
		{
			out[0] = (Sample)delay;
		}
		void SetDelay(int d)
		{
			delay = d;
			if (delay < 0) delay = 0;
			if (delay >= MaxDelayTime) delay = MaxDelayTime - 1;
		}
		Sample ProcessSample(Sample x)
		{
			if (delay < 1)return x;
			Sample y = buffer[pos];
			buffer[pos] = x;
			++pos;
			if (pos >= delay) pos = 0;
			return y;
		}
		void Reset()
		{
			for (int i = 0; i < MaxDelayTime; ++i) buffer[i] = 0.;
			pos = 0;
		}
	};
	template<typename Sample, int TableSize >
	class HammerModel2
	{
	public:
		constexpr static int NumParams = TableSize;
		Sample hammer[TableSize] = {};
		int pos = 0;
		Sample g = Sample(0);
	public:
		void InitParams(Sample* out)
		{
			for (int i = 0; i < TableSize; ++i) hammer[i] = (float)rand() / RAND_MAX * (rand() % 2 ? 1 : -1) / TableSize;
			//for (int i = 0; i < TableSize; ++i) hammer[i] = 0.;
			//ammer[0] = 1.;
			if (out)
			{
				ParamsToVec(out);
			}
		}
		void VecToParams(Sample* in)
		{
			for (int i = 0; i < TableSize; ++i) hammer[i] = in[i];
		}
		void ParamsToVec(Sample* out)
		{
			for (int i = 0; i < TableSize; ++i) out[i] = hammer[i];
		}
		void Reset()
		{
			pos = 0;
		}
		void NoteOn(Sample velocity)
		{
			pos = 0;
			g = velocity;
		}
		inline Sample ProcessSample()
		{
			if (pos >= TableSize) return Sample(0);
			const Sample x = hammer[pos] * g;
			++pos;
			return x;
		}
	};
	template <typename Sample, int N>
	class FIR
	{
	private:
		Sample coeffs[N];
		Sample buffer[N * 2] = {};
		int head = 0;
	public:
		constexpr static int NumParams = N;
		void InitParams(Sample* out, Sample v)
		{
			for (int i = 0; i < N; ++i) coeffs[i] = 0.;
			coeffs[N / 2] = v;
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			for (int i = 0; i < N; ++i) coeffs[i] = in[i];
		}
		void ParamsToVec(Sample* out)
		{
			for (int i = 0; i < N; ++i) out[i] = coeffs[i];
		}
		void Reset()
		{
			for (int i = 0; i < N; ++i) buffer[i] = 0.;
		}
		inline Sample ProcessSample(Sample x)
		{
			head = (head == 0) ? N - 1 : head - 1;
			buffer[head] = buffer[head + N] = x;
			Sample y = Sample(0);
			for (int i = 0; i < N; ++i)
				y += buffer[head + i] * coeffs[i];
			return y;
		}
	};

	template <typename Sample>
	class PianoDWG2Voice
	{
	public:
		constexpr static int FirSize = 16;
		using Hammer = HammerModel2<Sample, 768>;
		using DelayLine = DelayLine2<Sample, 48000>;
		using FIR = FIR<Sample, FirSize>;
		using Nonlinear = NLCells2<Sample, 4, 4>;

		constexpr static int NumStrings = 4;
		constexpr static int NumParams =
			Hammer::NumParams * NumStrings + //Hammer
			DelayLine::NumParams * NumStrings + //DelayLine
			FIR::NumParams * NumStrings * NumStrings + //FIR matrix
			FIR::NumParams * NumStrings * 2 + //FIR mixl + FIR mixr
			Nonlinear::NumParams * 2;//Nonlinear nll + Nonlinear nlr
	private:
		Hammer hammer[NumStrings];
		DelayLine delay[NumStrings];
		FIR matrix[NumStrings][NumStrings];
		FIR mixl[NumStrings];
		FIR mixr[NumStrings];
		Nonlinear nll;
		Nonlinear nlr;

		Sample pass[NumStrings];
		Sample dampV = 1.0;
		Sample releaseDampV = 0.995;
	public:
		void NoteOn(Sample velocity)
		{
			dampV = 1.;
			for (int i = 0; i < NumStrings; ++i)
				hammer[i].NoteOn(velocity);
		}
		void NoteOff()
		{
			dampV = releaseDampV;
		}
		void InitParams(Sample* out, Sample f0)
		{
			int delaySample[NumStrings];
			for (int i = 0; i < NumStrings; ++i)
			{
				int n = SampleRate / f0 - 1 - FirSize / 2;
				if (n < 0) n = 0;
				delaySample[i] = n;
			}

			for (int i = 0; i < NumStrings; ++i)
				hammer[i].InitParams(out + i * Hammer::NumParams);
			int pos = NumStrings * Hammer::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				delay[i].InitParams(out + pos, delaySample[i]), pos += DelayLine::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				for (int j = 0; j < NumStrings; ++j)
				{
					if (i == j)//对角
						matrix[i][j].InitParams(out + pos, Sample(1. / NumStrings * 0.95)), pos += FIR::NumParams;
					else
						matrix[i][j].InitParams(out + pos, Sample(-0.06)), pos += FIR::NumParams;
				}
			for (int i = 0; i < NumStrings; ++i)
				mixl[i].InitParams(out + pos, Sample(1. / NumStrings)), pos += FIR::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				mixr[i].InitParams(out + pos, Sample(1. / NumStrings)), pos += FIR::NumParams;
			nll.InitParams(out + pos), pos += Nonlinear::NumParams;
			nlr.InitParams(out + pos), pos += Nonlinear::NumParams;
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumStrings; ++i)
				hammer[i].VecToParams(in + pos), pos += Hammer::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				delay[i].VecToParams(in + pos), pos += DelayLine::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				for (int j = 0; j < NumStrings; ++j)
					matrix[i][j].VecToParams(in + pos), pos += FIR::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				mixl[i].VecToParams(in + pos), pos += FIR::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				mixr[i].VecToParams(in + pos), pos += FIR::NumParams;
			nll.VecToParams(in + pos), pos += Nonlinear::NumParams;
			nlr.VecToParams(in + pos), pos += Nonlinear::NumParams;
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumStrings; ++i)
				hammer[i].ParamsToVec(out + pos), pos += Hammer::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				delay[i].ParamsToVec(out + pos), pos += DelayLine::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				for (int j = 0; j < NumStrings; ++j)
					matrix[i][j].ParamsToVec(out + pos), pos += FIR::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				mixl[i].ParamsToVec(out + pos), pos += FIR::NumParams;
			for (int i = 0; i < NumStrings; ++i)
				mixr[i].ParamsToVec(out + pos), pos += FIR::NumParams;
			nll.ParamsToVec(out + pos), pos += Nonlinear::NumParams;
			nlr.ParamsToVec(out + pos), pos += Nonlinear::NumParams;
		}
		inline std::tuple<Sample, Sample> ProcessSample()
		{
			for (int i = 0; i < NumStrings; ++i)
				pass[i] += hammer[i].ProcessSample();
			for (int i = 0; i < NumStrings; ++i)
				pass[i] = delay[i].ProcessSample(pass[i]);
			Sample tmp[NumStrings];
			for (int i = 0; i < NumStrings; ++i)
			{
				Sample y{ 0. };
				for (int j = 0; j < NumStrings; ++j)
					y += matrix[i][j].ProcessSample(pass[j]);
				tmp[i] = y;
			}
			for (int i = 0; i < NumStrings; ++i)
				pass[i] = tmp[i] * dampV;
			Sample l{ 0. }, r{ 0. };
			for (int i = 0; i < NumStrings; ++i)
			{
				l += mixl[i].ProcessSample(tmp[i]);
				r += mixr[i].ProcessSample(tmp[i]);
			}
			l = nll.ProcessSample(l);
			r = nlr.ProcessSample(r);
			return { l,r };
		}
		void ProcessBlock(Sample* outl, Sample* outr, int numSamples)
		{
			for (int i = 0; i < numSamples; ++i)
			{
				auto [l, r] = ProcessSample();
				outl[i] = l;
				outr[i] = r;
			}
		}
	};
}