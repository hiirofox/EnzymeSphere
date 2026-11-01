#define _CRT_SECURE_NO_WARNINGS
#include <array>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#define OPTIM_ENABLE_EIGEN_WRAPPERS

#include <future>
#include <algorithm>
#include <queue>
#include <ensmallen.hpp>
#include <Eigen/Dense>
//#include <optim.hpp>
#include <cmath>
#include <utility>
#include "NLModeling.h"
#include "wavfile.h"
#include "loss.h"

const int NumTasks = 12;
int numTrainBlocks = 1;
std::vector<int> blockStart;
std::vector<int> blockLen;
class ADThreadPool
{
public:
	using Metrics = std::tuple<float, float, float, float, float, float>;
	struct TaskResult
	{
		Metrics metrics;
		Eigen::VectorXf grad;
	};
private:
	std::array<std::thread, NumTasks> pool;
	std::array<TaskResult, NumTasks> results;
	std::array<int, NumTasks> starts{};
	std::array<int, NumTasks> lengths{};
	std::array<bool, NumTasks> pending{};
	std::array<bool, NumTasks> done{};
	std::mutex mutex;
	std::condition_variable cv;
	bool stop = false;
	const float* params = nullptr;
	void ADTask(int id)
	{
		while (true)
		{
			int startPos, batchLen;
			const float* p;
			{
				std::unique_lock lock(mutex);
				cv.wait(lock, [&] { return stop || pending[id]; });
				if (stop) return;
				startPos = starts[id];
				batchLen = lengths[id];
				p = params;
				pending[id] = false;
			}
			auto& result = results[id];
			result.metrics = get_gradient(p, result.grad.data(), NumParams, startPos, batchLen);

			{
				std::lock_guard lock(mutex);
				done[id] = true;
			}
			cv.notify_all();
		}
	}
public:
	ADThreadPool()
	{
		for (auto& r : results)
			r.grad = Eigen::VectorXf::Zero(NumParams);
		for (int i = 0; i < NumTasks; ++i)
			pool[i] = std::thread(&ADThreadPool::ADTask, this, i);
	}
	~ADThreadPool()
	{
		{
			std::lock_guard lock(mutex);
			stop = true;
		}
		cv.notify_all();

		for (auto& t : pool)
			if (t.joinable()) t.join();
	}
	void SetParams(const float* p)
	{
		params = p;
	}
	void AddTask(int id, int startPos, int batchLen)
	{
		{
			std::lock_guard lock(mutex);
			starts[id] = startPos;
			lengths[id] = batchLen;
			done[id] = false;
			pending[id] = true;
		}
		cv.notify_all();
	}
	TaskResult GetResult(int id)
	{
		std::unique_lock lock(mutex);
		cv.wait(lock, [&] { return done[id]; });
		return results[id];
	}
};
//ADThreadPool adPool;
int iter = 0;
float bestLoss = 999999999;
float smoothLoss = 200.0;
float bestParams[SelectedNL::NumParams];
int newScoreFlag = 0;
std::string saveParamsFile = "";
void SaveParams(float* params, float& loss, int NumParams)
{
	FILE* pf = fopen(saveParamsFile.c_str(), "w");
	fprintf(pf, "%d %.5f\n", NumParams, loss);
	for (int i = 0; i < NumParams; ++i)
		fprintf(pf, "%.8f,", params[i]);
	fclose(pf);
}
int ReadParams(float* params, float& loss, int NumParams)
{
	FILE* pf = fopen(saveParamsFile.c_str(), "r");
	if (!pf)return 0;
	int n, tmp;
	fscanf(pf, "%d %f", &n, &loss);
	for (int i = 0; i < NumParams; ++i) fscanf(pf, "%f,", &params[i]);
	fclose(pf);
	return 1;
}
double objective(const Eigen::VectorXd& x, Eigen::VectorXd* grad_out, void* data)
{
	Eigen::VectorXf params = x.cast<float>();
	float sumLoss = 0.0f;
	float sumSpecPeak = 0.0f;
	float sumRms = 0.0f;
	float sumMax = 0.0f;
	float sumAlias = 0.0f;
	float sumStruct = 0.0f;
	Eigen::VectorXf grad = Eigen::VectorXf::Zero(NumParams);

	/*
	adPool.SetParams(params.data());
	for (int i = 0; i < NumTasks; ++i)
		adPool.AddTask(i, blockStart[i], blockLen[i]);
	for (int i = 0; i < NumTasks; ++i)
	{
		auto result = adPool.GetResult(i);
		auto [loss, specPeak, rms, max,alias,struct] = result.metrics;
		sumLoss += loss;
		sumSpecPeak += specPeak;
		sumRms += rms;
		sumMax += max;
		sumAlias += alias;
		grad += result.grad;
	}
	sumLoss /= NumTasks;
	sumSpecPeak /= NumTasks;
	sumRms /= NumTasks;
	sumMax /= NumTasks;
	grad /= NumTasks;
	*/
	//int selectBlockID = rand() % numTrainBlocks;
	int selectBlockID = 0;
	auto [loss, specPeak, rmsValue, maxValue, aliasValue, structValue] =
		get_gradient(params.data(), grad.data(), NumParams, blockStart[selectBlockID], blockLen[selectBlockID]);

	//if (iter == 0)smoothLoss = loss;
	//if (!std::isinf(loss) && !std::isnan(loss))
	//	smoothLoss += 0.0125 * (loss - smoothLoss);
	//sumLoss = smoothLoss;
	sumLoss = loss;
	sumSpecPeak = specPeak;
	sumRms = rmsValue;
	sumMax = maxValue;
	sumAlias = aliasValue;
	sumStruct = structValue;
	if (grad_out)
		*grad_out = grad.cast<double>();

	if (sumLoss < bestLoss)
	{
		bestLoss = sumLoss;
		for (int i = 0; i < SelectedNL::NumParams; ++i)
			bestParams[i] = params[i];
		newScoreFlag = 1;
	}
	if (iter % 5 == 0)
	{
		printf("Iter%5d loss=%03.3f spec=%03.3f rms=%03.3f max=%03.3f alias=%03.3f struct:%03.3f %s\n",
			iter, sumLoss, sumSpecPeak, sumRms, sumMax, sumAlias, sumStruct, newScoreFlag ? "(NEW!)" : "");
		if (newScoreFlag)
		{
			SaveParams(bestParams, bestLoss, SelectedNL::NumParams);
		}
		newScoreFlag = 0;
	}
	++iter;
	return sumLoss;
}

