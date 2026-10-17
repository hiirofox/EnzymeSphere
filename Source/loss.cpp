#include "loss.h"

double loss(double x)
{
    const double difference = x - 3.0;
    return difference * difference;
}

extern "C" double __enzyme_autodiff(void*, double);

double gradient(double x)
{
    return __enzyme_autodiff((void*)loss, x);
}
