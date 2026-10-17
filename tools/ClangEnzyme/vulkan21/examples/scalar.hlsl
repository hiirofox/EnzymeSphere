float gradient(float x);
RWStructuredBuffer<float> output : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  output[id.x] = gradient(float(id.x));
}
