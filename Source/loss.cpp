#include <array>
#include "loss.h"

double loss(double x)
{
	std::array<float, 8> a;
	for (int i = 0; i < 8; ++i)
		a[i] = powf(x, i) + log(x);
	float sum = 0;
	for (int i = 0; i < 8; ++i)
		sum += a[i];
	return sum * sum;
}

extern "C" double __enzyme_autodiff(void*, double);

double gradient(double x)
{
	return __enzyme_autodiff((void*)loss, x);
}