float WindowFunc(float x)//nice window near Harries-Blackman
{
	float z = x * x;
	float q = std::fma(0.34950587, z, -1.27143058);
	q = std::fma(q, z, 2.32561793);
	q = std::fma(q, z, -2.33733549);
	q = std::fma(q, z, 1.0);
	q = q * q;
	return std::fma(-z, q, q);
}
int SegmentBlock(std::vector<float>& samples, int numSamples, std::vector<int>& start, std::vector<int>& len)
{
	int blockSize = numSamples / numTrainBlocks;
	for (int i = 0; i < numTrainBlocks; ++i)
	{
		printf("block:%d:%d (%.2fs)\n", blockSize * i, blockSize, blockSize / 48000.0);
		int startPos = blockSize * i;
		start.push_back(startPos);
		len.push_back(blockSize);
		for (int j = startPos, k = 0; j < startPos + bootSize; ++j, ++k)
		{
			float x = (float)k / bootSize;
			samples[j] *= WindowFunc(x - 1.0);//给数据集一个缓慢上升的窗
		}
	}
	return blockSize;
}

class EnsmallenObjective
{
public:
	size_t NumFunctions()
	{
		return 1;
	}

	void Shuffle()
	{
	}

	double EvaluateWithGradient(const arma::mat& x, arma::mat& grad)
	{
		Eigen::VectorXd xEigen(NumParams);
		Eigen::VectorXd gradEigen(NumParams);

		for (int i = 0; i < NumParams; ++i)
			xEigen[i] = x[i];

		double loss = objective(xEigen, &gradEigen, nullptr);

		grad.set_size(NumParams, 1);
		for (int i = 0; i < NumParams; ++i)
			grad[i] = gradEigen[i];

		return loss;
	}

	double EvaluateWithGradient(
		const arma::mat& x,
		const size_t begin,
		arma::mat& grad,
		const size_t batchSize)
	{
		return EvaluateWithGradient(x, grad);
	}
};

