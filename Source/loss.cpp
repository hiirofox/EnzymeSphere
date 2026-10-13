#include <array>
#include <algorithm>
#include <cmath>
#include "loss.h"

#include <array>
#include <cmath>

constexpr int N = 20;
std::array<double, N> a;

double loss(double x)
{
    for (int i = 0; i < N; i++)
    {
        double t = x + i * 0.15;

        a[i] =
            3.0 * std::sin(t)
            + 2.0 * std::cos(2.5 * t)
            + 0.5 * std::log(1.0 + t * t)
            + 0.1 * t * t;
    }
    double sum = 0.0;
    for (int i = 0; i < N; i++)
    {
        double y = a[i];
        sum +=
            y * y * 0.05
            + std::sin(y * 3.0)
            + std::cos(y * 0.5);
    }
    return sum + 0.5 * x * x;
}

extern "C" double __enzyme_autodiff(void*, double);

double gradient(double x)
{
	return __enzyme_autodiff((void*)loss, x);
}