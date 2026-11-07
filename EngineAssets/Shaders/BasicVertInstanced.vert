#version 450 core

// Binding 1 — binding 0 należy do palety kości (BoneMatrices, BasicVertSkeletal.vert).
// Layout MUSI odpowiadać Plu::InstanceGPUData (RenderThreading.h).
struct InstanceData { mat4 model; mat4 normalMatrix; };
layout(std430, binding = 1) buffer InstanceMatrices { InstanceData instances[]; };

// Skompaktowane indeksy instancji, które przeżyły culling danego frustum (Renderer::
// CullShadowCasters, binding 3). KAŻDY pass rysujący static meshe — główny, depth prepass i mapy
// cieni — adresuje instancje przez tę samą indirekcję, więc ten sam vertex shader obsługuje je
// wszystkie (patrz Renderer::ResolveDepthProgram): pass głębi rysuje podzbiór batcha nie ruszając
// danych instancji, a pass główny widzi dokładnie te same macierze.
layout(std430, binding = 3) buffer VisibleInstanceIndices { uint visibleIndices[]; };

// Bufor bindujemy w całości (BindBase), więc offset batcha idzie uniformem, nie BindRange.
// UWAGA: instanceBaseIndex indeksuje visibleIndices, NIE instances.
uniform int instanceBaseIndex;
// UWAGA: celowo BRAK `uniform mat4 model` — model idzie z InstanceMatrices[visibleIndices[...]].

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aVertColor;
// Tangent spakowany 10_10_10_2: xyz = tangent, w = znak bitangentu (handedness).
layout (location = 4) in vec4 aTangent;

out vec4 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec3 VertColor;
// Baza TBN w world-space dla normal mappingu (kolumny: tangent, bitangent, normal).
out mat3 TBN;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    InstanceData inst = instances[visibleIndices[instanceBaseIndex + gl_InstanceID]];
    mat4 model = inst.model;

    gl_Position = projection * view * model * vec4(aPos, 1.0);
    FragPos = model * vec4(aPos, 1.0);

    // Macierz normalnych policzona na CPU (transpose(inverse(model)), patrz InstanceGPUData) —
    // odporna na niejednorodne skalowanie modelu, bez per-wierzchołkowego inverse() na GPU.
    mat3 normalMatrix = mat3(inst.normalMatrix);

    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent.xyz);
    // Re-ortogonalizacja Grama-Schmidta (T może nie być prostopadłe do N po interpolacji/skalowaniu).
    T = normalize(T - dot(T, N) * N);
    // Handedness z w decyduje o kierunku bitangentu (mirrored UV).
    vec3 B = cross(N, T) * aTangent.w;

    Normal = N;
    TBN = mat3(T, B, N);

    TexCoord = aTexCoord;
    VertColor = aVertColor;
}
