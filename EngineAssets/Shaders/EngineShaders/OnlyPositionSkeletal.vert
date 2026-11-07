#version 450

// Depth-only shader dla shadow passa skeletal meshy — skinowany odpowiednik OnlyPosition.vert.
// Paleta kości ta sama co w BasicVertSkeletal.vert (SSBO binding 0), by cień pokrywał się z animacją.
layout(std430, binding = 0) buffer BoneMatrices {
    mat4 finalBoneMatrix[];
};

layout (location = 0) in vec3 aPos;
layout (location = 5) in ivec4 boneIDs;   // do 4 kości wpływających na wierzchołek
layout (location = 6) in vec4  boneWeights;

// Offset palety kości TEGO obiektu w buforze BoneMatrices. Bufor niesie palety WSZYSTKICH
// skeletal meshy klatki, wysłane jednym uploadem (Renderer::UploadSkeletalPalettes) — dawniej
// każdy obiekt nadpisywał wspólny bufor tuż przed swoim rysowaniem, raz na kaskadę cieni
// i raz na pass główny. Uniform sterowany przez silnik, nie parametr materiału.
uniform int paletteBaseIndex;

uniform mat4 model;
// Osobno, nie premnożone — patrz komentarz w OnlyPositionInstanced.vert. Kolejność mnożeń jak
// w BasicVertSkeletal.vert.
uniform mat4 view;
uniform mat4 projection;

void main() {
    mat4 skinMatrix =
          boneWeights.x * finalBoneMatrix[paletteBaseIndex + boneIDs.x]
        + boneWeights.y * finalBoneMatrix[paletteBaseIndex + boneIDs.y]
        + boneWeights.z * finalBoneMatrix[paletteBaseIndex + boneIDs.z]
        + boneWeights.w * finalBoneMatrix[paletteBaseIndex + boneIDs.w];

    vec4 skinnedPos = skinMatrix * vec4(aPos, 1.0);
    gl_Position = projection * view * model * skinnedPos;
}
