#pragma once

void loss(double* params, double* lossOut, int numParams);
double gradient(const double* params, double* grad, int numParams);