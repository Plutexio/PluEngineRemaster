#version 450 core

// ==========================================================
// CelShaded — klasyczny cel/toon shading z pełną obsługą cieni.
//
// Różnica wobec StylizedLit.frag (płaski Lambert): tam N·L leci gładkim gradientem, tu jest
// KWANTOWANE do `shadeSteps` pasów, a cień jest twardy — próg zamiast miękkiego PCF. To te dwie
// rzeczy robią komiksowy look; reszta (albedo, normal map, ambient) jest wspólna.
//
// Cienie są te SAME co w PBR.frag / StylizedLit.frag (kaskady kierunkowe + atlas spotów, te same
// bloki UBO/SSBO i ten sam filtr PCF) — filtr liczy się normalnie, a dopiero JEGO WYNIK jest
// progowany. Dzięki temu `ShadowPcfRadius` na świetle dalej steruje tym, jak bardzo poszarpana
// jest krawędź cienia, mimo że sam cień jest binarny.
//
// Silnik nie ma mechanizmu #include dla GLSL, więc deklaracje bloków i funkcje filtrujące są tu
// POWIELONE z PBR.frag — zmieniając format ShadowData / SpotLightGPU / sloty tekstur, zmień
// WSZYSTKIE trzy pliki (PBR.frag, StylizedLit.frag, CelShaded.frag).
// ==========================================================

out vec4 FragColor;

// ==========================================================
// Wejścia z BasicVert.vert / BasicVertInstanced.vert / BasicVertSkeletal.vert
// ==========================================================
in vec4 FragPos;     // pozycja fragmentu w world-space
in vec3 Normal;      // normalna geometryczna (world-space)
in vec2 TexCoord;    // UV
in vec3 VertColor;   // kolor wierzchołka
in mat3 TBN;         // baza tangent->world dla normal mappingu

// ==========================================================
// Uniformy silnika (engine-only — NIE są parametrami materiału)
// ==========================================================
uniform vec3 cameraPos;
uniform vec3 dirLightDir;   // kierunek LOTU światła (ku światłu = -dirLightDir)
uniform vec4 dirLightColor; // rgb = kolor, w = intensywność
uniform mat4 view;
uniform mat4 projection;    // contact shadows: rzut próbek promienia na ekran          // głębia w przestrzeni kamery — wybór kaskady

// ==========================================================
// Cienie kaskadowe (CSM) — mirror bloku z PBR.frag, slot 15.
// ==========================================================
#define MAX_CASCADE_COUNT 6

// One cascade — mirrors Plu::ShadowCascadeGPU (see PBR.frag for why it is a struct).
struct ShadowCascade
{
    mat4 viewProj;
    vec4 atlasScaleBias;          // atlasUV = projCoords.xy * xy + zw
    vec4 params;                  // x = split, y = texel size (m), z = depth bias [0,1], w unused
};

layout(std140, binding = 2) uniform ShadowData
{
    ShadowCascade cascades[MAX_CASCADE_COUNT];
    vec2  invAtlasSize;           // 1 / atlas size; one atlas texel IS one cascade texel
    int   cascadeCount;           // 0 = brak cieni kierunkowych w tej klatce
    float shadowFadeStart;
    float shadowFadeEnd;
    float cascadeBlendFraction;
    float normalBiasScale;
    float pcfRadiusTexels;
    int   debugVisualizeCascades;
    int   pcfTapCount;
    int   pcfRotateSamples;
    int   contactShadowSteps;     // 0 = contact shadows wyłączone
    float contactShadowLength;    // metry marszu promienia
    float contactShadowThickness; // metry — zakładana grubość okludera
    float contactShadowBias;      // metry — odsuwa start promienia od własnej powierzchni
};

#define MAX_PCF_TAPS 32
// Górny limit kroków contact shadows; musi odpowiadać Plu::kMaxContactShadowSteps.
#define MAX_CONTACT_STEPS 64

// Głębia sceny z depth prepassa — zwykła tekstura głębi (BEZ samplera porównującego), slot 13.
// Osobny obiekt od bufora głębi passa oświetlenia: samplowanie własnego załącznika to feedback
// loop. Patrz komentarz przy sceneDepthTexture w PBR.frag.
layout(binding = 13) uniform sampler2D sceneDepthTexture;

