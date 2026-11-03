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
#include <cmath>
#include <algorithm>
#include <stdexcept>

template<typename Sample>
class Adam
{
    std::vector<double> m_, v_;
    int steps_ = 0;
    double b1_ = .9, b2_ = .999, eps_ = 1e-8;
public:
    explicit Adam(int n = 0) : m_(n, 0.), v_(n, 0.) {}
    void Reset(int n) { m_.assign(n, 0.); v_.assign(n, 0.); steps_ = 0; }
    // baseLR=0.0004 -> Hammer table and NL 6e-4; delay/APF 1.5e-4;
    // bridge+board 5e-4. No weight decay, no hammer regularization.
    void Update(Sample* p, const Sample* grads, int n, Sample baseLR = Sample(.0004),
                Sample maxNorm = Sample(5))
    {
        if ((int)m_.size() != n) Reset(n);
        double norm2 = 0.;
        for (int i = 0; i < n; ++i) {
            //if (!std::isfinite(grads[i])) throw std::runtime_error("Nonfinite Enzyme gradient");
            norm2 += double(grads[i])*grads[i];
        }
        double clip = std::min(1., double(maxNorm)/std::sqrt(norm2 + 1e-30));
        ++steps_;
        double c1 = 1. - std::pow(b1_, steps_), c2 = 1. - std::pow(b2_, steps_);
        constexpr int strings = 3, hammerTable = 512, hammer = 580;
        constexpr int perString = 653; // 512 + 68 NL + 1 delay + 4 APF + 3*17 bridge + 17 board
        static_assert(perString * strings == 1959, "PianoDWG-v1.3fix expects original parameter layout");
        for (int i = 0; i < n; ++i) {
            int local = i % perString;
            double mul = local < hammerTable ? 1.5 :
                         local < hammer ? 1.5 :
                         local < hammer + 5 ? .375 : 1.25;
            double g = grads[i] * clip;
            m_[i] = b1_*m_[i] + (1.-b1_)*g;
            v_[i] = b2_*v_[i] + (1.-b2_)*g*g;
            p[i] = Sample(double(p[i]) - double(baseLR)*mul*(m_[i]/c1)/(std::sqrt(v_[i]/c2)+eps_));
        }
    }
};
