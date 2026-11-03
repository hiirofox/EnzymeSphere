#pragma once
#include <vector>

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
	std::vector<Sample> m_, v_;
	int steps_ = 0;

	Sample lr_;
	Sample b1_, b2_;
	Sample eps_ = Sample(1e-8);

public:
	Adam(Sample lr = Sample(.001), Sample b1 = Sample(.9), Sample b2 = Sample(.999)) : lr_(lr), b1_(b1), b2_(b2) {}

	void Reset(int n)
	{
		m_.assign(n, Sample(0));
		v_.assign(n, Sample(0));
		steps_ = 0;
	}

	Sample Update(Sample* params, Sample* grads, int numParams, Sample loss)
	{
		if ((int)m_.size() != numParams) Reset(numParams);
		++steps_;
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