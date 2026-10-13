#include <iostream>
#include "loss.h"

int main()
{
    double x = 0;

    for (int i = 0; i < 100; i++)
    {
        double g = gradient(x);
        double l = loss(x);

        x -= 0.01 * g;

        std::cout
            << "x:" << x
            << " loss:" << l
            << " grad:" << g
            << std::endl;
    }
}