RWStructuredBuffer<float> output : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float x = float(id.x);
  output[id.x] = x * x;
}
