#version 450

// Depth-only shader dla shadow passa instancingu static meshy — instanced odpowiednik
// OnlyPosition.vert. Ten sam blok InstanceMatrices, ten sam instanceBaseIndex, bajtowo identyczny
// InstanceData co BasicVertInstanced.vert — jeden InstanceGPUData i jeden SSBO (Renderer::
// mInstanceBuffer, binding 1) obsługują oba passy w tej samej klatce.
struct InstanceData { mat4 model; mat4 normalMatrix; };
layout(std430, binding = 1) buffer InstanceMatrices { InstanceData instances[]; };

// Skompaktowane indeksy instancji, które przeżyły culling danej kaskady (Renderer::
// CullShadowCasters, binding 3). Dzięki tej jednej pośredniości kaskada rysuje PODZBIÓR batcha
// bez ruszania danych instancji: 4 B na przeżywającą instancję zamiast przepakowywania 128 B.
layout(std430, binding = 3) buffer VisibleInstanceIndices { uint visibleIndices[]; };

// UWAGA: w TYM shaderze instanceBaseIndex indeksuje visibleIndices, nie instances — pass główny
// (BasicVertInstanced.vert) używa go dalej jako offsetu w instances.
uniform int instanceBaseIndex;
// UWAGA: celowo BRAK `uniform mat4 model` — patrz BasicVertInstanced.vert.

layout (location = 0) in vec3 aPos;

// Widok i projekcja OSOBNO, nie premnożone na CPU — dokładnie te same uniformy i ta sama
// kolejność mnożeń co w BasicVertInstanced.vert. Dzięki temu głębia z tego shadera i głębia
// z passa oświetlenia są tą samą liczbą, a nie dwiema różniącymi się o kilka ulp (to był powód,
// dla którego depth prepass nie mógł karmić early-Z). Pass cieni podstawia tu macierze kaskady.
uniform mat4 view;
uniform mat4 projection;

void main() {
    mat4 model = instances[visibleIndices[instanceBaseIndex + gl_InstanceID]].model;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
