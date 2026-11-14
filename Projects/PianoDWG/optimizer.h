#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <limits>
#include <utility>

#include <ensmallen.hpp>

template<typename Sample>
class AdamW
{
private:
	Sample b1 = .9, b2 = .999;
	Sample lr = .001, lambda = .01;
	Sample s1 = 1., s2 = 1.;
	Sample lastloss = 0., smoothdloss = 0.;
	std::vector<Sample> m, v;
public:
	AdamW(Sample lr = .001, Sample lambda = .01, Sample b1 = .9, Sample b2 = .99) :lr(lr), lambda(lambda), b1(b1), b2(b2)
	{
		m.resize(100, 0.);
		v.resize(100, 0.);
		s1 = s2 = 1.;
		lastloss = 0.;
		smoothdloss = 0.;
	}
	Sample Update(Sample* params, Sample* grads, int numParams, Sample loss)
	{
		if (m.size() < numParams || v.size() < numParams)
		{
			m.resize(numParams, 0.);
			v.resize(numParams, 0.);
		}
		s1 *= b1;
		s2 *= b2;
		for (int i = 0; i < numParams; ++i)
		{
			m[i] = b1 * m[i] + (1. - b1) * grads[i];
			v[i] = b2 * v[i] + (1. - b2) * grads[i] * grads[i];
			Sample mhat = m[i] / (1. - s1);
			Sample vhat = v[i] / (1. - s2);
			params[i] = (1. - lr * lambda) * params[i] - lr * mhat / (sqrt(vhat) + 1.e-8);
		}


		//adaptive loss
		if (loss < lastloss)lr *= 1.001;
		else lr *= 0.99;
		lastloss = loss;
		return lr;
	}
};

// Adam used by TrainV2. All derivatives come from ClangEnzyme; this class
// only consumes a gradient vector and performs parameter updates.
template<typename Sample>
class Adam
{
	std::vector<Sample> m_, v_, bp;
	int steps_ = 0;

	Sample lr_;
	Sample b1_, b2_;
	Sample eps_ = Sample(1e-8);

public:
	Adam(Sample lr = Sample(.001)) : lr_(lr), b1_(.9), b2_(.999) {}

	void Reset(int n)
	{
		m_.assign(n, Sample(0));
		v_.assign(n, Sample(0));
		bp.assign(n, Sample(0));
		steps_ = 0;
	}
	float lastloss = 9999999, bestloss = 9999999.;
	int failiter = 0;
	Sample Update(Sample* params, Sample* grads, int numParams, Sample loss)
	{
		if ((int)m_.size() != numParams) Reset(numParams);
		++steps_;

		if (loss < lastloss)lr_ *= 1.01;
		else lr_ *= 0.9, failiter++;
		lastloss = loss;

		if (loss < bestloss)//最好参数
		{
			bestloss = loss;
			for (int i = 0; i < numParams; ++i)
				bp[i] = params[i];
			failiter = 0;
		}
		if (failiter > 20)//fallback机制，当一直失败20次时进行
		{
			for (int i = 0; i < numParams; ++i)
				params[i] = bp[i];
			lr_ *= 0.1;
		}

		const Sample c1 = Sample(1) - Sample(std::pow(double(b1_), steps_));
		const Sample c2 = Sample(1) - Sample(std::pow(double(b2_), steps_));
		const Sample oneMinusB1 = Sample(1) - b1_;
		const Sample oneMinusB2 = Sample(1) - b2_;
		for (int i = 0; i < numParams; ++i)
		{
			const Sample g = grads[i];
			m_[i] = b1_ * m_[i] + oneMinusB1 * g;
			v_[i] = b2_ * v_[i] + oneMinusB2 * g * g;
			const Sample mhat = m_[i] / c1;
			const Sample vhat = v_[i] / c2;
			params[i] -= lr_ * mhat / (Sample(std::sqrt(double(vhat))) + eps_);
		}


		return loss;
	}
};



template<typename Sample>
class EnsAdam
{
public:
	EnsAdam(Sample lr = Sample(.001))
		: lr_(lr),
		rule_(new ens::AdamUpdate())
	{
	}

	void Reset(int n)
	{
		if (n <= 0)
			throw std::invalid_argument("Invalid parameter count.");

		n_ = n;

		policy_.reset(
			new Policy(*rule_, static_cast<size_t>(n), 1));
	}

	Sample Update(
		Sample* params,
		Sample* grads,
		int numParams,
		Sample loss)
	{
		if (!params || !grads || numParams <= 0)
			throw std::invalid_argument("Invalid input.");

		if (!policy_ || n_ != numParams)
			Reset(numParams);

		// Zero-copy Armadillo views.
		Mat p(params, numParams, 1, false, true);
		Mat g(grads, numParams, 1, false, true);

		policy_->Update(
			p,
			static_cast<double>(lr_),
			g);

		return loss;
	}
	void SetLR(double lr)
	{
		lr_ = lr;
	}

private:
	using Mat = arma::Mat<Sample>;

	using Policy =
		ens::AdamUpdate::Policy<Mat, Mat>;

	Sample lr_;

	// Policy holds a reference to AdamUpdate.
	// Heap allocation keeps the address stable.
	std::unique_ptr<ens::AdamUpdate> rule_;

	std::unique_ptr<Policy> policy_;

	int n_ = 0;
};
