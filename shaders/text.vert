#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_texcoord;
layout(location = 0) out vec2 v_texcoord;
void main() {
    gl_Position = vec4(in_position, 1.0);
    /* Engine matrices are GL-style (y up); Vulkan's clip space is y down */
    gl_Position.y = -gl_Position.y;
    v_texcoord = in_texcoord;
}
