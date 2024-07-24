    #version 450

layout(std140, binding = 0) uniform LocalConstants {
    mat4 model;
    mat4 view_projection;
    vec4 camera_position;
    uint texture_index;
};

    void main() {
        gl_Position = model * vec4(0.0, 0.0, 0.0, 1.0);
    }