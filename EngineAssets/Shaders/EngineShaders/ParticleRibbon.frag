#version 450 core

// Ribbon texture on unit 0: u runs along the ribbon (0 at the head / newest), v across it.
layout(binding = 0) uniform sampler2D uRibbonTexture;
// 0 = no texture: soft edges across the width instead.
uniform int uHasTexture;

in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

void main()
{
    vec4 texel;
    if (uHasTexture != 0)
    {
        texel = texture(uRibbonTexture, vUV);
    }
    else
    {
        float across = abs(vUV.y * 2.0 - 1.0);
        texel = vec4(1.0, 1.0, 1.0, 1.0 - across * across);
    }

    vec4 color = vColor * texel;
    if (color.a <= 0.002) discard;
    FragColor = color;
}
