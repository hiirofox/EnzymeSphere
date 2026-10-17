float gradient_component(int task, float p0, float p1, float p2, int component);
StructuredBuffer<float> params : register(t0);
RWStructuredBuffer<float> gradients : register(u1);
[numthreads(2, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int task = int(id.x);
    float p0 = params[0];
    float p1 = params[1];
    float p2 = params[2];
    gradients[id.x * 3] = gradient_component(task, p0, p1, p2, 0);
    gradients[id.x * 3 + 1] = gradient_component(task, p0, p1, p2, 1);
    gradients[id.x * 3 + 2] = gradient_component(task, p0, p1, p2, 2);
}
