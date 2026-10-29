#pragma once
#include <vector>

class AdamW
{
private:
	double b1 = .9, b2 = .999;
	double lr = .001, lambda = .01;
	double s1 = 1., s2 = 1.;
	double lastloss = 0., smoothdloss = 0.;
	std::vector<double> m, v;
public:
	AdamW(double lr = .001, double lambda = .01, double b1 = .9, double b2 = .99) :lr(lr), lambda(lambda), b1(b1), b2(b2)
	{
		m.resize(100, 0.);
		v.resize(100, 0.);
		s1 = s2 = 1.;
		lastloss = 0.;
		smoothdloss = 0.;
	}
	void Update(double* params, double* grads, int numParams, double loss)
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
			double mhat = m[i] / (1. - s1);
			double vhat = v[i] / (1. - s2);
			params[i] = (1. - lr * lambda) * params[i] - lr * mhat / (sqrt(vhat) + 1.e-8);
		}

		//adaptive loss
		if (loss < lastloss)lr *= 1.0001;
		else lr *= 0.9;
		lastloss = loss;
	}
};