template<typename R, typename... A> R __enzyme_autodiff(void*, A...);
int enzyme_const;
int enzyme_dup;
float loss(int task, const float* params, int n) {
    float total = 0.0f;
    for (int i = 0; i < n; ++i) {
        float diff = params[i] - float(task + i);
        total += diff * diff;
    }
    return total;
}
float gradient_component(int task, float p0, float p1, float p2, int component) {
    float p[3] = {p0, p1, p2};
    float dp[3] = {0, 0, 0};
    __enzyme_autodiff<void>((void*)loss, enzyme_const, task,
                           enzyme_dup, p, dp, enzyme_const, 3);
    return dp[component];
}
