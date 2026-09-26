#version 450 core

// Sprite texture on unit 0 (engine samplers count down from 15, see kShadowTextureUnit).
layout(binding = 0) uniform sampler2D uSpriteTexture;
// 0 = no texture: a soft round disc instead.
uniform int uHasTexture;

in vec2 vUV;
in vec2 vLocal;
in vec4 vColor;
out vec4 FragColor;

void main()
{
    vec4 texel;
    if (uHasTexture != 0)
    {
        texel = texture(uSpriteTexture, vUV);
    }
    else
    {
        float falloff = clamp(1.0 - length(vLocal), 0.0, 1.0);
        texel = vec4(1.0, 1.0, 1.0, falloff * falloff);
    }

    vec4 color = vColor * texel;
    // Depth writes are off for the whole pass, so this only saves blending work.
    if (color.a <= 0.002) discard;
    FragColor = color;
}
