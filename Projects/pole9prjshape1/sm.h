#pragma once
#include <array>
#include <random>

namespace sm
{
	class FM
	{
	public:
		constexpr static int NumOPs = 6;
		static constexpr int GetNumParams()
		{
			return NumOPs + NumOPs * (NumOPs - 1) / 2;
		}
		void Params2Vec(float* out)
		{
			int idx = 0;
			for (int i = 0; i < NumOPs; ++i)out[idx++] = freqs[i];
			for (int j = 0; j < NumOPs; ++j)for (int k = j + 1; k < NumOPs; ++k)out[idx++] = modmat[j][k] / 8.;
		}
		void Vec2Params(float* in)
		{
			int idx = 0;
			for (int i = 0; i < NumOPs; ++i)freqs[i] = in[idx++];
			for (int j = 0; j < NumOPs; ++j)for (int k = 0; k < NumOPs; ++k)modmat[j][k] = 0.0f;
			for (int j = 0; j < NumOPs; ++j)for (int k = j + 1; k < NumOPs; ++k)modmat[j][k] = in[idx++] * 8.;
		}
	private:
		float freqs[NumOPs];//归一化频率，仅实验。
		float modmat[NumOPs][NumOPs];//只有一半有用，另一半是空数据。
		double t = 0.;
	public:
		void Reset(double t = 0.)
		{
			this->t = t;
		}
		void ProcessBlock(float* out, int NumSamples)
		{
			for (int i = 0; i < NumSamples; ++i)
			{
				float state[NumOPs];
				for (int j = 0; j < NumOPs; ++j) state[j] = 0.;
				float v = 0;
				for (int j = 0; j < NumOPs; ++j)
				{
					v = std::sin(t * freqs[j] + state[j]);
					for (int k = j + 1; k < NumOPs; ++k) state[k] += v * modmat[j][k];
				}
				out[i] = v;
				t += 3.14159265358979 * 2.;
			}
		}
	};
	class MLP
	{
	public:
		constexpr static int NumInParams = 2;
		constexpr static int NumHids = 3;
		constexpr static int NumHidParams = 50;
		constexpr static int NumOutParams = FM::GetNumParams();
		static constexpr int GetNumParams()
		{
			return NumHidParams * NumInParams
				+ NumHids * NumHidParams * NumHidParams
				+ NumOutParams * NumHidParams
				+ (NumHids + 1) * NumHidParams
				+ NumHids * NumHidParams
				+ NumOutParams;
		}
		void InitParams()
		{
			static std::mt19937 rng{ std::random_device{}() };
			auto init2D = [](auto& mat, float limit)
				{
					std::uniform_real_distribution<float> dist(-limit, limit);
					for (auto& row : mat)
						for (auto& v : row)
							v = dist(rng);
				};
			// 输入层：2 -> 50
			const float limPi = std::sqrt(6.0f / (NumInParams + NumHidParams));
			init2D(pi, limPi);
			// 隐藏层：50 -> 50
			const float limPh = std::sqrt(6.0f / (NumHidParams + NumHidParams));
			for (auto& layer : ph)init2D(layer, limPh);
			// 输出层：50 -> 15
			const float limPo = std::sqrt(6.0f / (NumHidParams + NumOutParams));
			init2D(po, limPo);
			// 偏置初始化为 0
			for (auto& row : bias)row.fill(0.0f);
			for (auto& v : biasout)v = 0.0f;
			// 残差系数初始化为 1
			for (auto& row : res)row.fill(0.0f);

		}
		void Params2Vec(float* out)
		{
			for (const auto& row : pi)for (float v : row)*out++ = v;
			for (const auto& layer : ph)for (const auto& row : layer)for (float v : row)*out++ = v;
			for (const auto& row : po)for (float v : row)*out++ = v;
			for (const auto& row : bias)for (float v : row)*out++ = v;
			for (const float v : biasout)*out++ = v;
			for (const auto& row : res)for (float v : row)*out++ = v;
		}
		void Vec2Params(float* in)
		{
			for (auto& row : pi)for (auto& v : row)v = *in++;
			for (auto& layer : ph)for (auto& row : layer)for (auto& v : row)v = *in++;
			for (auto& row : po)for (auto& v : row)v = *in++;
			for (auto& row : bias)for (auto& v : row)v = *in++;
			for (auto& v : biasout)v = *in++;
			for (auto& row : res)for (auto& v : row)v = *in++;
		}

	private:
		std::array<std::array<float, NumInParams>, NumHidParams> pi;
		std::array<std::array<std::array<float, NumHidParams>, NumHidParams>, NumHids> ph;
		std::array<std::array<float, NumHidParams>, NumOutParams> po;
		std::array<std::array<float, NumHidParams>, NumHids + 1> bias;
		std::array<float, NumOutParams> biasout;
		std::array<std::array<float, NumHidParams>, NumHids> res;

		std::array<std::array<float, NumHidParams>, NumHids + 1> state;

