//@include "pipelines/common.hlsli"

cbuffer Drawcall : register(b4) {
	float u_intensity;
	float u_lumamount;
	uint u_source;
	uint u_noise;
	uint u_output;
};

float3 filmGrain(float3 in_color, uint2 frag_coord) {
	int2 texture_size = int2(textureSize(bindless_textures[u_noise], 0));
	uint2 ij = (frag_coord + Global_time * 1e4) % texture_size;
	float3 noise = bindless_textures[u_noise][ij].xyz;
	float _luminance = lerp(0.0, luminance(in_color), u_lumamount);
	float lum = smoothstep(0.2, 0.0, _luminance) + _luminance;
	lum += _luminance;
	
	noise = lerp(0, pow(lum, 4.0), noise);
	return in_color + noise * u_intensity;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float3 c = bindless_textures[u_source][thread_id.xy].rgb;
	bindless_rw_textures[u_output][thread_id.xy] = float4(filmGrain(c, thread_id.xy), 1);
}
