#version 450 core

layout(std430, binding = 0) buffer BoneMatrices {
    mat4 finalBoneMatrix[];
};

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aVertColor;
layout (location = 4) in vec4 aTangent;
layout(location = 5) in ivec4 boneIDs;   // do 4 kości wpływających na wierzchołek
layout(location = 6) in vec4 boneWeights;

out vec4 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec3 VertColor;
// Baza TBN w world-space dla normal mappingu (kolumny: tangent, bitangent, normal).
out mat3 TBN;

// Offset palety kości TEGO obiektu w buforze BoneMatrices. Bufor niesie palety WSZYSTKICH
// skeletal meshy klatki, wysłane jednym uploadem (Renderer::UploadSkeletalPalettes) — dawniej
// każdy obiekt nadpisywał wspólny bufor tuż przed swoim rysowaniem, raz na kaskadę cieni
// i raz na pass główny. Uniform sterowany przez silnik, nie parametr materiału.
uniform int paletteBaseIndex;

uniform mat4 model;
// Normal matrix computed on the CPU (transpose(inverse(model)), set by the Renderer
// alongside "model") — no per-vertex inverse() on the GPU. Covers only the model transform;
// the skinning rotation is applied separately in main().
uniform mat4 normalMatrix;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    mat4 skinMatrix =
          boneWeights.x * finalBoneMatrix[paletteBaseIndex + boneIDs.x]
        + boneWeights.y * finalBoneMatrix[paletteBaseIndex + boneIDs.y]
        + boneWeights.z * finalBoneMatrix[paletteBaseIndex + boneIDs.z]
        + boneWeights.w * finalBoneMatrix[paletteBaseIndex + boneIDs.w];

    vec4 skinnedPos = skinMatrix * vec4(aPos, 1.0);
    gl_Position = projection * view * model * skinnedPos;
    FragPos = model * skinnedPos;

    mat3 nMat = mat3(normalMatrix);

    // Normals and tangents must follow the skinning rotation too — otherwise they stay in
    // bind-pose mesh space. On rigs whose bones carry an axis conversion (Mixamo/Blender
    // Z-up -> Y-up) that is a constant 90 deg error, not just a missing animation rotation.
    // The palette is rigid + uniform scale, so its 3x3 works for normals; normalize() drops the scale.
    mat3 skin3 = mat3(skinMatrix);

    vec3 N = normalize(nMat * (skin3 * aNormal));
    vec3 T = normalize(nMat * (skin3 * aTangent.xyz));
    // Re-ortogonalizacja Grama-Schmidta (T może nie być prostopadłe do N po interpolacji/skalowaniu).
    T = normalize(T - dot(T, N) * N);
    // Handedness z w decyduje o kierunku bitangentu (mirrored UV).
    vec3 B = cross(N, T) * aTangent.w;

    Normal = N;
    TBN = mat3(T, B, N);

    TexCoord = aTexCoord;
    VertColor = aVertColor;
}