		template <int w, int h>
		void mxv(const std::array<std::array<float, w>, h>& mat, const std::array<float, w>& x, std::array<float, h>& y)
		{
			for (int i = 0; i < h; ++i)
			{
				float v = 0.;
				for (int j = 0; j < w; ++j)
					v += mat[i][j] * x[j];
				y[i] = v;
			}
		}
		template<int N>
		void ea(std::array<float, N>& x, const std::array<float, N>& a)
		{
			for (int i = 0; i < N; ++i) x[i] += a[i];
		}
		template<int N>
		void ema(std::array<float, N>& x, const std::array<float, N>& a, const std::array<float, N>& b)
		{
			for (int i = 0; i < N; ++i) x[i] += a[i] * b[i];
		}
		template<int N>
		void nl(std::array<float, N>& x)
		{
			for (int i = 0; i < N; ++i) x[i] = std::tanhf(x[i]);
		}
	public:
		std::array<float, NumInParams> in;
		std::array<float, NumOutParams> out;
		void Process(float* pin, float* pout)
		{
			std::copy_n(pin, NumInParams, in.begin());

			mxv<NumInParams, NumHidParams>(pi, in, state[0]);
			ea<NumHidParams>(state[0], bias[0]);
			nl<NumHidParams>(state[0]);
			for (int i = 1; i <= NumHids; ++i)
			{
				mxv<NumHidParams, NumHidParams>(ph[i - 1], state[i - 1], state[i]);
				ea<NumHidParams>(state[i], bias[i]);
				nl<NumHidParams>(state[i]);
				ema<NumHidParams>(state[i], state[i - 1], res[i - 1]);
			}
			mxv<NumHidParams, NumOutParams>(po, state[NumHids], out);
			ea<NumOutParams>(out, biasout);
			nl<NumOutParams>(out);

			std::copy_n(out.begin(), NumOutParams, pout);
		}
	};

	class SynthModel
	{
	private:
		FM fm;
		MLP mlp;
		float fmparams[FM::GetNumParams()];
	public:
		static constexpr int GetNumParams()
		{
			return MLP::GetNumParams();
		}
		void InitModel(float* out)
		{
			mlp.InitParams();
			mlp.Params2Vec(out);
		}
		void LoadModel(float* in)
		{
			mlp.Vec2Params(in);
		}
		void SaveModel(float* out)
		{
			mlp.Params2Vec(out);
		}
		void LatentSample(float x, float y)
		{
			float latent[2] = { x,y };
			mlp.Process(latent, fmparams);
			fm.Vec2Params(fmparams);
		}
		void Reset(double t) { fm.Reset(t); }
		void ProcessBlock(float* out, int NumSamples)
		{
			fm.ProcessBlock(out, NumSamples);
		}

	public://train
		void FFT(float* re, float* im, int N, int inv)
		{
			const float PI = 3.14159265358979323846f;
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

		std::mt19937 rng{ std::random_device{}() };
		std::uniform_real_distribution<float> dist{ -1., 1. };
		float randf()
		{
			return dist(rng);
		}

		constexpr static int NumBatchesSmoooth = 10;
		constexpr static int NumBatchesRand = 10;

		constexpr static int BatchSize = 2048;
		float tre1[BatchSize];
		float tim1[BatchSize];
		float tre2[BatchSize];
		float tim2[BatchSize];
		float tre3[BatchSize];
		float tim3[BatchSize];

		float mean[BatchSize / 2];
		float M2[BatchSize / 2];
		float count[BatchSize / 2];
		float Loss()
		{
			float totsmoothloss = 0;
			//邻域内采样
			for (int n = 0; n < NumBatchesSmoooth; ++n)
			{
				float lx = randf(), ly = randf();
				float epsx = randf(), epsy = randf();
				float r = std::sqrtf(epsx * epsx + epsy * epsy);
				epsx = epsx / r * 0.01;
				epsy = epsy / r * 0.01;
				float time = randf() * 1000.;
				LatentSample(lx - epsx, ly - epsx);
				Reset(time);
				fm.ProcessBlock(tre1, BatchSize);
				LatentSample(lx, ly);
				Reset(time);
				fm.ProcessBlock(tre2, BatchSize);
				LatentSample(lx + epsx, ly + epsx);
				Reset(time);
				fm.ProcessBlock(tre3, BatchSize);
				FFT(tre1, tim1, BatchSize, false);
				FFT(tre2, tim2, BatchSize, false);
				FFT(tre3, tim3, BatchSize, false);
				float smoothloss = 0.;
				for (int i = 0; i < BatchSize / 2; ++i)
				{
					float mag1 = std::sqrtf(tre1[i] * tre1[i] + tim1[i] * tim1[i]);
					float mag2 = std::sqrtf(tre2[i] * tre2[i] + tim2[i] * tim2[i]);
					float mag3 = std::sqrtf(tre3[i] * tre3[i] + tim3[i] * tim3[i]);
					float d = (mag1 + mag3 - mag2 * 2.) / 0.0001;
					smoothloss += d * d;
				}
				smoothloss /= BatchSize / 2;
				totsmoothloss += smoothloss;
			}
			totsmoothloss /= NumBatchesSmoooth;
			//随机采样
			float totvar = 0.;
			for (int i = 0; i < BatchSize / 2; ++i)
			{
				mean[i] = 0.;
				M2[i] = 0.;
				count[i] = 0.;
			}
			for (int n = 0; n < NumBatchesRand; ++n)
			{
				LatentSample(randf(), randf());
				Reset(randf() * 1000.);
				fm.ProcessBlock(tre1, BatchSize);
				FFT(tre1, tim1, BatchSize, false);
				for (int i = 0; i < BatchSize / 2; ++i)
				{
					float mag1 = std::sqrtf(tre1[i] * tre1[i] + tim1[i] * tim1[i]);
					float delta = mag1 - mean[i];
					count[i] += 1.f;
					mean[i] += delta / count[i];
					M2[i] += delta * (mag1 - mean[i]);
				}
			}
			for (int i = 0; i < BatchSize / 2; ++i)
			{
				float var = M2[i] / count[i];
				totvar += var;
			}
			totvar /= BatchSize / 2;

			return totsmoothloss + totvar;
		}
	};
}