// A plain 2D texture, not an array: every cascade is a rectangle of one atlas, which is what
// lets a near cascade be 2048² while a far one is 512².
layout(binding = 15) uniform sampler2DShadow shadowCascades;

// ==========================================================
// Światła stożkowe (SpotLight) — mirror bloków z PBR.frag, atlas na slocie 14.
// ==========================================================
layout(std140, binding = 4) uniform SpotLightData
{
    int   spotLightOffset;
    int   spotLightCount;          // 0 = brak świateł stożkowych w tej klatce
    float invSpotShadowResolution;
    int   spotShadowSlotCount;
};

struct SpotLightGPU
{
    mat4  shadowViewProj;         // identity, gdy światło nie dostało slotu
    vec3  position;
    float range;
    vec3  direction;              // kierunek LOTU światła
    float innerConeCos;
    vec3  color;                  // kolor * intensywność, premultiplied na CPU
    float outerConeCos;
    int   shadowSlot;             // -1 = brak mapy cienia w tej klatce
    float depthBias;              // METRY (przeliczane przez depthBiasScale)
    float normalBias;
    float pcfRadius;
    float texelWorldPerMetre;
    int   pcfTaps;
    float depthBiasScale;         // f*n/(f-n) — przelicza depthBias z metrów na głębię [0,1]
    float padding;
};

layout(std430, binding = 5) readonly buffer SpotLights       { SpotLightGPU spotLights[]; };
layout(std430, binding = 6) readonly buffer SpotLightIndices { uint spotLightIndices[]; };

layout(binding = 14) uniform sampler2DArrayShadow spotShadowMaps;

// ==========================================================
// Parametry materiału (auto-wykrywane przez ShaderCodeParser)
//
// Uwaga na typy: RenderFromMaterial ma settery dla sampler2D / float / vec3 (oraz bool, którego
// używa PBR na flagach map). Dlatego `shadeSteps` jest FLOATEM, nie intem — mimo że semantycznie
// to liczba całkowita, zaokrąglana niżej.
// ==========================================================
uniform sampler2D albedoMap;
uniform sampler2D normalMap;

uniform bool useAlbedoMap;
uniform bool useNormalMap;

uniform vec3  albedoColor;     // tint albedo (i kolor bazowy, gdy brak mapy)
uniform float normalStrength;  // siła normal mappingu

// Liczba pasów jasności, clampowana do [2, 8]. 2 = twardy podział na światło i cień
// (najmocniejszy komiks), 4-5 = łagodniejszy, ilustracyjny.
uniform float shadeSteps;

// Szerokość przejścia na granicy pasa, w ułamku szerokości pasa [0..0.5]. 0 = idealnie twardy
// schodek (ładny na stopklatce, ale migocze przy ruchu kamery); ~0.03 wygładza go do mniej
// więcej jednego piksela, nie psując komiksowego wrażenia. Ten sam próg miękczy krawędź cienia
// i rim light, żeby cały shader miał jedną, spójną "twardość".
uniform float bandSoftness;

// Kolor strony zacienionej: mnoży ambient, czyli JEDYNE światło docierające tam, gdzie nie sięga
// żaden light. Chłodny błękit tutaj to najtańszy sposób na czytelny cel look.
uniform vec3 shadowTint;

// Podświetlenie krawędzi sylwetki. rimColor = czerń wyłącza efekt całkowicie.
uniform vec3  rimColor;
uniform float rimPower;   // wyższy = węższa obwódka

uniform vec3 ambientColor;

// ----------------------------------------------------------
// Cienie — kod identyczny z PBR.frag (patrz uwaga o braku #include na górze pliku).
// ----------------------------------------------------------

vec2 VogelDiskSample(int index, int count, float phase)
{
    const float kGoldenAngle = 2.39996323;
    float radius = sqrt((float(index) + 0.5) / float(count));
    float theta  = float(index) * kGoldenAngle + phase;
    return radius * vec2(cos(theta), sin(theta));
}

float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float TapShadow(vec2 atlasUv, float refDepth)
{
    return texture(shadowCascades, vec3(atlasUv, refDepth));
}

