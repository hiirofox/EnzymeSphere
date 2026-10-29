#include "ui.h"
#include "loss.h"
#include "sm.h"
#include "optimizer.h"

AdamW<float> opt{ .0001, .01, .9999, .9999 };
constexpr static int NumParams = sm::SynthModel::GetNumParams();
float params[NumParams];
float grads[NumParams];
sm::SynthModel syn1;

void LoadCheckpoint()
{
	FILE* pf = fopen("checkpoint.txt", "r");
	if (!pf)
	{
		printf("!!!!!!!!\n");
		exit(0);
	}
	for (int i = 0; i < NumParams; ++i)
	{
		fscanf(pf, "%f, ", &params[i]);
	}
	fclose(pf);
}
void SaveCheckpoint()
{
	FILE* pf = fopen("checkpoint.txt", "w");
	for (int i = 0; i < NumParams; ++i)
	{
		fprintf(pf, "%.8f, ", params[i]);
	}
	fclose(pf);
}

int main()
{
	TrainStateUI canvas;
	if (!canvas.Create(800, 600)) return 1;
	HWND hwnd = canvas.GetHWND();

	LoadCheckpoint();

	//syn1.InitModel(params);
	for (int iter = 0;; ++iter)
	{
		float loss = gradient(params, grads, NumParams);
		opt.Update(params, grads, NumParams, loss);
		printf("iter:%d loss=%.5f\n", iter, loss);
		if (iter % 20 == 0)
		{
			SaveCheckpoint();
		}
	}

	for (;;)std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}