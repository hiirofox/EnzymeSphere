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
	constexpr static double nyqx = 16;
	constexpr static double eps = 0.01;
	double loss = 0.0;
	double lpass = 0., lstop = 0.;
	int ipass = 0, istop = 0;
	for (int n = 0; n <= N; ++n)
	{
		std::complex<double> h{ 0., 0. };

		const double lw = log(8.3333e-4), hw = log(3.14159265 * nyqx);
		const double x = (double)n / N;
		double omega = exp(x * (hw - lw) + lw);//nyq->pi
		for (int i = 0; i < numParams; i += 4)
		{
			double a = params[i + 0];
			double k = params[i + 1];
			double w = params[i + 2];
			double phi = params[i + 3];
			h += DampOscHw(omega, a, k, w, phi);
		}
		double mag = std::abs(h);
		if (omega < 3.1415926535)
		{
			double l = log10(mag);
			l = pow(l, 2.) * 200.0;
			lpass += l;
			ipass++;
		}
		else
		{
			double l = log10(mag) + 3.;
			l = pow(l, 2.);
			lstop += l;
			istop++;
		}
	}
	loss = lpass / ipass + lstop / istop;
	*lossOut = loss * 10.0;
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