float FilterCascade(int cascade, vec3 worldPos, vec3 normal, float slope)
{
    float texelWorld  = cascades[cascade].params.y;
    float offsetScale = texelWorld * normalBiasScale * clamp(0.5 + slope, 0.5, 3.0);
    vec3  offsetPos   = worldPos + normal * offsetScale;

    // Projekcja kaskady jest ortho, więc w == 1 — dzielenie perspektywiczne jest zbędne.
    vec4 fragPosLS  = cascades[cascade].viewProj * vec4(offsetPos, 1.0);
    vec3 projCoords = fragPosLS.xyz * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 1.0;
    // A cascade's neighbour in the atlas is ANOTHER cascade, not the sampler border, so
    // falling off the edge has to be rejected explicitly instead of reading a stranger.
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0))))
        return 1.0;

    vec2 atlasScale  = cascades[cascade].atlasScaleBias.xy;
    vec2 atlasOffset = cascades[cascade].atlasScaleBias.zw;
    vec2 atlasUv     = projCoords.xy * atlasScale + atlasOffset;

    float refDepth = projCoords.z - cascades[cascade].params.z * (1.0 + 2.0 * slope);

    int taps = clamp(pcfTapCount, 1, MAX_PCF_TAPS);
    if (taps == 1 || pcfRadiusTexels <= 0.0)
    {
        return TapShadow(atlasUv, refDepth);
    }

    // Keep the disk inside this cascade's rectangle, with half a texel of margin because the
    // hardware tap is bilinear and reaches into the neighbouring texel on its own.
    vec2 clampMin = atlasOffset + 0.5 * invAtlasSize;
    vec2 clampMax = atlasOffset + atlasScale - 0.5 * invAtlasSize;

    vec2  radiusUV = pcfRadiusTexels * invAtlasSize;
    float phase    = (pcfRotateSamples != 0)
                   ? InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530
                   : 0.0;

    float shadow = 0.0;
    for (int i = 0; i < taps; i++)
    {
        vec2 sampleUv = atlasUv + VogelDiskSample(i, taps, phase) * radiusUV;
        shadow += TapShadow(clamp(sampleUv, clampMin, clampMax), refDepth);
    }
    return shadow / float(taps);
}

int SelectCascade(float depthView)
{
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount - 1; i++)
    {
        if (depthView < cascades[i].params.x) { return i; }
    }
    return cascade;
}


// Contact shadows — patrz obszerny komentarz w PBR.frag. Krótki promień przez głębię sceny,
// w jednostkach PIKSELA EKRANU zamiast teksela kaskady, więc łapie detal, którego kaskada nie
// rozdziela. Widzi wyłącznie to, co jest w buforze głębi — to dodatek do kaskad, nie zamiennik.
float LinearizeSceneDepth(float depth01)
{
    float ndcZ = depth01 * 2.0 - 1.0;
    return projection[3][2] / (ndcZ + projection[2][2]);
}

// World size of one screen pixel at view depth `depthView`. See PBR.frag.
float PixelWorldSize(float depthView)
{
    return 2.0 * depthView / (projection[1][1] * float(textureSize(sceneDepthTexture, 0).y));
}

float ContactShadow(vec3 worldPos, vec3 normal, vec3 L, float slope, float depthView)
{
    if (contactShadowSteps <= 0) return 1.0;

    // Distance fade by the pixels the ray spans — past ~2 px it only hits its own surface and
    // darkens everything far away. The bias grows to one pixel for the same reason. See PBR.frag.
    float pixelWorld = PixelWorldSize(depthView);
    float distanceFade = smoothstep(2.0, 4.0, contactShadowLength / pixelWorld);
    if (distanceFade <= 0.0) return 1.0;
    float bias = max(contactShadowBias, pixelWorld);

    // Wygaszanie przy świetle STYCZNYM — tam o trafieniu decyduje dyskretyzacja bufora głębi,
    // a nie geometria (resztka acne, której bias nie usuwa), a N·L jest i tak bliskie zeru.
    // Patrz PBR.frag.
    float grazingFade = smoothstep(0.0, 0.3, dot(normal, L));
    if (grazingFade <= 0.0) return 1.0;

    // Start odsunięty wzdłuż NORMALNEJ, skalowany tan(θ) — przy świetle stycznym offset wzdłuż
    // samego promienia nie oddala go od powierzchni i promień łapie własną geometrię (acne).
    // Patrz PBR.frag.
    vec3 originWorld = worldPos + normal * (bias * (1.0 + 2.0 * slope));

    vec3 rayOriginView = (view * vec4(originWorld, 1.0)).xyz;
    vec3 rayDirView    = normalize(mat3(view) * L);
    rayOriginView += rayDirView * bias;

    int   steps  = min(contactShadowSteps, MAX_CONTACT_STEPS);
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy);

    for (int i = 1; i <= steps; i++)
    {
        // Rozkład KWADRATOWY — zagęszcza próbki przy powierzchni, gdzie leżą detale. Przy równych
        // krokach rozdzielczość promienia (length/steps) przekracza rozmiar detalu i promień nad
        // nim przeskakuje. Patrz PBR.frag.
        float t = (float(i) - 0.5 + jitter) / float(steps);
        vec3 samplePosView = rayOriginView + rayDirView * (contactShadowLength * t * t);

        vec4 clip = projection * vec4(samplePosView, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;

        float diff = -samplePosView.z - LinearizeSceneDepth(texture(sceneDepthTexture, uv).r);
        if (diff > 0.0 && diff < contactShadowThickness)
        {
            return mix(1.0, smoothstep(0.75, 1.0, t), grazingFade * distanceFade);
        }
    }
    return 1.0;
}

