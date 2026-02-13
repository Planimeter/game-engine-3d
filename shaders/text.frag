#version 450
layout(set = 0, binding = 0) uniform sampler2D tex[];
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 texSample = texture(tex[0], v_texcoord);
    out_color = vec4(1.0, 1.0, 1.0, texSample.a);
}
