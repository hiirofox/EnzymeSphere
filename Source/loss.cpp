#include "loss.h"
#include <complex>

extern "C" {
	extern int enzyme_dup;
	extern int enzyme_const;
	void __enzyme_autodiff(void*, ...);
}

std::complex<double> DampOscHw(double omega, double a, double k, double w, double phi)
{
	//Aexp(-kt)cos(wt+phi)
	std::complex<double> s{ 0., omega };
	return ((s + k) * cos(phi) - w * sin(phi)) * a / ((s + k) * (s + k) + w * w);
}
void loss(double* params, double* lossOut, int numParams)
{
	//a,k,w,phi为一个模态
	constexpr static int N = 1000;
	constexpr static int nyq = N / 4;
	double loss = 0.0;
	for (int n = 0; n <= N; ++n)
	{
		std::complex<double> h{ 0., 0. };
		double omega = (double)n / nyq * 3.14159265358979;//nyq->pi
		for (int i = 0; i < numParams; i += 4)
		{
			double a = params[i + 0];
			double k = params[i + 1];
			double w = params[i + 2];
			double phi = params[i + 3];
			h += DampOscHw(omega, a, k, w, phi);
		}
		double mag = std::abs(h);
		double l = 0;
		if (n == 0)
		{
			l = mag;
			l = l * l * 10.0;
		}
		else if (n < nyq - 1)
		{
			l = mag - 1.;
			l = l * l * l * l * 10000.;
		}
		else if (n >= nyq)
		{
			l = mag;
			l = l * l * 100.;
		}
		loss += l;
	}
	*lossOut = loss / N;
}

double gradient(const double* params, double* grad, int numParams)
{
	for (int i = 0; i < numParams; ++i)grad[i] = 0.;
	double lossOut, dLoss = 1.;
	__enzyme_autodiff(
		(void*)loss,
		enzyme_dup, params, grad,
		enzyme_dup, &lossOut, &dLoss,
		enzyme_const, numParams
	);
	return lossOut;
}
