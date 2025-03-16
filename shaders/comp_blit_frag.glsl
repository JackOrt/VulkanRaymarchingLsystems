#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Our descriptor set (set=0) with binding=1 as a combined sampler2D
layout(set = 0, binding = 1) uniform sampler2D renderedImage;

void main() {
    outColor = texture(renderedImage, fragUV);
}
