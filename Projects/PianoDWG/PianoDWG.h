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

namespace PianoDWG
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

	template <typename Sample>
	class NLCells2
	{
	public:
		constexpr static int NumCells = 4;
		constexpr static int NumLatticeLayers = 4;
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
		void InitParams(Sample* out) //直通
		{
			for (int i = 0; i < NumCells; ++i)
			{
				iirs[i].InitParams(out);
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

	template <typename Sample, int MaxDelay>
	class DelayLine
	{
	private:
		Sample buf[MaxDelay];
		int time, pos = 0;
	public:
		void SetDelayTime(int sample)
		{
			time = sample;
			if (time < 0)time = 0;
			if (time >= MaxDelay)time = MaxDelay - 1;
		}
		void Reset()
		{
			pos = 0;
			for (int i = 0; i < MaxDelay; ++i)
				buf[i] = 0;
		}
		Sample ProcessSample(Sample x)
		{
			int idx = pos + time;
			if (idx >= MaxDelay)idx -= MaxDelay;
			buf[idx] = x;
			Sample y = buf[pos];
			pos++;
			if (pos >= MaxDelay)pos = 0;
			return y;
		}
	};
	template<typename Sample, int MaxDelayTime>
	class DelayLine2
	{
		static_assert(MaxDelayTime >= 1, "MaxDelayTime must be positive");
		static constexpr int Capacity = MaxDelayTime + 1;
		Sample buffer[Capacity];
		Sample delayTime = Sample(1);
		int writePos = 0;
		int validSamples = 0;
		inline Sample ReadTap(int delay) const
		{
			if (delay >= validSamples) return Sample(0);
			int pos = writePos - delay;
			if (pos < 0) pos += Capacity;
			return buffer[pos];
		}
	public:
		void SetDelayTime(Sample value)
		{
			if (!(value >= Sample(0))) value = Sample(0);
			if (value > Sample(MaxDelayTime)) value = Sample(MaxDelayTime);
			delayTime = value;
		}
		constexpr static int NumParams = 1;
		void InitParams(float* out)
		{
			SetDelayTime(300.);
		}
		void ParamsToVec(float* out)
		{
			out[0] = delayTime;
		}
		void VecToParams(float* in)
		{
			SetDelayTime(in[0]);
		}
		void Reset()
		{
			writePos = 0;
			validSamples = 0;
		}
		Sample ProcessSample(Sample x)
		{
			buffer[writePos] = x;
			if (validSamples < Capacity) ++validSamples;
			const int integerDelay = static_cast<int>(delayTime);
			const Sample fraction = delayTime - Sample(integerDelay);
			const Sample newer = ReadTap(integerDelay);
			const Sample older = ReadTap(integerDelay + 1);
			const Sample y = newer + fraction * (older - newer);
			if (++writePos == Capacity) writePos = 0;
			return y;
		}
	};
	template <typename Sample, int DelaySample>
	class DelayLineStatic
	{
	private:
		constexpr static int BufferSize =
			DelaySample > 0 ? DelaySample : 1;
		Sample buf[BufferSize];
		// 编译期展开移位操作
		template <std::size_t... I>
		inline void ShiftIn(
			Sample x,
			std::index_sequence<I...>)
		{
			if constexpr (sizeof...(I) > 0)
				((buf[DelaySample - 1 - I] = buf[DelaySample - 2 - I]), ...);
			buf[0] = x;
		}
		// 编译期展开初始化
		template <std::size_t... I>
		inline void Clear(std::index_sequence<I...>)
		{
			((buf[I] = Sample(0)), ...);
		}
	public:
		constexpr static int NumDelay = DelaySample;
		void Reset()
		{
			Clear(std::make_index_sequence<BufferSize>{});
		}
		inline Sample ProcessSample(Sample x)
		{
			Sample y = buf[DelaySample - 1];
			ShiftIn(x, std::make_index_sequence<DelaySample - 1>{});
			return y;
		}
	};

	template <typename Sample>
	class HammerModel
	{
	public:
		constexpr static int TableSize = 512;
		constexpr static int NumParams = TableSize + NLCells2<Sample>::NumParams;
	private:
		NLCells2<Sample> nlcells;
		Sample hammer[TableSize];
		int pos = 0;
		Sample g;//这不是参数
	public:
		void InitParams(Sample* out)
		{
			for (int i = 0; i < TableSize; ++i) hammer[i] = 0.;
			hammer[0] = 1.;
			nlcells.InitParams(out + TableSize);
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < TableSize; ++i)
				hammer[i] = in[pos++];
			nlcells.VecToParams(in + pos);
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < TableSize; ++i)
				out[pos++] = hammer[i];
			nlcells.ParamsToVec(out + pos);
		}
		void Reset()
		{
			pos = 0;
			nlcells.Reset();
		}
		void NoteOn(Sample velocity)
		{
			pos = 0;
			g = velocity;
		}
		Sample ProcessSample()
		{
			if (pos < TableSize) return nlcells.ProcessSample(hammer[pos++] * g);
			else return 0.;
		}
	};
	template<typename Sample>
	class HammerModel2
	{
	public:
		constexpr static int TableSize = 512;
		constexpr static int NumParams = TableSize + NLCells2<Sample>::NumParams;
	private:
		NLCells2<Sample> nlcells;
		Sample hammer[TableSize];
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
				nlcells.InitParams(out + TableSize);
				ParamsToVec(out);
			}
		}
		void VecToParams(Sample* in)
		{
			for (int i = 0; i < TableSize; ++i) hammer[i] = in[i];
			nlcells.VecToParams(in + TableSize);
		}
		void ParamsToVec(Sample* out)
		{
			for (int i = 0; i < TableSize; ++i) out[i] = hammer[i];
			nlcells.ParamsToVec(out + TableSize);
		}
		void Reset()
		{
			pos = 0;
			nlcells.Reset();
		}
		void NoteOn(Sample velocity)
		{
			pos = 0;
			g = velocity;
		}
		Sample ProcessSample()
		{
			if (pos >= TableSize) return Sample(0);
			const Sample x = hammer[pos] * g;
			++pos;
			return nlcells.ProcessSample(x);
		}
	};
	template <typename Sample>
	class HammerModelStatic
	{
	public:
		constexpr static int TableSize = 512;
		constexpr static int NumParams = TableSize + NLCells2<Sample>::NumParams;
	private:
		NLCells2<Sample> nlcells;
		Sample hammer[TableSize];
		int pos = 0;
		Sample g;
		template <std::size_t... I>
		void InitHammer(std::index_sequence<I...>)
		{
			((hammer[I] = Sample(0)), ...);
			hammer[0] = Sample(1);
		}
		template <std::size_t... I>
		void LoadHammer(Sample* in, std::index_sequence<I...>)
		{
			((hammer[I] = in[I]), ...);
		}
		template <std::size_t... I>
		void SaveHammer(Sample* out, std::index_sequence<I...>) const
		{
			((out[I] = hammer[I]), ...);
		}
		template <std::size_t... I>
		Sample SelectHammer(std::index_sequence<I...>) const
		{
			Sample x = Sample(0);
			((x += (pos == static_cast<int>(I) ? hammer[I] : Sample(0))), ...);
			return x;
		}
	public:
		void InitParams(Sample* out)
		{
			Sample dummy[NLCells2<Sample>::NumParams];
			InitHammer(std::make_index_sequence<TableSize>{});
			nlcells.InitParams(out ? out + TableSize : dummy);
			pos = 0;
			g = Sample(0);
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			LoadHammer(in, std::make_index_sequence<TableSize>{});
			nlcells.VecToParams(in + TableSize);
		}
		void ParamsToVec(Sample* out)
		{
			SaveHammer(out, std::make_index_sequence<TableSize>{});
			nlcells.ParamsToVec(out + TableSize);
		}
		void Reset()
		{
			pos = 0;
			g = Sample(0);
			nlcells.Reset();
		}
		void NoteOn(Sample velocity)
		{
			pos = 0;
			g = velocity;
		}
		Sample ProcessSample()
		{
			if (pos >= TableSize) return Sample(0);
			Sample x = SelectHammer(std::make_index_sequence<TableSize>{});
			++pos;
			return nlcells.ProcessSample(x * g);
		}
	};
	template <typename Sample>
	class PianoWaveguide
	{
	public:
		using Hammer = HammerModel2<Sample>;
		using Delay = DelayLine2<Sample, 1024>;
		using Disperser = CascadeAPF<Sample, 4>;
		using Bridge = LatticeIIR<Sample, 8>;
		using Board = LatticeIIR<Sample, 8>;

		constexpr static float SampleRate = PianoDWG::SampleRate;
		constexpr static int NumStrings = 3;
		constexpr static int NumParams =
			NumStrings * (Hammer::NumParams +
				Disperser::NumParams +
				NumStrings * Bridge::NumParams +
				Board::NumParams) + NumStrings;
	public:
		//HammerModel<Sample> hammer[NumStrings];//琴槌模型
		//HammerModelStatic<Sample> hammer[NumStrings];//琴槌模型
		Hammer hammer[NumStrings];//琴槌模型

		//DelayLine<Sample, (int)SampleRate> delay[NumStrings];//波导延迟
		//DelayLineStatic<Sample, 329> delay[NumStrings];//波导延迟
		Delay delay[NumStrings];//波导延迟

		//LatticeAPF<Sample, 4> disperser[NumStrings];//琴弦色散
		Disperser disperser[NumStrings];//琴弦色散
		Bridge bridgeMatrix[NumStrings][NumStrings];//琴桥耦合响应
		Board board[NumStrings];//琴板响应

		Sample releaseDampV = 0.999;//释放阻尼系数

		Sample pass[NumStrings];//状态
		Sample damp = 1.;
	public:
		PianoWaveguide()
		{
			//SetBaseFreq(261.63);//C4
			Reset();
		}
		// C3 initialization only; the waveguide's DDSP signal graph remains unchanged.
		// Optional reference attack supplies Hammer's initial zero pattern, not pretraining.
		void InitParams(Sample* out, const Sample* target = nullptr, int numSamples = 0)
		{
			Sample* base = out;
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].InitParams(out);
				out += Hammer::NumParams;
				delay[i].InitParams(out);
				out += 1;
				disperser[i].InitParams(out);
				out += Disperser::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					bridgeMatrix[i][j].InitParams(out);
					out += Bridge::NumParams;
				}
				board[i].InitParams(out);
				out += Board::NumParams;
			}
			if (!base) return;
			constexpr int stride = NumParams / NumStrings;
			static_assert(Hammer::TableSize == 512, "Keep the original 512-sample Hammer");

			//泛音基频色散估计
			const int overtoneFreqTableLen = 25;
			float overtoneFreqTable[overtoneFreqTableLen];
			EstimationDelayK((float*)target, numSamples, overtoneFreqTable, overtoneFreqTableLen, (float)SampleRate);
			auto [zm, k0, k1, k2, k3] = mkEstim(overtoneFreqTable, overtoneFreqTableLen);
			float ks[] = { k0, k1, k2, k3 };

			for (int i = 0; i < NumStrings; ++i)
			{
				Sample* p = base + i * stride;
				// Target-head alignment accounts for the initial delay and APF phase response.
				// C3 / 22050 Hz: 170 samples instead of the previously assumed 155.
				constexpr int TargetHeadOffset = 0;
				constexpr int ReturnSamples = 0;
				for (int j = 0; j < Hammer::TableSize; ++j)
				{
					if (target && numSamples > j + TargetHeadOffset)
					{
						const Sample desired = target[j + TargetHeadOffset];
						const Sample returning = j >= ReturnSamples ? Sample(.985) * target[j + TargetHeadOffset - ReturnSamples] : Sample(0);
						p[j] = (desired - returning) / Sample(2.96);
					}
				}
				//p[Hammer::NumParams] = Sample(155.4 * 2. + (i - 1) * .12);
				//for (int j = 0; j < Disperser::NumParams; ++j)
				//	p[Hammer::NumParams + 1 + j] = Sample(-.52);
				//应用基频色散估计结果
				p[Hammer::NumParams] = zm + (Sample)(i - 1) * 0.15;//delay z^-m
				for (int j = 0; j < Disperser::NumParams; ++j)
					p[Hammer::NumParams + 1 + j] = ks[j];//disperser

				const int bridges = Hammer::NumParams + 1 + Disperser::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					Sample* bp = p + bridges + j * Bridge::NumParams;
					for (int k = 0; k < Bridge::NumParams; ++k) bp[k] = Sample(0);
					bp[Bridge::NumParams - 1] = (i == j) ? Sample(2.999) : Sample(-.006);//对角大，非对角小
				}
			}
			VecToParams(base);
		}

		void VecToParams(Sample* in)
		{
			//float k1 = 0.877081952428, k2 = -0.983032106409, k3 = 0.990713663258, k4 = -0.995647276579;
			//float dispersek[] = { -0.810856, -0.810637, -0.810371, -0.810073 };
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].VecToParams(in);
				in += Hammer::NumParams;
				delay[i].VecToParams(in);
				in += 1;
				disperser[i].VecToParams(in);
				//disperser[i].VecToParams(dispersek);
				in += Disperser::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					bridgeMatrix[i][j].VecToParams(in);
					in += Bridge::NumParams;
				}
				board[i].VecToParams(in);
				in += Board::NumParams;
			}
		}
		void ParamsToVec(Sample* out)
		{
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].ParamsToVec(out);
				out += Hammer::NumParams;
				delay[i].ParamsToVec(out);
				out += 1;
				disperser[i].ParamsToVec(out);
				out += Disperser::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					bridgeMatrix[i][j].ParamsToVec(out);
					out += Bridge::NumParams;
				}
				board[i].ParamsToVec(out);
				out += Board::NumParams;
			}
		}
		/*
		void SetBaseFreq(Sample freq)
		{
			Sample delayTime = SampleRate / freq;
			for (int i = 0; i < NumStrings; ++i)
				delay[i].SetDelayTime((int)delayTime - 2);
		}*/
		void Reset()
		{
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].Reset();
				delay[i].Reset();
				disperser[i].Reset();
				for (int j = 0; j < NumStrings; ++j)
					bridgeMatrix[i][j].Reset();
				board[i].Reset();
				pass[i] = 0.;
			}
		}
		void NoteOn(Sample velocity)
		{
			for (int i = 0; i < NumStrings; ++i)
				hammer[i].NoteOn(velocity);
			damp = 1.;
		}
		void NoteOff()
		{
			damp = releaseDampV;
		}
		const Sample divStrs = 1. / NumStrings;
		Sample ProcessSample()
		{
			for (int j = 0; j < NumStrings; ++j)
				pass[j] += hammer[j].ProcessSample();
			for (int j = 0; j < NumStrings; ++j)
				pass[j] = delay[j].ProcessSample(pass[j]);
			for (int j = 0; j < NumStrings; ++j)
				pass[j] = disperser[j].ProcessSample(pass[j]);
			Sample tmp[NumStrings];
			for (int j = 0; j < NumStrings; ++j)
			{
				tmp[j] = 0.;
				for (int k = 0; k < NumStrings; ++k)
					tmp[j] += bridgeMatrix[j][k].ProcessSample(pass[k]);
			}
			for (int j = 0; j < NumStrings; ++j)
				pass[j] = tmp[j] * damp * divStrs;
			Sample y = 0.;
			for (int j = 0; j < NumStrings; ++j)
				y += board[j].ProcessSample(tmp[j]);
			return y;
		}
		void ProcessBlock(Sample* out, int NumSamples)
		{
			for (int i = 0; i < NumSamples; ++i)
			{
				out[i] = ProcessSample();
			}
		}
	};

	namespace TrainSimple
	{
		inline void FFT(float* re, float* im, int N, int inv)
		{
			for (int i = 1, j = 0; i < N; i++) {
				int b = N >> 1;
				for (; j & b; b >>= 1) j ^= b;
				j ^= b;
				if (i < j) {
					float t = re[i]; re[i] = re[j]; re[j] = t;
					t = im[i]; im[i] = im[j]; im[j] = t;
				}
			}
			for (int len = 2; len <= N; len <<= 1) {
				float a = (inv ? 2.0f : -2.0f) * PI / len;
				float cr = cosf(a), ci = sinf(a);
				for (int i = 0; i < N; i += len) {
					float wr = 1, wi = 0;
					for (int j = 0; j < len / 2; j++) {
						int u = i + j, v = u + len / 2;
						float tr = wr * re[v] - wi * im[v];
						float ti = wr * im[v] + wi * re[v];
						re[v] = re[u] - tr; im[v] = im[u] - ti;
						re[u] += tr; im[u] += ti;
						float t = wr * cr - wi * ci;
						wi = wr * ci + wi * cr;
						wr = t;
					}
				}
			}
			if (inv)
				for (int i = 0; i < N; i++) {
					re[i] /= N;
					im[i] /= N;
				}
		}

		inline float LossSimple(float* params, int numParams, float* target, int numSamples, PianoWaveguide<float>* instance)
		{
			instance->VecToParams(params);
			instance->Reset();
			instance->NoteOn(1.0f);
			double mse = 0.;
			double g = 1. / numSamples;
			for (int i = 0; i < numSamples; ++i)
			{
				float y = instance->ProcessSample();
				float e = y - target[i];
				mse += e * e * g;
			}
			return (float)mse;
		}
	}

	namespace Train
	{
		inline void FFT(float* re, float* im, int N, int inv)
		{
			for (int i = 1, j = 0; i < N; i++) {
				int b = N >> 1;
				for (; j & b; b >>= 1) j ^= b;
				j ^= b;
				if (i < j) {
					float t = re[i]; re[i] = re[j]; re[j] = t;
					t = im[i]; im[i] = im[j]; im[j] = t;
				}
			}
			for (int len = 2; len <= N; len <<= 1) {
				float a = (inv ? 2.0f : -2.0f) * PI / len;
				float cr = cosf(a), ci = sinf(a);
				for (int i = 0; i < N; i += len) {
					float wr = 1, wi = 0;
					for (int j = 0; j < len / 2; j++) {
						int u = i + j, v = u + len / 2;
						float tr = wr * re[v] - wi * im[v];
						float ti = wr * im[v] + wi * re[v];
						re[v] = re[u] - tr; im[v] = im[u] - ti;
						re[u] += tr; im[u] += ti;
						float t = wr * cr - wi * ci;
						wi = wr * ci + wi * cr;
						wr = t;
					}
				}
			}
			if (inv)
				for (int i = 0; i < N; i++) {
					re[i] /= N;
					im[i] /= N;
				}
		}

		struct LossSTFTData
		{
			constexpr static int MaxFFTSize = 8192;
			std::array<float, 10000000> audio{};
			std::array<float, MaxFFTSize> yr{};
			std::array<float, MaxFFTSize> yi{};
			std::array<float, MaxFFTSize> tr{};
			std::array<float, MaxFFTSize> ti{};
			std::array<float, MaxFFTSize> window{};
		};

		inline float LossSTFT(float* params, int numParams, float* target, int numSamples, PianoWaveguide<float>* instance, LossSTFTData* data)
		{
			(void)numParams;

			if (numSamples <= 0) return 0.0f;
			if (!data || !target || !instance) return std::numeric_limits<float>::quiet_NaN();

			constexpr int MaxFFTSize = 8192;
			constexpr float MagEps = 1e-7f;
			constexpr float NormEps = 1e-7f;
			constexpr float AbsEps = 1e-4f;

			// Generate audio.
			instance->VecToParams(params);
			instance->Reset();
			instance->NoteOn(1.0f);

			for (int i = 0; i < numSamples; ++i)
				data->audio[i] = instance->ProcessSample();

			// Calculate multi-resolution STFT loss.
			auto CalcScale = [&](int windowSize) -> float
				{
					int hop = windowSize / 2;
					float* yr = data->yr.data();
					float* yi = data->yi.data();
					float* tr = data->tr.data();
					float* ti = data->ti.data();
					float* window = data->window.data();

					for (int i = 0; i < windowSize; ++i)
						window[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / windowSize);

					double l = 0.;
					for (int i = 0; i < numSamples - windowSize; i += hop)
					{
						for (int j = 0; j < windowSize; ++j)
						{
							yr[j] = data->audio[i + j] * window[j];
							yi[j] = 0.;
							tr[j] = target[i + j] * window[j];
							ti[j] = 0.;
						}
						FFT(yr, yi, windowSize, 0);
						FFT(tr, ti, windowSize, 0);
						double l1 = 0.;
						for (int j = 1; j < windowSize / 2; ++j)
						{
							float magy = std::sqrt(yr[j] * yr[j] + yi[j] * yi[j]);
							float magt = std::sqrt(tr[j] * tr[j] + ti[j] * ti[j]);
							float lmy = std::log(magy + 1e-5);
							float lmt = std::log(magt + 1e-5);
							float vl = (lmy - lmt) / (windowSize / 2);
							float v = (magy - magt) / (windowSize / 2);
							l1 += vl * vl + v * v * 10.;
						}
						l += l1 / (numSamples / hop);
					}
					return l;
				};

			//float l0 = CalcScale(512);
			float l1 = CalcScale(512);
			//float l2 = CalcScale(8192);

			double tmse = 0.;
			for (int i = 0; i < numSamples; ++i)
			{
				double a = data->audio[i];
				double t = target[i];
				double v = a - t;
				tmse += v * v / numSamples;
			}
			tmse *= 1000.;

			return l1 * 1000.;
		}
	}

	namespace TrainV2 {

		constexpr int SR = static_cast<int>(PianoDWG::SampleRate);
		constexpr double V2_PI = 3.141592653589793238462643383279502884;
		constexpr int NumPartials = 24;

		struct Spec {
			int fft = 0;
			int hop = 0;
			int frames = 0;
			int bins = 0;
			std::vector<double> mag; // frequency-major
			double largest = 0.0;
			double norm = 0.0;
			double at(int k, int t) const { return mag[std::size_t(k) * frames + t]; }
		};

		namespace Detail {
			inline void FFT(std::vector<double>& re, std::vector<double>& im)
			{
				const int n = static_cast<int>(re.size());
				for (int i = 1, j = 0; i < n; ++i) {
					int bit = n >> 1;
					for (; j & bit; bit >>= 1) j ^= bit;
					j ^= bit;
					if (i < j) {
						std::swap(re[i], re[j]);
						std::swap(im[i], im[j]);
					}
				}
				for (int size = 2; size <= n; size <<= 1) {
					const int half = size >> 1;
					for (int k = 0; k < n; k += size) {
						for (int j = 0; j < half; ++j) {
							const double a = -2.0 * V2_PI * j / size;
							const double wr = std::cos(a);
							const double wi = std::sin(a);
							const double tr = wr * re[k + j + half] - wi * im[k + j + half];
							const double ti = wr * im[k + j + half] + wi * re[k + j + half];
							re[k + j + half] = re[k + j] - tr;
							im[k + j + half] = im[k + j] - ti;
							re[k + j] += tr;
							im[k + j] += ti;
						}
					}
				}
			}

			inline int Reflect(int i, int length)
			{
				while (i < 0 || i >= length) {
					if (i < 0) i = -i;
					if (i >= length) i = 2 * length - 2 - i;
				}
				return i;
			}

			inline Spec STFT(const double* y, int length, int fftSize, int hop)
			{
				Spec s;
				s.fft = fftSize;
				s.hop = hop;
				s.bins = fftSize / 2 + 1;
				s.frames = length / hop + 1;
				s.mag.resize(std::size_t(s.bins) * s.frames);
				std::vector<double> re(fftSize), im(fftSize);
				for (int t = 0; t < s.frames; ++t) {
					const int offset = t * hop - fftSize / 2;
					for (int j = 0; j < fftSize; ++j) {
						re[j] = y[Reflect(offset + j, length)] *
							(0.5 - 0.5 * std::cos(2.0 * V2_PI * j / fftSize));
						im[j] = 0.0;
					}
					FFT(re, im);
					for (int k = 0; k < s.bins; ++k) {
						const double v = std::sqrt(re[k] * re[k] + im[k] * im[k] + 1.0e-30);
						s.mag[std::size_t(k) * s.frames + t] = v;
						s.largest = std::max(s.largest, v);
						s.norm += v * v;
					}
				}
				s.norm = std::sqrt(s.norm);
				return s;
			}

			struct Envelope {
				int partials = 0;
				int frames = 0;
				std::vector<double> env;
				std::vector<double> ripple;
				std::vector<double> mod;
			};

			inline Envelope Harmonics(const Spec& spec, const std::vector<int>& centers)
			{
				Envelope e;
				e.partials = static_cast<int>(centers.size());
				e.frames = spec.frames;
				e.env.resize(std::size_t(e.partials) * e.frames);
				for (int k = 0; k < e.partials; ++k) {
					for (int t = 0; t < e.frames; ++t) {
						double v = 1.0e-12;
						for (int j = -2; j <= 2; ++j) {
							const int ix = std::clamp(centers[k] + j, 0, spec.bins - 1);
							const double a = spec.at(ix, t);
							v += a * a;
						}
						e.env[std::size_t(k) * e.frames + t] = std::sqrt(v);
					}
				}
				return e;
			}

			inline std::vector<double> Detrend(const Envelope& e)
			{
				const int T = std::max(0, e.frames - 6);
				std::vector<double> out(std::size_t(e.partials) * T, 0.0);
				// Ripple detrending needs at least two time points.
				if (T <= 1) return out;
				for (int k = 0; k < e.partials; ++k) {
					double maxv = 0.0;
					for (int t = 3; t < e.frames - 3; ++t)
						maxv = std::max(maxv, e.env[std::size_t(k) * e.frames + t]);
					double mean = 0.0, slope = 0.0, denom = 0.0;
					for (int t = 0; t < T; ++t) {
						const double x = T > 1 ? (-1.0 + 2.0 * t / (T - 1)) : 0.0;
						const double v = e.env[std::size_t(k) * e.frames + t + 3];
						const double l = std::log(v + maxv * 0.005 + 1.0e-8);
						out[std::size_t(k) * T + t] = l;
						mean += l;
						slope += x * l;
						denom += x * x;
					}
					mean /= T;
					slope /= denom;
					for (int t = 0; t < T; ++t) {
						const double x = -1.0 + 2.0 * t / (T - 1);
						out[std::size_t(k) * T + t] -= mean + slope * x;
					}
				}
				return out;
			}

			inline std::vector<double> Modulations(const std::vector<double>& residual, int parts, int frames)
			{
				const int len = std::max(0, frames - 6);
				const int maxFreq = std::max(0, std::min(19, len / 2));
				std::vector<double> result(std::size_t(parts) * maxFreq, 0.0);
				for (int k = 0; k < parts; ++k) {
					for (int h = 1; h <= maxFreq; ++h) {
						double re = 0.0, im = 0.0;
						for (int t = 0; t < len; ++t) {
							const double x = residual[std::size_t(k) * len + t] *
								(0.5 - 0.5 * std::cos(2.0 * V2_PI * t / len));
							const double a = 2.0 * V2_PI * t * h / len;
							re += x * std::cos(a);
							im -= x * std::sin(a);
						}
						result[std::size_t(k) * maxFreq + h - 1] =
							std::sqrt(re * re + im * im + 1.0e-30) / len;
					}
				}
				return result;
			}

			inline std::vector<double> RmsEnv(const double* x, int end, int window)
			{
				const int stride = std::max(1, window / 4);
				const int n = 1 + (end - window) / stride;
				std::vector<double> result(std::max(0, n));
				for (int t = 0; t < n; ++t) {
					double v = 1.0e-12;
					for (int j = 0; j < window; ++j)
						v += x[t * stride + j] * x[t * stride + j] / window;
					result[t] = std::sqrt(v);
				}
				return result;
			}
		}

		struct FlatSpec {
			int n = 0;
			int hop = 0;
			int frames = 0;
			int bins = 0;
			const double* ref = nullptr;
			double max = 0.0;
			double norm = 0.0;
			std::size_t offset = 0;
		};

		struct Cache {
			int size = 0;
			int end = 0;
			int onset = 0;
			int parts = 0;
			int frames = 0;
			int modN = 0;
			std::vector<double> target;
			std::array<std::vector<double>, 3> refSpecs;
			std::array<std::vector<double>, 3> refShort;
			std::array<FlatSpec, 3> spec;
			std::array<FlatSpec, 3> shorts;
			FlatSpec fluxSpec;
			std::vector<int> partialBins;
			std::vector<double> refEnv;
			std::vector<double> floors;
			std::vector<double> weights;
			std::vector<double> refRipple;
			std::vector<double> refMod;
			std::vector<double> refRms22;
			std::vector<double> refRms88;
			std::vector<double> refFlux;
			std::vector<double> refEnergy;
			std::size_t workSize = 0;
			std::size_t envOffset = 0;
			std::size_t residualOffset = 0;

			void Init(const std::vector<double>& audio, const std::vector<double>& freqs, int onsetIndex)
			{
				target = audio;
				size = static_cast<int>(audio.size());
				// Same onset detector as the original v1.3 Objective.
				if (onsetIndex >= 0) {
					onset = onsetIndex;
				}
				else {
					const int scan = std::min(SR, size);
					std::vector<double> energy(scan + 1, 0.0), rms(scan, 0.0);
					for (int i = 0; i < scan; ++i)
						energy[i + 1] = energy[i] + target[i] * target[i];
					double maxRms = 0.0;
					for (int i = 0; i < scan; ++i) {
						const int lo = std::max(0, i - 44);
						const int hi = std::min(scan, i + 44);
						rms[i] = std::sqrt((energy[hi] - energy[lo]) / 88.0 + 1.0e-20);
						maxRms = std::max(maxRms, rms[i]);
					}
					int best = 0;
					bool found = false;
					for (int i = 0; i + 44 <= scan; ++i) {
						int hits = 0;
						for (int j = 0; j < 44; ++j)
							if (rms[i + j] >= 0.2 * maxRms) ++hits;
						if (hits == 44) { best = i; found = true; break; }
					}
					if (!found)
						best = static_cast<int>(std::max_element(rms.begin(), rms.end()) - rms.begin());
					onset = best;
				}
				end = std::min(size, onset + static_cast<int>(0.12 * SR));
				workSize = 0;
				partialBins.clear();
				refFlux.clear();
				refEnergy.clear();

				for (int ix = 0; ix < 3; ++ix) {
					const int n = ix == 0 ? 256 : (ix == 1 ? 1024 : 4096);
					Spec r = Detail::STFT(target.data(), size, n, n / 4);
					refSpecs[ix] = std::move(r.mag);
					spec[ix] = { n, n / 4, r.frames, r.bins, refSpecs[ix].data(), r.largest, r.norm, workSize };
					workSize += refSpecs[ix].size();
				}
				for (int ix = 0; ix < 3; ++ix) {
					const int n = ix == 0 ? 64 : (ix == 1 ? 128 : 512);
					Spec r = Detail::STFT(target.data(), end, n, n / 4);
					refShort[ix] = std::move(r.mag);
					shorts[ix] = { n, n / 4, r.frames, r.bins, refShort[ix].data(), r.largest, r.norm, workSize };
					workSize += refShort[ix].size();
				}
				Spec rf = Detail::STFT(target.data(), end, 256, 32);
				fluxSpec = { 256, 32, rf.frames, rf.bins, nullptr, rf.largest, rf.norm, workSize };
				workSize += std::size_t(rf.frames) * rf.bins;

				for (double f : freqs)
					partialBins.push_back(std::clamp(int(std::nearbyint(f * 4096.0 / SR)), 0, 2048));
				parts = static_cast<int>(partialBins.size());
				frames = spec[2].frames;
				modN = std::max(0, std::min(19, (frames - 6) / 2));

				// Reusable LossKernel scratch is owned by LossState::work, not the stack.
				// Only one partial is processed at a time, so frames/T samples suffice.
				envOffset = workSize;
				workSize += static_cast<std::size_t>(frames);
				residualOffset = workSize;
				workSize += static_cast<std::size_t>(std::max(0, frames - 6));

				Spec big;
				big.fft = 4096;
				big.hop = 1024;
				big.frames = frames;
				big.bins = 2049;
				big.mag = refSpecs[2];
				Detail::Envelope ref = Detail::Harmonics(big, partialBins);
				refEnv = ref.env;
				floors.resize(parts);
				weights.resize(parts);
				for (int k = 0; k < parts; ++k) {
					double maxv = 0.0, power = 0.0;
					for (int t = 0; t < frames; ++t) {
						const double x = refEnv[std::size_t(k) * frames + t];
						maxv = std::max(maxv, x);
						power += x * x / frames;
					}
					floors[k] = maxv * 0.01 + 1.0e-8;
					weights[k] = std::sqrt(std::sqrt(power));
				}
				const double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
				for (double& x : weights) x /= sum;

				ref.ripple = Detail::Detrend(ref);
				refRipple = ref.ripple;
				refMod = Detail::Modulations(refRipple, parts, frames);
				refRms22 = Detail::RmsEnv(target.data(), end, 22);
				refRms88 = Detail::RmsEnv(target.data(), end, 88);
				for (int k = 7; k < 94; ++k)
					for (int t = 1; t < rf.frames; ++t)
						refFlux.push_back(std::max(0.0, rf.at(k, t) - rf.at(k, t - 1)));
				double total = 0.0;
				for (int i = 0; i < end; ++i) {
					total += target[i] * target[i];
					refEnergy.push_back(total);
				}
				for (double& x : refEnergy) x /= total + 1.0e-8;
			}
		};

		struct LossState {
			Cache cache;
			std::vector<float> audio;
			std::vector<float> audioGradient;
			std::array<float, 12> components{};
			std::array<float, 12> componentsGradient{};
			std::vector<double> work;
			std::vector<double> workGradient;
			const float* preparedTarget = nullptr;
			int preparedSamples = 0;
			int preparedInputSamples = 0;
			int preparedOnsetOverride = -1;
			int onsetOverride = 0;

			void InvalidateTarget()
			{
				preparedTarget = nullptr;
				preparedSamples = 0;
				preparedInputSamples = 0;
				preparedOnsetOverride = -1;
				cache.size = 0;
			}
		};

		using LossResult = std::tuple<float, float, float, float, float, float,
			float, float, float, float, float, float>;

		inline int TrainingSamples(int numSamples)
		{
			// Train on the complete supplied target. This helper remains so all
			// call sites use one consistent length policy.
			return numSamples;
		}

		inline void PrepareTarget(float* target, int numSamples, LossState& data,
			const char* partialFile = nullptr)
		{
			const int n = TrainingSamples(numSamples);
			if (!target || n < 4096)
				throw std::runtime_error("TrainV2 requires at least 4096 samples at 22050 Hz");
			if (!partialFile && data.preparedTarget == target && data.preparedSamples == n &&
				data.preparedInputSamples == numSamples &&
				data.preparedOnsetOverride == data.onsetOverride)
				return;

			std::vector<double> reference(target, target + n);
			std::vector<double> freqs;
			if (partialFile && *partialFile) {
				std::ifstream file(partialFile);
				if (!file) throw std::runtime_error("Cannot open TrainV2 harmonic partial file");
				double x;
				while (file >> x) freqs.push_back(x);
				if (freqs.empty()) throw std::runtime_error("TrainV2 harmonic partial file is empty");
			}
			else {
				float identified[NumPartials]{};
				// Reuse the original PianoDWG estimator. This runs once per target, outside AD.
				EstimationDelayK(target, numSamples, identified, NumPartials, PianoDWG::SampleRate);
				for (int i = 0; i < NumPartials; ++i) {
					if (!std::isfinite(identified[i]) || identified[i] <= 0.0f ||
						identified[i] >= PianoDWG::SampleRate * 0.5f)
						throw std::runtime_error("TrainV2: EstimationDelayK failed to recognize 24 partials");
					if (i && identified[i] <= identified[i - 1])
						throw std::runtime_error("TrainV2: EstimationDelayK partials are not increasing");
					freqs.push_back(identified[i]);
				}
			}

			data.cache.Init(reference, freqs, data.onsetOverride);
			data.audio.resize(n);
			data.audioGradient.resize(n);
			data.work.resize(data.cache.workSize);
			data.workGradient.resize(data.cache.workSize);
			data.preparedTarget = target;
			data.preparedSamples = n;
			data.preparedInputSamples = numSamples;
			data.preparedOnsetOverride = data.onsetOverride;
		}

		// Keep the FFT transform as a separate Enzyme-visible primitive, matching
		// the original flat loss kernel's compilation boundary.
#if defined(_MSC_VER)
#define PIANODWG_V2_NOINLINE __declspec(noinline)
#else
#define PIANODWG_V2_NOINLINE __attribute__((noinline))
#endif
		static PIANODWG_V2_NOINLINE void STFTAudio(const float* audio, double* mem, const FlatSpec& cfg, int length)
		{
			const int n = cfg.n;
			double re[4096], im[4096];
			for (int t = 0; t < cfg.frames; ++t) {
				const int offset = t * cfg.hop - n / 2;
				for (int j = 0; j < n; ++j) {
					re[j] = double(audio[Detail::Reflect(offset + j, length)]) *
						(0.5 - 0.5 * std::cos(2.0 * V2_PI * j / n));
					im[j] = 0.0;
				}
				for (int i = 1, j = 0; i < n; ++i) {
					int bit = n >> 1;
					for (; j & bit; bit >>= 1) j ^= bit;
					j ^= bit;
					if (i < j) {
						double tmp = re[i]; re[i] = re[j]; re[j] = tmp;
						tmp = im[i]; im[i] = im[j]; im[j] = tmp;
					}
				}
				for (int step = 2; step <= n; step <<= 1) {
					const int half = step / 2;
					const double a = -2.0 * V2_PI / step;
					const double baseR = std::cos(a), baseI = std::sin(a);
					for (int k = 0; k < n; k += step) {
						double wr = 1.0, wi = 0.0;
						for (int j = 0; j < half; ++j) {
							const double tr = wr * re[k + j + half] - wi * im[k + j + half];
							const double ti = wr * im[k + j + half] + wi * re[k + j + half];
							re[k + j + half] = re[k + j] - tr;
							im[k + j + half] = im[k + j] - ti;
							re[k + j] += tr;
							im[k + j] += ti;
							const double nextR = wr * baseR - wi * baseI;
							wi = wr * baseI + wi * baseR;
							wr = nextR;
						}
					}
				}
				for (int k = 0; k < cfg.bins; ++k)
					mem[std::size_t(k) * cfg.frames + t] =
					std::sqrt(re[k] * re[k] + im[k] * im[k] + 1.0e-30);
			}
		}

		// Sole v1.3 loss math kernel. Loss() below calls this for both ordinary
		// evaluation and the Enzyme loss-to-audio pass, so there is no second objective.
		extern "C" PIANODWG_V2_NOINLINE inline void LossKernel(
			const float* audio, float* components, double* work, Cache* cache)
		{
			const Cache& c = *cache;

			// 1) Multi-resolution spectral convergence and log magnitude.
			double sc = 0.0, logMagnitude = 0.0;
			for (int ix = 0; ix < 3; ++ix)
				STFTAudio(audio, work + c.spec[ix].offset, c.spec[ix], c.size);
			for (int ix = 0; ix < 3; ++ix) {
				const FlatSpec& s = c.spec[ix];
				const double* a = work + s.offset;
				const double* ref = s.ref;
				double delta = 0.0, logError = 0.0;
				for (int i = 0; i < s.frames * s.bins; ++i) {
					const double d = a[i] - ref[i];
					delta += d * d;
					logError += std::fabs(std::log(a[i] + s.max * 1.0e-4) -
						std::log(ref[i] + s.max * 1.0e-4));
				}
				sc += std::sqrt(delta) / (s.norm + 1.0e-8) / 3.0;
				logMagnitude += logError / (s.frames * s.bins) / 3.0;
			}

			// 2-4) Harmonic envelope, detrended ripple, and modulation spectrum.
			const FlatSpec& harmonicSpec = c.spec[2];
			const double* harmonicMag = work + harmonicSpec.offset;
			const int T = std::max(0, c.frames - 6);
			double harmonicEnvelope = 0.0, ripple = 0.0, modulation = 0.0;
			double* const env = work + c.envOffset;
			double* const residual = work + c.residualOffset;
			for (int k = 0; k < c.parts; ++k) {
				double envError = 0.0;
				const int center = c.partialBins[k];
				for (int t = 0; t < c.frames; ++t) {
					double v = 1.0e-12;
					for (int j = -2; j <= 2; ++j) {
						const int ix = std::clamp(center + j, 0, harmonicSpec.bins - 1);
						const double z = harmonicMag[std::size_t(ix) * harmonicSpec.frames + t];
						v += z * z;
					}
					env[t] = std::sqrt(v);
					envError += std::fabs(std::log(env[t] + c.floors[k]) -
						std::log(c.refEnv[k * c.frames + t] + c.floors[k]));
				}
				harmonicEnvelope += c.weights[k] * envError / c.frames;
				// The original ripple/modulation definitions need at least two
				// detrended frames. For shorter valid targets, keep these terms at 0.
				if (T > 1) {
					double maxv = 0.0;
					for (int t = 3; t < c.frames - 3; ++t) maxv = std::max(maxv, env[t]);
					double mean = 0.0, slope = 0.0, denom = 0.0;
					for (int t = 0; t < T; ++t) {
						const double x = -1.0 + 2.0 * t / (T - 1);
						const double v = std::log(env[t + 3] + maxv * 0.005 + 1.0e-8);
						residual[t] = v;
						mean += v;
						slope += x * v;
						denom += x * x;
					}
					mean /= T;
					slope /= denom;
					double rippleError = 0.0;
					for (int t = 0; t < T; ++t) {
						const double x = -1.0 + 2.0 * t / (T - 1);
						residual[t] -= mean + slope * x;
						rippleError += std::fabs(residual[t] - c.refRipple[k * T + t]);
					}
					ripple += c.weights[k] * rippleError / T;

					double modulationError = 0.0;
					for (int h = 1; h <= c.modN; ++h) {
						double re = 0.0, im = 0.0;
						for (int t = 0; t < T; ++t) {
							const double x = residual[t] * (0.5 - 0.5 * std::cos(2.0 * V2_PI * t / T));
							const double a = 2.0 * V2_PI * t * h / T;
							re += x * std::cos(a);
							im -= x * std::sin(a);
						}
						const double mag = std::sqrt(re * re + im * im + 1.0e-30) / T;
						modulationError += std::fabs(mag - c.refMod[k * c.modN + h - 1]);
					}
					if (c.modN > 0) modulation += c.weights[k] * modulationError / c.modN;
				}
			}

			// 5) Attack multi-band spectral convergence.
			for (int ix = 0; ix < 3; ++ix)
				STFTAudio(audio, work + c.shorts[ix].offset, c.shorts[ix], c.end);
			int attackBandCount = 0;
			double attackBandSC = 0.0;
			for (int ix = 0; ix < 3; ++ix) {
				const FlatSpec& s = c.shorts[ix];
				const double* a = work + s.offset;
				const double* ref = s.ref;
				for (int band = 0; band < 3; ++band) {
					const int fmin = band == 0 ? 0 : (band == 1 ? 600 : 2500);
					const int fmax = band == 0 ? 600 : (band == 1 ? 2500 : 8000);
					const int lo = static_cast<int>(std::ceil(double(fmin) * s.n / SR));
					const int hi = std::min(s.bins, static_cast<int>(std::ceil(double(fmax) * s.n / SR)));
					if (lo >= hi) continue;
					double ea = 0.0, eb = 0.0, et = 0.0;
					for (int k = lo; k < hi; ++k) {
						for (int t = 0; t < s.frames; ++t) {
							const double aa = a[std::size_t(k) * s.frames + t];
							const double rr = ref[std::size_t(k) * s.frames + t];
							ea += (aa - rr) * (aa - rr);
							eb += rr * rr;
						}
					}
					for (int i = 0; i < s.frames * s.bins; ++i) et += ref[i] * ref[i];
					attackBandSC += std::sqrt(ea) /
						(std::sqrt(eb) + 0.03 * std::sqrt(et) + 1.0e-8);
					++attackBandCount;
				}
			}
			attackBandSC /= attackBandCount;

			// 6) Attack waveform, same 15/40/80 ms windows and same origin convention.
			double attackWave = 0.0;
			for (int ms : { 15, 40, 80 }) {
				const int len = std::min(c.end, c.onset + int(ms * 0.001 * SR));
				double err = 0.0, refEnergy = 0.0;
				for (int i = 0; i < len; ++i) {
					const double d = double(audio[i]) - c.target[i];
					err += d * d;
					refEnergy += c.target[i] * c.target[i];
				}
				attackWave += std::sqrt(err) / (std::sqrt(refEnergy) + 1.0e-8) / 3.0;
			}

			// 7 and 10) Short RMS envelope and positive envelope slope.
			double shortRms = 0.0, positiveSlope = 0.0;
			for (int wi = 0; wi < 2; ++wi) {
				const int window = wi == 0 ? 22 : 88;
				const int stride = std::max(1, window / 4);
				const int count = 1 + (c.end - window) / stride;
				const double* ref = wi == 0 ? c.refRms22.data() : c.refRms88.data();
				double err = 0.0, den = 0.0, up = 0.0, down = 0.0, prev = 0.0;
				for (int t = 0; t < count; ++t) {
					double v = 1.0e-12;
					for (int j = 0; j < window; ++j) {
						const double z = audio[t * stride + j];
						v += z * z / window;
					}
					const double x = std::sqrt(v);
					err += std::fabs(x - ref[t]);
					den += ref[t];
					if (t) {
						up += std::fabs(std::max(0.0, x - prev) - std::max(0.0, ref[t] - ref[t - 1]));
						down += std::max(0.0, ref[t] - ref[t - 1]);
					}
					prev = x;
				}
				shortRms += 0.5 * (err / count) / (den / count + 1.0e-8);
				positiveSlope += 0.5 * up / (down + 1.0e-8);
			}

			// 8) High-band spectral flux.
			STFTAudio(audio, work + c.fluxSpec.offset, c.fluxSpec, c.end);
			const double* fluxMag = work + c.fluxSpec.offset;
			double fluxDiff = 0.0, fluxSum = 0.0;
			std::size_t fluxIndex = 0;
			for (int k = 7; k < 94; ++k) {
				for (int t = 1; t < c.fluxSpec.frames; ++t) {
					const double x = std::max(0.0,
						fluxMag[std::size_t(k) * c.fluxSpec.frames + t] -
						fluxMag[std::size_t(k) * c.fluxSpec.frames + t - 1]);
					fluxDiff += std::fabs(x - c.refFlux[fluxIndex]);
					fluxSum += std::fabs(c.refFlux[fluxIndex]);
					++fluxIndex;
				}
			}
			const double highBandFlux = fluxDiff / (fluxSum + 1.0e-8);

			// 9) Cumulative energy timing.
			double totalEnergy = 0.0;
			for (int i = 0; i < c.end; ++i) totalEnergy += double(audio[i]) * audio[i];
			double cumulative = 0.0, energyTiming = 0.0;
			for (int i = 0; i < c.end; ++i) {
				cumulative += double(audio[i]) * audio[i];
				energyTiming += std::fabs(cumulative / (totalEnergy + 1.0e-8) - c.refEnergy[i]);
			}
			energyTiming /= c.end;

			const double total = sc + 0.1 * logMagnitude + 0.2 * harmonicEnvelope +
				0.25 * ripple + 0.3 * modulation + 0.6 * attackBandSC +
				0.7 * attackWave + 0.4 * shortRms + 0.25 * highBandFlux +
				0.2 * positiveSlope + 0.1 * energyTiming;

			components[0] = float(total);
			components[1] = float(sc);
			components[2] = float(logMagnitude);
			components[3] = float(harmonicEnvelope);
			components[4] = float(ripple);
			components[5] = float(modulation);
			components[6] = float(attackBandSC);
			components[7] = float(attackWave);
			components[8] = float(shortRms);
			components[9] = float(highBandFlux);
			components[10] = float(positiveSlope);
			components[11] = float(energyTiming);
		}

#undef PIANODWG_V2_NOINLINE

		inline LossResult Loss(float* params, int numParams, float* target, int numSamples,
			PianoWaveguide<float>& instance, LossState& data)
		{
			if (numParams != PianoWaveguide<float>::NumParams)
				throw std::runtime_error("TrainV2 requires original PianoDWG parameter count: 1959");
			PrepareTarget(target, numSamples, data);
			const int n = TrainingSamples(numSamples);

			// Use the caller-owned persistent waveguide instance, exactly like original loss().
			instance.VecToParams(params);
			instance.Reset();
			instance.NoteOn(1.0f);
			instance.ProcessBlock(data.audio.data(), n);

			LossKernel(data.audio.data(), data.components.data(), data.work.data(), &data.cache);
			return { data.components[0], data.components[1], data.components[2], data.components[3],
					 data.components[4], data.components[5], data.components[6], data.components[7],
					 data.components[8], data.components[9], data.components[10], data.components[11] };
		}

	} // namespace TrainV2

}