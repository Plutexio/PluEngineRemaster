#version 450 core

// ==========================================================
// StylizedLit — proste, płaskie oświetlenie z pełną obsługą cieni.
//
// To NIE jest uproszczony PBR, tylko inny model: czysty Lambert (albedo * N·L), bez specularu,
// bez GGX, bez roughness/metallic/AO. Zostaje dokładnie to, co decyduje o czytelności bryły —
// kierunek światła, kolor i cień — więc materiał ma trzy suwaki zamiast dziesięciu.
//
// Cienie są te SAME co w PBR.frag (kaskady kierunkowe + atlas spotów, ten sam filtr PCF i te same
// bloki UBO/SSBO), bo to zasoby silnika, a nie efekt materiału. Silnik nie ma mechanizmu
// #include dla GLSL, więc deklaracje bloków i funkcje filtrujące są tu POWIELONE z PBR.frag —
// zmieniając format ShadowData / SpotLightGPU / sloty tekstur, zmień OBA pliki.
// ==========================================================

out vec4 FragColor;

// ==========================================================
// Wejścia z BasicVert.vert / BasicVertInstanced.vert / BasicVertSkeletal.vert
// (ten sam .frag obsługuje wszystkie trzy programy, dokładnie jak PBR.frag)
// ==========================================================
in vec4 FragPos;     // pozycja fragmentu w world-space
in vec3 Normal;      // normalna geometryczna (world-space)
in vec2 TexCoord;    // UV
in vec3 VertColor;   // kolor wierzchołka
in mat3 TBN;         // baza tangent->world dla normal mappingu

// ==========================================================
// Uniformy silnika (engine-only — NIE są parametrami materiału)
// ==========================================================
uniform vec3 cameraPos;     // pozycja obserwatora (world-space)
uniform vec3 dirLightDir;   // kierunek LOTU światła (forward słońca; ku światłu = -dirLightDir)
uniform vec4 dirLightColor; // rgb = kolor, w = intensywność
uniform mat4 view;
uniform mat4 projection;    // contact shadows: rzut próbek promienia na ekran          // macierz widoku (głębia w przestrzeni kamery — wybór kaskady)

// ==========================================================
// Cienie kaskadowe (CSM) — mirror bloku z PBR.frag. Atlas kaskad siedzi na slocie 15
// (ostatnim gwarantowanym przez GL 4.5), bo samplery silnika rosną od 15 w dół, a tekstury
// materiału od 0 w górę — patrz komentarz przy shadowCascades w PBR.frag.
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

// Górny limit pętli filtra; musi odpowiadać Plu::kMaxShadowPcfTaps.
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
// Światła stożkowe (SpotLight) — mirror bloków z PBR.frag.
// ==========================================================
layout(std140, binding = 4) uniform SpotLightData
{
    int   spotLightOffset;         // gdzie zaczyna się lista indeksów tego draw calla
    int   spotLightCount;          // 0 = brak świateł stożkowych w tej klatce
    float invSpotShadowResolution; // 1 / rozdzielczość slotu atlasu
    int   spotShadowSlotCount;     // 0 = cienie spotów niedostępne w tej klatce
};

struct SpotLightGPU
{
    mat4  shadowViewProj;         // identity, gdy światło nie dostało slotu
    vec3  position;
    float range;                  // metry
    vec3  direction;              // kierunek LOTU światła
    float innerConeCos;
    vec3  color;                  // kolor * intensywność, premultiplied na CPU
    float outerConeCos;
    int   shadowSlot;             // -1 = brak mapy cienia w tej klatce
    float depthBias;              // METRY (przeliczane przez depthBiasScale)
    float normalBias;             // teksele
    float pcfRadius;              // teksele
    float texelWorldPerMetre;     // rozmiar teksela na metr odległości od źródła
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
// Świadomie NIE MA tu: metallicMap/metallicFactor, roughnessMap/roughnessFactor,
// occlusionMap/occlusionStrength ani niczego związanego ze specularem — to właśnie te parametry
// robią z materiału PBR. Normal map zostaje, bo bazę TBN i tak liczy vertex shader (wspólny
// z PBR), a bez niej stylizowana powierzchnia nie ma jak pokazać żadnego detalu.
// ==========================================================
uniform sampler2D albedoMap;
uniform sampler2D normalMap;

uniform bool useAlbedoMap;
uniform bool useNormalMap;

uniform vec3  albedoColor;    // tint albedo (i kolor bazowy, gdy brak mapy)
uniform float normalStrength; // siła normal mappingu

// Prosty ambient zastępczy (brak IBL w silniku) — jedyne źródło światła w cieniu.
uniform vec3  ambientColor;

// ----------------------------------------------------------
// Cienie — kod identyczny z PBR.frag (patrz uwaga o braku #include na górze pliku).
// ----------------------------------------------------------

// Dysk Vogela: `count` próbek rozłożonych po złotym kącie.
vec2 VogelDiskSample(int index, int count, float phase)
{
    const float kGoldenAngle = 2.39996323;
    float radius = sqrt((float(index) + 0.5) / float(count));
    float theta  = float(index) * kGoldenAngle + phase;
    return radius * vec2(cos(theta), sin(theta));
}

// Interleaved gradient noise (Jimenez) — obraca dysk per piksel, zamieniając schodki wielkości
// teksela kaskady na dither o szerokości jednego piksela ekranu.
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Jedno sprzętowe porównanie = przefiltrowany bilinearnie wynik, czyli 2x2 PCF za JEDNO pobranie.
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