WavReader wr;
int main()
{
	for (int i = 0; i < WindowSize; ++i)
	{
		window[i] = WindowFunc((float)i / WindowSize * 2.0 - 1.0);
	}

	//testX.resize(BatchSampleLen);
	//targetY.resize(BatchSampleLen);
	//GenerateTestData(testX.data(), targetY.data(), BatchSampleLen);

	//std::string root = "/home/hiirofox/TestEnzyme/projects/NonlinearModeling/builds/";
	std::string root = "";

	std::ifstream f(root + "tasks.txt");
	int tasks = 0, maxiter = 1; float minloss = 0;
	f >> tasks >> minloss >> maxiter; f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	std::vector<std::string> targetsName(tasks);
	std::vector<int> okflags(tasks, 0);
	printf("tasks:%d\nMaxIter:%d\nminLoss:%.5f\ntargets:\n", tasks, maxiter, minloss);
	for (int i = 0; i < tasks; ++i) {
		std::getline(f, targetsName[i]);
		if (!targetsName[i].empty() && targetsName[i].back() == '\r')
			targetsName[i].pop_back();
		std::cout << "\"" << targetsName[i] << "\"\n";
	}

	printf("\n---TASKS START---\n\n");

	for (;;) for (int taskid = 0; taskid < tasks; ++taskid)
	{
		int allokflag = 1;
		for (int i = 0; i < tasks; ++i)
			if (!okflags[i])allokflag = 0;
		if (allokflag) goto done;
		if (okflags[taskid])continue;

		std::cout << targetsName[taskid] << "\n";

		wr.OpenWAV(root + "tb-input.wav");//input.wav
		//std::string target = "nd-d10-t05";//target
		std::string target = targetsName[taskid];
		BatchSampleLen = wr.GetNumSamples();
		printf("wav NumSamples:%d\n", BatchSampleLen);
		testX.resize(BatchSampleLen);
		targetY.resize(BatchSampleLen);
		wr.ReadBlockMono(testX.data(), BatchSampleLen);
		wr.OpenWAV(root + target + ".wav");
		saveParamsFile = "bestloss-nl3-" + target + ".txt";

		wr.ReadBlockMono(targetY.data(), BatchSampleLen);
		float xrms = 0;
		float yrms = 0;
		float yAvgEnergy = 0;
		for (int i = 0; i < BatchSampleLen; ++i)
		{
			testX[i] *= 1.0;
			targetY[i] *= 1.0 / 1.414213562;//+3dB(reaper导出mono)
			xrms += testX[i] * testX[i] * 0.01;
			yrms += targetY[i] * targetY[i] * 0.01;
		}
		yAvgEnergy = yrms / BatchSampleLen / 0.0001;
		SegmentBlock(testX, BatchSampleLen, blockStart, blockLen);

		xrms = sqrtf(xrms / 0.0001);
		yrms = sqrtf(yrms / 0.0001);
		printf("boot sample:%d(%.2fs)\n", bootSize, (float)bootSize / 48000.0);
		printf("read ok. xrms=%.5f yrms=%.5f\n", xrms, yrms);

		//open file
		float directParams[NumParams];
		float fileParamsLoss = 9999999999;
		int result = ReadParams(directParams, fileParamsLoss, NumParams);
		if (result && fileParamsLoss < minloss)
		{
			okflags[taskid] = 1;
			continue;
		}
		if (!result)SelectedNL::NLModelParams::InitVecDirect(directParams);

		arma::vec x(NumParams);
		for (int i = 0; i < NumParams; ++i)
			bestParams[i] = x[i] = directParams[i];

		EnsmallenObjective objectiveFunction;
		float lrstart = 0.0005;
		ens::Adam adam;
		adam.StepSize() = lrstart;
		adam.BatchSize() = 1;
		adam.Beta1() = 0.9;
		adam.Beta2() = 0.999;
		adam.Epsilon() = 1e-8;
		adam.MaxIterations() = maxiter;
		adam.Tolerance() = 0.0;
		adam.Shuffle() = false;
		ens::L_BFGS lbfgs;
		lbfgs.MaxIterations() = maxiter;


		
		float adamloss = bestLoss;
		bestLoss = 999999999;
		iter = 0;
		adam.Optimize(objectiveFunction, x);
		for (int i = 0; i < NumParams; ++i) x[i] = bestParams[i];
		printf("adam loss: %.3f->%.3f (%.3f%%)\n", adamloss, bestLoss, (adamloss - bestLoss) / adamloss * 100.0);
		
		/*
		float lbfgsloss = bestLoss;
		bestLoss = 999999999;
		iter = 0;
		lbfgs.Optimize(objectiveFunction, x);
		for (int i = 0; i < NumParams; ++i) x[i] = bestParams[i];
		printf("lbfgs loss: %.3f->%.3f (%.3f%%)\n", lbfgsloss, bestLoss, (lbfgsloss - bestLoss) / lbfgsloss * 100.0);
		*/

		if (bestLoss < minloss) okflags[taskid] = 1;
	}
done:;


}
