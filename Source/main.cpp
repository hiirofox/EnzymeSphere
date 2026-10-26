#include "ui.h"
#include "loss.h"
#include "optimizer.h"

AdamW opt{ .1, .01, .9, .9 };
constexpr static int numParams = 4 * 6;
double params[numParams];
double grads[numParams];
void InitParams()
{
	for (int i = 0; i < numParams; i += 4)
	{
		params[i + 0] = 1. / (numParams / 4);//a
		params[i + 1] = (double)rand() / RAND_MAX * 0.001;//k
		params[i + 2] = (double)rand() / RAND_MAX * 10.0;//w
		params[i + 3] = (double)rand() / RAND_MAX * 3.1415926;//phi
	}
}

int main()
{
	TrainStateUI canvas;
	if (!canvas.Create(800, 600)) return 1;
	HWND hwnd = canvas.GetHWND();

	InitParams();
	for (int iter = 0; iter < 7000; iter++)
	{
		double loss = gradient(params, grads, numParams);
		opt.Update(params, grads, numParams, loss);
		for (int i = 0; i < numParams; i += 4)
		{
			double k = params[i + 1];//k
			params[i + 1] = std::abs(k) + 1e-6;
		}
		printf("iter:%3d loss=%.5f\n", iter, loss);
		canvas.UpdateAndRepaint(params, numParams);
	}
	for (int i = 0; i < numParams; i += 4)
	{
		double a = params[i + 0];
		double k = params[i + 1];
		double w = params[i + 2];
		double phi = params[i + 3];
		printf("%f, %f, %f, %f, \n", a, k, w, phi);
	}
	for (;;)std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}