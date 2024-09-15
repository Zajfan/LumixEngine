//@surface
//@include "pipelines/common.hlsli"
//@texture_slot "Texture", "textures/common/white.tga"
//@uniform "Material color", "color", {1, 1, 1, 1}

struct VSInput {
	float3 position : TEXCOORD0;
	float3 i_pos : TEXCOORD1;
	float4 i_rot : TEXCOORD2;
	float3 i_half_extents : TEXCOORD3;
	float2 i_uv_scale : TEXCOORD4;
};

struct VSOutput {
	float3 half_extents : TEXCOORD0;
	float3 pos : TEXCOORD1;
	float4 rot : TEXCOORD2;
	float2 uv_scale : TEXCOORD3;
	float4 position : SV_POSITION;
};

struct Input {
	float3 half_extents : TEXCOORD0;
	float3 pos : TEXCOORD1;
	float4 rot : TEXCOORD2;
	float2 uv_scale : TEXCOORD3;
	float4 frag_coord : SV_POSITION;
};

struct Output {
	float4 o0 : SV_TARGET0;
	float4 o1 : SV_TARGET1;
	float4 o2 : SV_TARGET2;
};

cbuffer DC : register(b4) {
	uint u_gbuffer_depth;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	output.pos = input.i_pos;
	output.rot = input.i_rot;
	output.half_extents = input.i_half_extents;
	float3 pos = rotateByQuat(input.i_rot, input.position * input.i_half_extents);
	pos += input.i_pos;
	output.uv_scale = input.i_uv_scale;
	output.position = mul(float4(pos, 1), mul(Global_view, Global_projection));
	return output;
}

Output mainPS(Input input) {
	float2 screen_uv = input.frag_coord.xy / Global_framebuffer_size;
	float3 wpos = getViewPosition(u_gbuffer_depth, Global_inv_view_projection, screen_uv);
	
	float4 r = input.rot;
	r.w = -r.w;
	float3 lpos = rotateByQuat(r, wpos - input.pos);
	if (any(abs(lpos) > input.half_extents)) discard;
	
	float4 color = sampleBindless(LinearSampler, t_texture, (lpos.xz / input.half_extents.xz * 0.5 + 0.5) * input.uv_scale);
	//if (color.a < 0.01) discard;
	color.rgb *= u_material_color.rgb;

	Output output;
	output.o0 = float4(color.rgb, color.a);
	output.o1 = float4(0, 0, 0, 0);
	output.o2 = float4(0, 0, 0, 0);
	return output;
}
