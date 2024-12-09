

#if defined(COMPUTE_DEPTH_PYRAMID)

layout(set = MATERIAL_SET, binding = 0) uniform sampler2D src;

layout(set = MATERIAL_SET, binding = 1) uniform writeonly image2D dst;

// TODO(marco): use push constants to select LOD

layout( push_constant ) uniform constants
{
	vec2 image_size;
	vec2 padding;
};

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
	ivec2 texel_position00 = ivec2( gl_GlobalInvocationID.xy ) * 2;
	ivec2 texel_position01 = texel_position00 + ivec2(0, 1);
	ivec2 texel_position10 = texel_position00 + ivec2(1, 0);
	ivec2 texel_position11 = texel_position00 + ivec2(1, 1);

	float color00 = texelFetch( src, texel_position00, 0 ).r;
	float color01 = texelFetch( src, texel_position01, 0 ).r;
	float color10 = texelFetch( src, texel_position10, 0 ).r;
	float color11 = texelFetch( src, texel_position11, 0 ).r;

	float result = max( max( max( color00, color01 ), color10 ), color11 );

	//uvec2 pos = gl_GlobalInvocationID.xy;
	//vec2 uv = (vec2(pos) + vec2(0.5)) / image_size; // UV space

	//if (uv.x >= data.x && uv.x <= data.z && uv.y <= data.y && uv.y >= data.w)
	//	imageStore( dst, ivec2( gl_GlobalInvocationID.xy ), vec4( 1, 0, 0, 0 ) );
	//else
	//	imageStore( dst, ivec2( gl_GlobalInvocationID.xy ), vec4( 0, 0, 0, 0 ) );

	imageStore( dst, ivec2( gl_GlobalInvocationID.xy ), vec4( result, 0, 0, 0 ) );

	groupMemoryBarrier();
	barrier();
}

#endif