float ShadowVisibility(vec3 worldPos, vec3 normal, float depthView, float slope)
{
    float contact = ContactShadow(worldPos, normal, normalize(-dirLightDir), slope, depthView);

    if (cascadeCount <= 0) return contact;

    int   cascade    = SelectCascade(depthView);
    float visibility = FilterCascade(cascade, worldPos, normal, slope);

    if (cascade < cascadeCount - 1 && cascadeBlendFraction > 0.0)
    {
        float rangeStart = (cascade == 0) ? 0.0 : cascades[cascade - 1].params.x;
        float rangeEnd   = cascades[cascade].params.x;
        float bandStart  = mix(rangeEnd, rangeStart, cascadeBlendFraction);
        float t = clamp((depthView - bandStart) / max(rangeEnd - bandStart, 1e-4), 0.0, 1.0);
        if (t > 0.0)
        {
            visibility = mix(visibility, FilterCascade(cascade + 1, worldPos, normal, slope), t);
        }
    }

    float fade = clamp((depthView - shadowFadeStart) / max(shadowFadeEnd - shadowFadeStart, 1e-4), 0.0, 1.0);
    visibility = mix(visibility, 1.0, fade);

    // Najciemniejszy wygrywa — mnożenie podwajałoby zaciemnienie tam, gdzie kaskada i promień
    // widzą ten sam okluder, obrysowując każdy detal czarną obwódką.
    return min(visibility, contact);
}

float SpotShadowVisibility(SpotLightGPU light, vec3 worldPos, vec3 normal, float dist, float slope)
{
    if (light.shadowSlot < 0) return 1.0;

    float texelWorld  = dist * light.texelWorldPerMetre;
    float offsetScale = texelWorld * light.normalBias * clamp(0.5 + slope, 0.5, 3.0);
    vec3  offsetPos   = worldPos + normal * offsetScale;

    vec4 fragPosLS = light.shadowViewProj * vec4(offsetPos, 1.0);
    if (fragPosLS.w <= 0.0) return 1.0;

    vec3 projCoords = (fragPosLS.xyz / fragPosLS.w) * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 1.0;

    // Bias jest autorowany w METRACH, a bufor głębi projekcji perspektywicznej jest nieliniowy
    // (z01 = f/(f-n) * (1 - n/d)), więc stała wartość w [0,1] znaczyłaby co innego na każdym
    // dystansie — przy 15 m stożku już 5 m od lampy odpowiadałaby ~0.75 m przesunięcia w świecie
    // i cień po prostu przestawałby istnieć. Dokładna pochodna dz01/dd = f*n/((f-n)*d^2) wraca
    // do stałego dystansu fizycznego. `fragPosLS.w` to dokładnie głębia widokowa d (macierz
    // perspektywiczna ma w wierszu w (0,0,-1,0)), więc nie trzeba jej niczym przybliżać.
    float viewDepth = fragPosLS.w;
    float bias01    = light.depthBias * light.depthBiasScale / max(viewDepth * viewDepth, 1e-4);
    float refDepth  = projCoords.z - bias01 * (1.0 + 2.0 * slope);

    int taps = clamp(light.pcfTaps, 1, MAX_PCF_TAPS);
    if (taps == 1 || light.pcfRadius <= 0.0)
    {
        return texture(spotShadowMaps, vec4(projCoords.xy, float(light.shadowSlot), refDepth));
    }

    vec2  radiusUV = vec2(light.pcfRadius * invSpotShadowResolution);
    float phase    = (pcfRotateSamples != 0)
                   ? InterleavedGradientNoise(gl_FragCoord.xy) * 6.28318530
                   : 0.0;

    float shadow = 0.0;
    for (int i = 0; i < taps; i++)
    {
        vec2 uv = projCoords.xy + VogelDiskSample(i, taps, phase) * radiusUV;
        shadow += texture(spotShadowMaps, vec4(uv, float(light.shadowSlot), refDepth));
    }
    return shadow / float(taps);
}

