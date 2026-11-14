#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace PianoDWG
{
	constexpr float SampleRate = 22050.f;

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
			for (int i = 0; i < NumLayers; ++i)k[i] = std::rand() / RAND_MAX * (std::rand() % 2 ? 1 : -1);
			if (out) ParamsToVec(out);
		}
		void VecToParams(Sample* in)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				k[i] = std::tanh(in[pos++]);
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				out[pos++] = std::atanh(k[i]);
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
			{//k[i] = std::tanh(in[pos++]);
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
				//out[pos++] = std::atanh(k[i]);
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
				k[i] = std::tanh(in[pos++]);
			for (int i = 0; i < NumLayers + 1; ++i)
				g[i] = in[pos++];
		}
		void ParamsToVec(Sample* out)
		{
			int pos = 0;
			for (int i = 0; i < NumLayers; ++i)
				out[pos++] = std::atanh(k[i]);
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
			Sample absx = std::fabs(x);
			Sample x2 = absx * absx;
			Sample x3 = x2 * absx;
			Sample num = b1[i] * absx + b2[i] * x2 + b3[i] * x3;
			Sample den = std::fabs(a1[i] * absx + a2[i] * x2 + a3[i] * x3) + 1.;
			return std::copysign(num / den, x);
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
		// Keep the two older Hermite taps available at the upper delay limit.
		static constexpr int Capacity = MaxDelayTime + 3;
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
		void InitParams(Sample* out)
		{
			SetDelayTime(Sample(300));
			if (out) ParamsToVec(out);
		}
		void ParamsToVec(Sample* out)
		{
			out[0] = delayTime;
		}
		void VecToParams(Sample* in)
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
			const Sample p1 = ReadTap(integerDelay);
			const Sample p2 = ReadTap(integerDelay + 1);
			const Sample p3 = ReadTap(integerDelay + 2);
			// Extrapolate the unavailable future tap for delays in [0, 1).
			const Sample p0 = integerDelay > 0 ? ReadTap(integerDelay - 1) : Sample(2) * p1 - p2;
			const Sample m1 = Sample(0.5) * (p2 - p0);
			const Sample m2 = Sample(0.5) * (p3 - p1);
			const Sample y = (Sample(2) * t3 - Sample(3) * t2 + Sample(1)) * p1
				+ (t3 - Sample(2) * t2 + t) * m1
				+ (-Sample(2) * t3 + Sample(3) * t2) * p2 + (t3 - t2) * m2;
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
			for (int i = 0; i < TableSize; ++i) hammer[i] = (float)std::rand() / RAND_MAX * (std::rand() % 2 ? 1 : -1) / TableSize;
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
	class PianoWaveguide1
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
		PianoWaveguide1()
		{
			//SetBaseFreq(261.63);//C4
			Reset();
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


	template<typename Sample, int MaxDelayTime>
	class DelayLineDWG2
	{
	private:
		Sample buffer[MaxDelayTime + 10];
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
			volatile int& resetPos = pos;
			resetPos = 0;
		}
	};
	template<typename Sample, int TableSize >
	class HammerModelDWG2
	{
	public:
		constexpr static int NumParams = TableSize;
		Sample hammer[TableSize] = {};
		int pos = 0;
		Sample g = Sample(0);
	public:
		void InitParams(Sample* out)
		{
			for (int i = 0; i < TableSize; ++i) hammer[i] = (float)std::rand() / RAND_MAX * (std::rand() % 2 ? 1 : -1) / TableSize;
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


	template <typename Sample>
	class PianoWaveguide2
	{
	public:
		using Hammer = HammerModelDWG2<Sample, 2048>;
		using Delay = DelayLine2<Sample, 4096>;
		using StringIIR = LatticeIIR<Sample, 8>;
		using MatrixIIR = LatticeIIR<Sample, 4>;
		//using OutputProc = NLCells2<Sample>;
		using OutputProc = LatticeIIR<Sample, 4>;

		constexpr static int NumStrings = 3;
		Hammer hammer[NumStrings];
		Delay delay[NumStrings];
		StringIIR striir[NumStrings];
		MatrixIIR matiir[NumStrings][NumStrings];
		Sample gOutL[NumStrings];
		Sample gOutR[NumStrings];
		OutputProc nll, nlr;

		Sample pass[NumStrings];
		Sample dampV = 1.0, releaseDampV = 0.985;
	public:
		static constexpr int NumParams = NumStrings * (Hammer::NumParams + Delay::NumParams +
			StringIIR::NumParams + NumStrings * MatrixIIR::NumParams + 2) + 2 * OutputProc::NumParams;

		void VecToParams(Sample* in)
		{
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].VecToParams(in); in += Hammer::NumParams;
				delay[i].VecToParams(in); in += Delay::NumParams;
				striir[i].VecToParams(in); in += StringIIR::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					matiir[i][j].VecToParams(in);
					in += MatrixIIR::NumParams;
				}
				gOutL[i] = *in++;
				gOutR[i] = *in++;
			}
			nll.VecToParams(in);
			nlr.VecToParams(in + OutputProc::NumParams);
		}

		void ParamsToVec(Sample* out)
		{
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].ParamsToVec(out); out += Hammer::NumParams;
				delay[i].ParamsToVec(out); out += Delay::NumParams;
				striir[i].ParamsToVec(out); out += StringIIR::NumParams;
				for (int j = 0; j < NumStrings; ++j)
				{
					matiir[i][j].ParamsToVec(out);
					out += MatrixIIR::NumParams;
				}
				*out++ = gOutL[i];
				*out++ = gOutR[i];
			}
			nll.ParamsToVec(out);
			nlr.ParamsToVec(out + OutputProc::NumParams);
		}

		void Reset()
		{
			for (int i = 0; i < NumStrings; ++i)
			{
				hammer[i].Reset();
				delay[i].Reset();
				striir[i].Reset();
				for (int j = 0; j < NumStrings; ++j)
					matiir[i][j].Reset();
				pass[i] = Sample(0);
			}
			nll.Reset();
			nlr.Reset();
			dampV = Sample(1);
		}

		void NoteOn(Sample velocity)
		{
			dampV = Sample(1);
			for (auto& excitation : hammer)
				excitation.NoteOn(velocity);
		}

		void NoteOff()
		{
			dampV = releaseDampV;
		}
		inline std::tuple<Sample, Sample> ProcessSample()
		{
			for (int i = 0; i < NumStrings; ++i)
				pass[i] += hammer[i].ProcessSample();
			for (int i = 0; i < NumStrings; ++i)
				pass[i] = delay[i].ProcessSample(pass[i]);
			for (int i = 0; i < NumStrings; ++i)
				pass[i] = striir[i].ProcessSample(pass[i]);
			Sample tmp[NumStrings];
			for (int i = 0; i < NumStrings; ++i)
			{
				Sample v = 0.;
				for (int j = 0; j < NumStrings; ++j)
					v += matiir[i][j].ProcessSample(pass[j]);
				tmp[i] = v;
			}
			for (int i = 0; i < NumStrings; ++i)
				pass[i] = tmp[i] * dampV;
			Sample l = 0., r = 0.;
			for (int i = 0; i < NumStrings; ++i)
			{
				l += tmp[i] * gOutL[i];
				r += tmp[i] * gOutR[i];
			}
			l = nll.ProcessSample(l);
			r = nlr.ProcessSample(r);
			return { l,r };
		}
		void ProcessBlock(Sample* outl, Sample* outr, int NumSamples)
		{
			for (int i = 0; i < NumSamples; ++i)
			{
				auto [l, r] = ProcessSample();
				outl[i] = l;
				outr[i] = r;
			}
		}
	};
}
