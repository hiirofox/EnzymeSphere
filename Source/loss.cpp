// Cross-backend C++ compatibility probe: use the same source constructs for
// OpenCL SPIR-V, Vulkan SPIR-V, and the CPU Enzyme reference.
struct LossTerm
{
    float difference;
    ~LossTerm() {}
};

template <typename T, int N>
struct FixedArray
{
    T values[N];

    T& operator[](int index)
    {
        return values[index];
    }
};

constexpr float loss_square(float value)
{
    return value * value;
}

enum class LossTaskKind
{
    Zero,
    Other
};

float loss(int task, int NumTasks, float* params, int NumParams)
{
    (void)NumTasks;
    if (NumParams <= 0)
        return 0.0f;

    const LossTaskKind taskKind = task == 0 ? LossTaskKind::Zero : LossTaskKind::Other;
    int taskBase = 0;
    switch (taskKind)
    {
    case LossTaskKind::Zero:
        taskBase = 0;
        break;
    case LossTaskKind::Other:
        taskBase = task;
        break;
    }

    constexpr float lossScale = 1.0f;
    float taskLoss = 0.0f;
    int i = 0;
    while (i < NumParams)
    {
        LossTerm term{params[i] - static_cast<float>(taskBase + i)};
        const float& difference = term.difference;
        FixedArray<float, 1> oneTerm{{difference}};
        for (int j = 0; j < 1; ++j)
        {
            float value = oneTerm[j];
            float magnitude = value;
            if (magnitude < 0.0f)
                magnitude = -magnitude;
            taskLoss += lossScale * [=](float x)
            {
                return loss_square(x);
            }(magnitude);
        }
        ++i;
    }
    return taskLoss;
}
