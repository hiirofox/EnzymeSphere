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
		Sample ProcessSampleLinear(Sample x)
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
		Sample ProcessSample(Sample x)
		{
			buffer[writePos] = x;
			if (validSamples < Capacity) ++validSamples;
			const int integerDelay = static_cast<int>(delayTime);
			const Sample t = delayTime - Sample(integerDelay);
			const Sample t2 = t * t;
			const Sample t3 = t2 * t;
			const Sample p0 = ReadTap(integerDelay - 1);
			const Sample p1 = ReadTap(integerDelay);
			const Sample p2 = ReadTap(integerDelay + 1);
			const Sample p3 = ReadTap(integerDelay + 2);
			const Sample m1 = Sample(0.5) * (p2 - p0);
			const Sample m2 = Sample(0.5) * (p3 - p1);
			const Sample y = (Sample(2) * t3 - Sample(3) * t2 + Sample(1)) * p1 + (t3 - Sample(2) * t2 + t) * m1 + (-Sample(2) * t3 + Sample(3) * t2) * p2 + (t3 - t2) * m2;
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
		constexpr static int TableSize = 1024;
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
		//using OutputDist = NLCells2<Sample>;

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
			//static_assert(Hammer::TableSize == 512, "Keep the original 512-sample Hammer");

			//泛音基频色散估计
			int overtoneFreqTableLen = 25;
			float overtoneFreqTable[25];
			EstimationDelayK((float*)target, numSamples, overtoneFreqTable, overtoneFreqTableLen, (float)SampleRate);
			auto [zm, k0, k1, k2, k3] = mkEstim(overtoneFreqTable, overtoneFreqTableLen);
			float ks[] = { k0, k1, k2, k3 };
			printf("Waveguide DelayLength:%.5f\n", zm);
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
				p[Hammer::NumParams] = zm + (Sample)(i - 1) * 0.045;//delay z^-m
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

			//Sample y = 0.;
			//for (int j = 0; j < NumStrings; ++j)
			//	y += board[j].ProcessSample(pass[j]);

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

#include "namespaceTrainV2.h"
}