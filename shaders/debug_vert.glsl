#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragColor;
layout(std140, push_constant) uniform DebugPush {
    mat4 viewProjection;
} debugPush;
void main() {
    gl_Position = debugPush.viewProjection * vec4(inPosition, 1.0);
    fragColor = vec3(0.0, 1.0, 0.0);
}
