#include <iostream>
#include "loss.h"

int main()
{
    const double x = 5.0;
    std::cout << "x=" << x
              << " loss=" << loss(x)
              << " gradient=" << gradient(x)
              << '\n';
}
