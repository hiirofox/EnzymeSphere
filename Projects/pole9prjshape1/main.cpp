#include "ui.h"
#include "loss.h"
#include "sm.h"
#include "optimizer.h"

AdamW<float> opt{ .1, .01, .9, .999 };
constexpr static int NumParams = sm::SynthModel::GetNumParams();
float params[NumParams];
float grads[NumParams];
sm::SynthModel syn;

int main()
{
	TrainStateUI canvas;
	if (!canvas.Create(800, 600)) return 1;
	HWND hwnd = canvas.GetHWND();

	syn.InitModel(params);
	for (int iter = 0;; ++iter)
	{
		float loss = gradient(params, grads, NumParams);
		opt.Update(params, grads, NumParams, loss);
		printf("iter:%d loss=%.5f\n", i, loss);
	}


	for (;;)std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}