    // Przenikanie kaskad — bez tego skok rozmiaru teksela rysuje szew w poprzek sceny.
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

    // Wygaszanie na dystansie zamiast twardej krawędzi na końcu ostatniej kaskady.
    float fade = clamp((depthView - shadowFadeStart) / max(shadowFadeEnd - shadowFadeStart, 1e-4), 0.0, 1.0);
    visibility = mix(visibility, 1.0, fade);

    // Najciemniejszy wygrywa — mnożenie podwajałoby zaciemnienie tam, gdzie kaskada i promień
    // widzą ten sam okluder, obrysowując każdy detal czarną obwódką.
    return min(visibility, contact);
}

// Cień spota — perspektywiczny, więc z dzieleniem przez w i z odrzuceniem fragmentów za
// wierzchołkiem stożka; normal-offset skalowany dystansem (teksel rośnie z odległością).
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

// Bias cienia rośnie jak tan(θ) kąta padania, nie liniowo — przy kącie ślizgowym dąży do
// nieskończoności. Clampujemy cos od dołu, żeby tan nie wybuchł, i tniemy slope do 5.
float SlopeFromNdotL(float NdotL)
{
    float cosT = max(NdotL, 0.1);
    return min(sqrt(1.0 - cosT * cosT) / cosT, 5.0);
}

void main()
{
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

    // Głębia w przestrzeni widoku — trzeci wiersz macierzy view × FragPos (bez pełnego mnożenia
    // mat4 × vec4). Steruje wyborem kaskady, więc liczona przed gałęzią światła.
    float depthView = abs(dot(vec4(view[0][2], view[1][2], view[2][2], view[3][2]), FragPos));

    // ----------------------------------------------------------
    // Światło kierunkowe — czysty Lambert: albedo * N·L * radiancja * cień.
    // ----------------------------------------------------------
    vec3 Lo = vec3(0.0);
    {
        vec3  L = normalize(-dirLightDir);
        float NdotL = max(dot(N, L), 0.0);

        // Early-out: cały wkład jest mnożony przez NdotL, więc dla fragmentów odwróconych od
        // światła (~połowa każdego obiektu) wynik to gwarantowane zero — nie próbkuj cienia.
        if (NdotL > 0.0)
        {
            vec3  radiance = dirLightColor.rgb * dirLightColor.w;
            float shadow   = ShadowVisibility(FragPos.xyz, N, depthView, SlopeFromNdotL(NdotL));
            Lo += shadow * albedo * radiance * NdotL;
        }
    }

    // ----------------------------------------------------------
    // Światła stożkowe — ta sama pętla i te same trzy wczesne odrzucenia co w PBR.frag; różni
    // się wyłącznie tym, co jest mnożone na końcu (albedo zamiast BRDF-u Cook-Torrance).
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

        // Odwrotność kwadratu z oknem wygaszającym (Frostbite/UE) — bez okna światło urywałoby
        // się skokiem dokładnie na `range`.
        float rangeRatio  = dist / max(light.range, 1e-4);
        float window      = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
        float attenuation = (1.0 / (distSq + 1e-4)) * window * window;

        float spot   = smoothstep(light.outerConeCos, light.innerConeCos, cosAngle);
        float shadow = SpotShadowVisibility(light, FragPos.xyz, N, dist, SlopeFromNdotL(NdotL));

        Lo += shadow * albedo * (light.color * attenuation * spot) * NdotL;
    }

    vec3 color = ambientColor * albedo + Lo;

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
    // (Reinhard w PBR.frag) tu ŚWIADOMIE NIE MA: on ściąga jasne barwy ku bieli, a płaskie,
    // nasycone plamy koloru to cały sens tego shadera. Cena: przy intensywności światła mocno
    // powyżej 1 jasne obszary przycinają się twardo do bieli, zamiast rolować się miękko.
    color = clamp(color, 0.0, 1.0);
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
