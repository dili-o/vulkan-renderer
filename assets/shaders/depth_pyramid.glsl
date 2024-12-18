

#if defined(COMPUTE_DEPTH_PYRAMID)

layout(set = MATERIAL_SET, binding = 0) uniform sampler2D src;

layout(set = MATERIAL_SET, binding = 1) uniform writeonly image2D dst;

// TODO(marco): use push constants to select LOD

layout( push_constant ) uniform constants
{
	vec2 src_image_size;
};

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {

	uvec2 pos = gl_GlobalInvocationID.xy;
	float depth = texture(src, (vec2(pos) + vec2(0.5)) / src_image_size).x;
	
	imageStore( dst, ivec2(pos), vec4( depth, 0, 0, 0 ) );

	groupMemoryBarrier();
	barrier();
}

#endif