float SlopeFromNdotL(float NdotL)
{
    float cosT = max(NdotL, 0.1);
    return min(sqrt(1.0 - cosT * cosT) / cosT, 5.0);
}

// ----------------------------------------------------------
// Kwantyzacja — serce cel shadingu
// ----------------------------------------------------------

// Przejście przez próg 0.5, zmiękczone o `softness`. `softness == 0` daje czysty `step`
// (smoothstep z równymi krawędziami to dzielenie przez zero, stąd jawna gałąź).
//
// Dlaczego stała szerokość, a nie `fwidth`: analityczny antialiasing pochodnymi byłby ostrzejszy,
// ale funkcje pochodnych w NIEJEDNORODNYM przepływie sterowania mają wynik niezdefiniowany —
// a pętla po spotach niżej jest pełna `continue`, więc każdy fragment przechodzi ją inaczej.
// Stała szerokość jest deterministyczna wszędzie i przy okazji jest suwakiem dla artysty.
float SoftStep(float value, float softness)
{
    if (softness <= 0.0) return step(0.5, value);
    return smoothstep(0.5 - softness, 0.5 + softness, value);
}

// Kwantuje N·L do `steps` pasów rozpiętych na [0,1].
//
// Granice pasów wypadają na PÓŁ-całkowitych wartościach `scaled`, nie na całkowitych — dlatego
// `floor` + próg 0.5 zamiast prostego `floor(NdotL*steps)/steps`. Przy tamtym wariancie i steps=2
// granica lądowałaby dokładnie na N·L = 1, czyli obiekt byłby jednolicie czarny; tutaj ląduje na
// 0.5, czyli tam, gdzie oko spodziewa się terminatora.
float CelRamp(float NdotL, float steps, float softness)
{
    float divisions = max(steps - 1.0, 1.0);
    float scaled    = clamp(NdotL, 0.0, 1.0) * divisions;
    float lower     = floor(scaled);
    float t         = SoftStep(scaled - lower, softness);
    return (lower + t) / divisions;
}

