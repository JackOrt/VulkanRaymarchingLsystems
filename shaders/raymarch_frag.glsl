#version 450

// Even though we aren't using them here, 
// we'll keep the push constants and inputs to match your pipeline interface.
layout(std140, push_constant) uniform PushBlock {
    mat4 viewProjection; // Not used for the gradient
    float time;          // Not used
    vec3 cameraPos;      // Not used
} pushBlock;

// The vertex shader is presumably passing fragUV in [0..1].
layout(location = 0) in vec2 fragUV;

// Final output color
layout(location = 0) out vec4 outColor;

void main()
{
    // Just output a simple gradient based on fragUV.
    // fragUV.x => red channel, fragUV.y => green channel, alpha=1
    outColor = vec4(fragUV, 0.0, 1.0);
}
