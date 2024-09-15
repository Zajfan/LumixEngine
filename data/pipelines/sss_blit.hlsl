//@include "pipelines/common.hlsli"

cbuffer Data : register(b4) {
	float2 u_size;
	float u_current_frame_weight;
	uint u_sss;
	uint u_history;
	uint u_depthbuf;
	uint u_gbuffer2;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	uint2 ij = thread_id.xy;
	if (any(ij >= uint2(u_size.xy))) return;

	float depth = bindless_textures[u_depthbuf][ij].x;
	
	float2 uv = (float2(ij) + 0.5) / u_size.xy;
	float2 uv_prev = cameraReproject(uv, depth).xy;

	float current = bindless_textures[u_sss][ij].x;
	if (all(uv_prev < 1) && all(uv_prev > 0)) {
		float prev = sampleBindlessLod(LinearSamplerClamp, u_history, uv_prev, 0).x;
		current = lerp(prev, current, u_current_frame_weight);
		bindless_rw_textures[u_sss][ij] = current;
	}
	
	float4 gb2v = bindless_rw_textures[u_gbuffer2][ij];
	gb2v.w = min(current, gb2v.w);
	bindless_rw_textures[u_gbuffer2][ij] = gb2v;
}

