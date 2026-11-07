import argparse
from pathlib import Path
import os
import re

parser = argparse.ArgumentParser()

parser.add_argument("--project")
parser.add_argument("--engine")
parser.add_argument("--file")

args = parser.parse_args()

if not args.project or not args.engine:
    parser.print_help()
    exit(1)
projectPath = Path(args.project)
enginePath = Path(args.engine)
filePath: Path = Path("")
fileMode: bool = False
if args.file:
    filePath = args.file
    fileMode = True
    print(f"File: {filePath}")

print(f"Parsing shaders at: {projectPath}/Shaders")
print(f"Parsing Engine shaders at: {enginePath}/Shaders")

engineShadersPath = os.path.join(enginePath, "Shaders")
projectShadersPath = os.path.join(projectPath, "Shaders")
projectCachePath = os.path.join(projectPath, "Cache")

foundShaders: list[Path] = []

if not fileMode:
    for subdir, dirs, files in os.walk(projectShadersPath):
        for file in files:
            shaderPath = os.path.join(subdir, file)
            if shaderPath.endswith((".frag", ".vert")):
                foundShaders.append(Path(shaderPath))
                print(f"Shader {shaderPath}")
    for subdir, dirs, files in os.walk(engineShadersPath):
        for file in files:
            shaderPath = os.path.join(subdir, file)
            if shaderPath.endswith((".frag", ".vert")):
                foundShaders.append(Path(shaderPath))
                print(f"Engine Shader {shaderPath}")
else:
    foundShaders.append(Path(filePath))

uniformPattern = r'uniform\s+(\w+)\s+(\w+)(?:\[(\d+)\])?\s*;'

engineOnlyUniforms = [
    {"mat4", "model"},
    # Macierz normalnych (transpose(inverse(model))) ustawiana przez Renderer razem z "model"
    # (BasicVert.vert / BasicVertSkeletal.vert) — nie parametr materiału. mat4 i tak nie ma
    # settera w RenderFromMaterial, ale nie zaśmiecamy plików ShaderParse.
    {"mat4", "normalMatrix"},
    {"mat4", "view"},
    {"mat4", "projection"},
    {"vec3", "cameraPos"},
    {"float", "time"},
    # Indeksy sterowane przez silnik per draw: offset batcha w buforze widocznych instancji
    # (BasicVertInstanced.vert) i offset palety kości (BasicVertSkeletal.vert). Jako parametr
    # materiału zostałyby nadpisane wartością z assetu — cały batch narysowałby się transformem
    # pierwszej instancji, a mesh skeletalny paletą pierwszego obiektu.
    {"int", "instanceBaseIndex"},
    {"int", "paletteBaseIndex"},
    {"vec4", "dirLightColor"},
    {"vec3", "dirLightDir"},
    # Atlas map cieni kaskadowych (CSM) — bindowany przez silnik na stały slot 15
    # (Renderer::RenderSnapshot), nie jest parametrem materiału. Regex łapie ją mimo prefiksu
    # layout(binding = 0). Reszta parametrów cieni (macierze, splity, biasy, cascadeCount) żyje
    # w bloku UBO `ShadowData`, którego to wyrażenie i tak nie widzi: dopasowuje wyłącznie
    # luźne `uniform T nazwa;`, a nie członków bloku.
    {"sampler2DShadow", "shadowCascades"},
    # Atlas cieni świateł stożkowych — bindowany przez silnik na stały slot 14
    # (Renderer::RenderSnapshot), nie jest parametrem materiału. To JEDYNY luźny uniform sekcji
    # spotów: reszta ich danych żyje w blokach SpotLightData / SpotLights / SpotLightIndices,
    # których to wyrażenie nie widzi (dopasowuje wyłącznie `uniform T nazwa;`). Bez tego wpisu
    # RenderFromMaterial nadpisałby sampler zserializowaną wartością materiału w środku klatki.
    {"sampler2DArrayShadow", "spotShadowMaps"},
    # Głębia sceny z depth prepassa (contact shadows) — bindowana przez silnik na stały slot 13
    # (Renderer::RenderSnapshot), nie jest parametrem materiału. Bez tego wpisu RenderFromMaterial
    # nadpisałby sampler zserializowaną wartością materiału w środku klatki, tak samo jak zrobiłby
    # to z mapami cieni wyżej.
    {"sampler2D", "sceneDepthTexture"},
    # Offset batcha w SSBO InstanceMatrices — sterowany przez silnik per draw call
    # (Renderer::RenderSnapshot), nie parametr materiału. Bez tego pominięcia RenderFromMaterial
    # nadpisuje go w środku klatki zserializowaną wartością materiału, psując każdy batch poza
    # pierwszym (dokładnie ten błąd, który cascadeCount obchodzi wyżej).
    {"int", "instanceBaseIndex"},
    # Offset palety kości w SSBO BoneMatrices — jak wyżej, sterowany przez silnik per obiekt.
    # BasicVertSkeletal.vert jest programem REALNYCH materiałów, więc bez tego pominięcia
    # RenderFromMaterial nadpisałoby offset zserializowaną wartością materiału i każdy skeletal
    # mesh poza pierwszym skinowałby się cudzą paletą.
    {"int", "paletteBaseIndex"},
]

for shader in foundShaders:
    with open(shader, "r", encoding="utf-8") as s:
        lines = s.readlines()
        fullFile: str = ""
        for line in lines:
            fullFile += line
        uniforms = re.findall(uniformPattern, fullFile)
        if not os.path.exists(os.path.join(projectCachePath, "ShaderParse/")):
            os.makedirs(os.path.join(projectCachePath, "ShaderParse/"))
        with open(os.path.join(projectCachePath, "ShaderParse", shader.name + ".txt"), "w") as r:
            for uniform in uniforms:
                if {uniform[0], uniform[1]} not in engineOnlyUniforms:
                    r.write(f"{uniform[0]} {uniform[1]} {uniform[2]}\n")

