#pragma once
#include <cmath>
#include <algorithm>
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
	Adam(Sample lr = Sample(.001)) : lr_(lr), b1_(.9), b2_(.999) {}

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


template<typename Sample>
class LBFGS
{
	std::vector<std::vector<Sample>> sHist_, yHist_;
	std::vector<Sample> rho_;
	std::vector<Sample> baseParams_, baseGrads_, direction_;
	Sample lr_;
	Sample baseLoss_ = Sample(0);
	Sample step_ = Sample(0);
	Sample dg0_ = Sample(0);
	int lineSearchTrials_ = 0;
	bool searching_ = false;
	static bool Finite(Sample x)
	{
		return std::isfinite(double(x));
	}
	static Sample Dot(const std::vector<Sample>& a, const std::vector<Sample>& b)
	{
		Sample v = Sample(0);
		for (int i = 0; i < (int)a.size(); ++i) v += a[i] * b[i];
		return v;
	}
	Sample InitialStep() const
	{
		return Finite(lr_) && lr_ > Sample(0) ? lr_ : Sample(.001);
	}
	void ClearHistory()
	{
		sHist_.clear();
		yHist_.clear();
		rho_.clear();
	}
	void AddHistory(const Sample* params, const Sample* grads, int n)
	{
		std::vector<Sample> s(n), y(n);
		for (int i = 0; i < n; ++i)
		{
			s[i] = params[i] - baseParams_[i];
			const Sample g = Finite(grads[i]) ? grads[i] : Sample(0);
			y[i] = g - baseGrads_[i];
		}
		const Sample ys = Dot(y, s);
		const Sample ss = Dot(s, s);
		const Sample yy = Dot(y, y);
		if (!Finite(ys) || !Finite(ss) || !Finite(yy) || ss <= Sample(0) || yy <= Sample(0)) return;
		const Sample threshold = Sample(1e-10) * Sample(std::sqrt(double(ss) * double(yy)));
		if (ys <= threshold) return;
		const Sample rho = Sample(1) / ys;
		if (!Finite(rho)) return;
		if ((int)sHist_.size() >= 30)
		{
			sHist_.erase(sHist_.begin());
			yHist_.erase(yHist_.begin());
			rho_.erase(rho_.begin());
		}
		sHist_.push_back(std::move(s));
		yHist_.push_back(std::move(y));
		rho_.push_back(rho);
	}
	void StartStep(Sample* params, Sample* grads, int n, Sample loss)
	{
		baseParams_.assign(params, params + n);
		baseGrads_.resize(n);
		for (int i = 0; i < n; ++i) baseGrads_[i] = Finite(grads[i]) ? grads[i] : Sample(0);
		const Sample gradNorm2 = Dot(baseGrads_, baseGrads_);
		if (!Finite(gradNorm2) || gradNorm2 <= Sample(1e-24))
		{
			searching_ = false;
			return;
		}
		if (!Finite(loss))
		{
			ClearHistory();
			searching_ = false;
			const Sample a = InitialStep();
			for (int i = 0; i < n; ++i) params[i] = baseParams_[i] - a * baseGrads_[i];
			return;
		}
		const int m = (int)sHist_.size();
		std::vector<Sample> q = baseGrads_;
		std::vector<Sample> alpha(m, Sample(0));
		for (int k = m - 1; k >= 0; --k)
		{
			alpha[k] = rho_[k] * Dot(sHist_[k], q);
			for (int i = 0; i < n; ++i) q[i] -= alpha[k] * yHist_[k][i];
		}
		Sample gamma = Sample(1);
		if (m > 0)
		{
			const Sample sy = Dot(sHist_.back(), yHist_.back());
			const Sample yy = Dot(yHist_.back(), yHist_.back());
			if (Finite(sy) && Finite(yy) && yy > Sample(1e-20)) gamma = sy / yy;
		}
		direction_.resize(n);
		for (int i = 0; i < n; ++i) direction_[i] = gamma * q[i];
		for (int k = 0; k < m; ++k)
		{
			const Sample beta = rho_[k] * Dot(yHist_[k], direction_);
			const Sample c = alpha[k] - beta;
			for (int i = 0; i < n; ++i) direction_[i] += c * sHist_[k][i];
		}
		for (int i = 0; i < n; ++i) direction_[i] = -direction_[i];
		dg0_ = Dot(baseGrads_, direction_);
		if (!Finite(dg0_) || dg0_ >= Sample(0))
		{
			ClearHistory();
			for (int i = 0; i < n; ++i) direction_[i] = -baseGrads_[i];
			dg0_ = -gradNorm2;
		}
		if (!Finite(dg0_) || dg0_ >= Sample(0))
		{
			searching_ = false;
			return;
		}
		baseLoss_ = loss;
		step_ = InitialStep();
		lineSearchTrials_ = 0;
		searching_ = true;
		for (int i = 0; i < n; ++i) params[i] = baseParams_[i] + step_ * direction_[i];
	}
public:
	LBFGS(Sample lr = Sample(.001)) : lr_(lr) {}
	void Reset(int n)
	{
		ClearHistory();
		baseParams_.assign(n, Sample(0));
		baseGrads_.assign(n, Sample(0));
		direction_.assign(n, Sample(0));
		baseLoss_ = Sample(0);
		step_ = Sample(0);
		dg0_ = Sample(0);
		lineSearchTrials_ = 0;
		searching_ = false;
	}
	Sample Update(Sample* params, Sample* grads, int numParams, Sample loss)
	{
		if (numParams <= 0) return loss;
		if ((int)baseParams_.size() != numParams) Reset(numParams);
		if (searching_)
		{
			const Sample tol = Sample(1e-12) + Sample(std::abs(double(baseLoss_))) * Sample(1e-8);
			const bool accepted = Finite(loss) && (loss <= baseLoss_ + Sample(1e-4) * step_ * dg0_ || loss <= baseLoss_ + tol);
			if (accepted)
			{
				AddHistory(params, grads, numParams);
				searching_ = false;
				StartStep(params, grads, numParams, loss);
				return loss;
			}
			if (lineSearchTrials_ < 8)
			{
				++lineSearchTrials_;
				step_ *= Sample(.5);
				for (int i = 0; i < numParams; ++i) params[i] = baseParams_[i] + step_ * direction_[i];
				return loss;
			}
			const Sample fallbackStep = std::max(Sample(1e-8), InitialStep() * Sample(.01));
			for (int i = 0; i < numParams; ++i) params[i] = baseParams_[i] - fallbackStep * baseGrads_[i];
			ClearHistory();
			searching_ = false;
			return loss;
		}
		StartStep(params, grads, numParams, loss);
		return loss;
	}
};