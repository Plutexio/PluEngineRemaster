#version 450 core

uniform vec3 uColor;
flat in float vShadow;
out vec4 FragColor;

// Particles are unlit, so "in shadow" can only mean darker. Brightness of a fully shadowed particle.
const float kShadowedBrightness = 0.35;

void main() {
    FragColor = vec4(uColor * mix(kShadowedBrightness, 1.0, vShadow), 1.0);
}
