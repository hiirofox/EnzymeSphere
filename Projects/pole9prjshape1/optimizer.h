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
	void Update(Sample* params, Sample* grads, int numParams, Sample loss)
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

		/*
		//adaptive loss
		if (loss < lastloss)lr *= 1.0001;
		else lr *= 0.9;
		lastloss = loss;
		*/
	}
};