void main()
{
    float steps    = clamp(floor(shadeSteps + 0.5), 2.0, 8.0);
    float softness = clamp(bandSoftness, 0.0, 0.5);

    // --- Albedo ---
    vec3 albedo = albedoColor;
    if (useAlbedoMap)
    {
        vec3 texel = texture(albedoMap, TexCoord).rgb;
        // Stopgap dopóki silnik nie wgrywa albedo w GL_SRGB8_ALPHA8 (jak w PBR.frag).
        texel = pow(texel, vec3(2.2));
        albedo *= texel;
    }

    // --- Normalna ---
    vec3 N;
    if (useNormalMap)
    {
        vec3 tangentNormal = texture(normalMap, TexCoord).xyz * 2.0 - 1.0;
        tangentNormal.xy *= normalStrength;
        N = normalize(TBN * tangentNormal);
    }
    else
    {
        N = normalize(Normal);
    }

    vec3 V = normalize(cameraPos - FragPos.xyz);

    // Głębia w przestrzeni widoku — trzeci wiersz macierzy view × FragPos (bez pełnego mnożenia
    // mat4 × vec4). Steruje wyborem kaskady, więc liczona przed gałęzią światła.
    float depthView = abs(dot(vec4(view[0][2], view[1][2], view[2][2], view[3][2]), FragPos));

    // ----------------------------------------------------------
    // Światło kierunkowe — pasmowe N·L razy twardy cień.
    // ----------------------------------------------------------
    vec3 Lo = vec3(0.0);
    {
        vec3  L = normalize(-dirLightDir);
        float NdotL = max(dot(N, L), 0.0);

        // Early-out: cały wkład jest mnożony przez rampę, która dla NdotL == 0 wynosi 0 —
        // nie ma po co próbkować cienia dla odwróconej połowy obiektu.
        if (NdotL > 0.0)
        {
            vec3  radiance = dirLightColor.rgb * dirLightColor.w;
            float ramp     = CelRamp(NdotL, steps, softness);
            // Filtr PCF liczy się w pełni, a progowany jest dopiero jego WYNIK — dzięki temu
            // ShadowPcfRadius na świetle dalej decyduje o postrzępieniu krawędzi.
            float shadow   = SoftStep(ShadowVisibility(FragPos.xyz, N, depthView, SlopeFromNdotL(NdotL)), softness);

            Lo += albedo * radiance * (ramp * shadow);
        }
    }

    // ----------------------------------------------------------
    // Światła stożkowe — ta sama pętla i te same trzy wczesne odrzucenia co w PBR.frag.
    // Tłumienie i stożek zostają CIĄGŁE (nie kwantujemy ich): pasmowanie zasięgu światła daje
    // koncentryczne obręcze na podłodze, co wygląda na błąd, a nie na styl. Kwantowany jest
    // wyłącznie kąt padania — czyli to, co rysuje kształt bryły.
    // ----------------------------------------------------------
    for (int i = 0; i < spotLightCount; i++)
    {
        SpotLightGPU light = spotLights[spotLightIndices[spotLightOffset + i]];

        vec3  toLight = light.position - FragPos.xyz;
        float distSq  = dot(toLight, toLight);
        if (distSq > light.range * light.range) continue;   // #1 poza zasięgiem

        float dist = sqrt(distSq);
        vec3  L    = toLight / max(dist, 1e-4);

        float cosAngle = dot(-L, light.direction);
        if (cosAngle <= light.outerConeCos) continue;       // #2 poza stożkiem

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;                         // #3 odwrócone od światła

        float rangeRatio  = dist / max(light.range, 1e-4);
        float window      = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
        float attenuation = (1.0 / (distSq + 1e-4)) * window * window;

        float spot   = smoothstep(light.outerConeCos, light.innerConeCos, cosAngle);
        float ramp   = CelRamp(NdotL, steps, softness);
        float shadow = SoftStep(SpotShadowVisibility(light, FragPos.xyz, N, dist, SlopeFromNdotL(NdotL)), softness);

        Lo += albedo * (light.color * attenuation * spot) * (ramp * shadow);
    }

    // Ambient tintowany kolorem cienia — to jedyne światło po ciemnej stronie, więc pokolorowanie
    // go JEST pokolorowaniem cienia, bez osobnego mnożnika doklejanego po fakcie.
    vec3 color = albedo * ambientColor * shadowTint + Lo;

    // Rim light — twarda obwódka na krawędzi sylwetki, ta sama "twardość" co pasy.
    float rimRaw = pow(1.0 - max(dot(N, V), 0.0), max(rimPower, 0.01));
    color += rimColor * SoftStep(rimRaw, softness);

    // Debug: pokoloruj pikselom przypisaną kaskadę (View → ustawienia sceny) — parytet z PBR,
    // żeby diagnostyka cieni działała niezależnie od tego, który shader nosi obiekt.
    if (debugVisualizeCascades != 0 && cascadeCount > 0)
    {
        const vec3 kCascadeTint[MAX_CASCADE_COUNT] = vec3[](
            vec3(1.0, 0.4, 0.4), vec3(0.4, 1.0, 0.4),
            vec3(0.4, 0.6, 1.0), vec3(1.0, 1.0, 0.4),
            vec3(1.0, 0.5, 1.0), vec3(0.4, 1.0, 1.0)
        );
        color *= kCascadeTint[SelectCascade(depthView)];
    }

    // Bufor główny jest liniowy (RGBA8), więc korekcja gamma jest obowiązkowa. Tonemappingu
    // (Reinhard w PBR.frag) tu ŚWIADOMIE NIE MA — rolowałby jasne pasy ku bieli i zlewał je
    // ze sobą, kasując dokładnie ten podział, który jest sensem tego shadera.
    color = clamp(color, 0.0, 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
