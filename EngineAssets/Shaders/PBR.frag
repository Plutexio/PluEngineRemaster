#version 450 core

out vec4 FragColor;

// ==========================================================
// Wejścia z BasicVert.vert
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
uniform mat4 view;          // macierz widoku (do wyliczenia głębi w przestrzeni kamery — wybór kaskady)
uniform mat4 projection;    // macierz projekcji (contact shadows: rzut próbek promienia na ekran)

// ==========================================================
// Cienie kaskadowe (CSM) światła kierunkowego — sterowane przez silnik
// (Renderer::RenderSnapshot). Wszystkie kaskady leżą obok siebie w JEDNEJ teksturze 2D (atlasie)
// na slocie 15 — OSTATNIM gwarantowanym przez GL 4.5 (GL_MAX_TEXTURE_IMAGE_UNITS >= 16).
//
// Dlaczego nie na slocie 0: sampler materiału, któremu nie przypisano tekstury, NIE dostaje
// przypisanego slotu (RenderFromMaterial wywołuje SetTextureUniform tylko dla realnie
// zbindowanych tekstur), więc zostaje na domyślnym slocie 0. Dwa samplery RÓŻNYCH typów
// (sampler2D materiału + sampler2DShadow) na tym samym slocie to INVALID_OPERATION przy
// rysowaniu — część sterowników to toleruje, część (Mesa) nie i gasi cały pass. Trzymanie
// cieni na drugim końcu zakresu usuwa całą klasę tych kolizji.
// ==========================================================
// MAX_CASCADE_COUNT jest tylko górnym ograniczeniem tablicy (Plu::kMaxShadowCascades);
// realną liczbę kaskad niesie cascadeCount z bloku ShadowData. Indeksowanie tablic w UBO
// zmienną nie-uniformową jest legalne (w odróżnieniu od tablic samplerów — tamto było
// formalnym UB, dlatego kaskady dzielą JEDNĄ teksturę, a nie N samplerów).
#define MAX_CASCADE_COUNT 6

// One cascade. Mirrors Plu::ShadowCascadeGPU — a struct rather than parallel arrays because a
// std140 array of scalars has a 16 B stride, so the parallel form had to pack four cascades into
// one vec4, and that is what capped the cascade count at four.
struct ShadowCascade
{
    mat4 viewProj;
    // Places this cascade inside the shared atlas: atlasUV = projCoords.xy * xy + zw.
    // Cascades no longer have equal resolution, so where a cascade lives and how large it is are
    // per-cascade data, not one global map size.
    vec4 atlasScaleBias;
    // x = view-space end distance, y = texel size in metres, z = depth bias in [0,1] depth
    // (converted on the CPU), w = unused.
    vec4 params;
};

layout(std140, binding = 2) uniform ShadowData
{
    ShadowCascade cascades[MAX_CASCADE_COUNT];
    vec2  invAtlasSize;           // 1 / atlas size; one atlas texel IS one cascade texel
    int   cascadeCount;           // 0 = brak cieni kierunkowych w tej klatce
    float shadowFadeStart;        // metry — od tąd cień zanika
    float shadowFadeEnd;          // metry — za tym dystansem pełne światło
    float cascadeBlendFraction;   // jaka część kaskady służy do przenikania w następną
    float normalBiasScale;        // normal-offset w tekselach
    float pcfRadiusTexels;        // promień dysku PCF w tekselach
    int   debugVisualizeCascades; // != 0 = koloruj kaskady
    int   pcfTapCount;            // liczba próbek dysku PCF (1 = jedno sprzętowe pobranie)
    int   pcfRotateSamples;       // != 0 = obrót dysku per piksel (schodki -> dither)
    int   contactShadowSteps;     // 0 = contact shadows wyłączone
    float contactShadowLength;    // metry marszu promienia
    float contactShadowThickness; // metry — zakładana grubość okludera
    float contactShadowBias;      // metry — odsuwa start promienia od własnej powierzchni
};

// Górny limit pętli filtra; musi odpowiadać Plu::kMaxShadowPcfTaps.
#define MAX_PCF_TAPS 32
// Górny limit kroków contact shadows; musi odpowiadać Plu::kMaxContactShadowSteps.
#define MAX_CONTACT_STEPS 64

// Głębia sceny z depth prepassa (Renderer::RenderDepthPrepass) — zwykła tekstura głębi, BEZ
// samplera porównującego: contact shadows czytają wartość głębi i maszerują wzdłuż niej, a nie
// porównują ją z referencją. Slot 13, czyli kolejny w dół od kaskad (15) i spotów (14).
//
// To NIE jest bufor głębi, do którego pisze pass oświetlenia — to osobny obiekt zapełniony
// wcześniej. Samplowanie załącznika aktualnie zbindowanego framebuffera jest feedback loopem
// (niezdefiniowanym przez spec i odrzucanym przez część sterowników), więc prepass musi mieć
// własną teksturę.
layout(binding = 13) uniform sampler2D sceneDepthTexture;

// Sampler PORÓWNUJĄCY: `texture()` na sampler2DShadow zwraca wynik porównania głębi
// przefiltrowany sprzętowo (2x2 PCF za jedno pobranie), a nie samą głębię. Tryb porównania
// siedzi na obiekcie samplera bindowanym tylko na czas passu światła (Renderer), więc sama
// tekstura zostaje zwykłą teksturą głębi — podglądaną np. przez TextureViewerPanel.
//
// A plain 2D texture, not an array: every cascade is a rectangle of one atlas, which is what
// lets a near cascade be 2048² while a far one is 512².
layout(binding = 15) uniform sampler2DShadow shadowCascades;

// ==========================================================
// Światła stożkowe (SpotLight) — sterowane przez silnik (Renderer::UpdateSpotLightBuffers).
//
// Dane świateł idą BLOKAMI (UBO/SSBO), nie tablicą luźnych uniformów, i to jest celowe:
// ShaderCodeParser dopasowuje wyłącznie `uniform T nazwa;`, więc członkowie bloków są dla niego
// niewidoczni i nie trafiają do listy parametrów materiału. Jedyny luźny uniform tej sekcji to
// sampler `spotShadowMaps`, dlatego jest wpisany do engineOnlyUniforms w ShaderCodeParser.py.
//
// spotLightOffset/spotLightCount to cała inwestycja pod clustered forward: dziś bufor indeksów
// jest jedną globalną listą i offset wynosi 0; po dodaniu klastrów te dwie liczby będą pochodzić
// z lookupu do siatki, a pętla niżej NIE zmieni się ani o linijkę.
// ==========================================================
layout(std140, binding = 4) uniform SpotLightData
{
    int   spotLightOffset;        // gdzie zaczyna się lista indeksów tego draw calla
    int   spotLightCount;         // 0 = brak świateł stożkowych w tej klatce
    float invSpotShadowResolution; // 1 / rozdzielczość slotu atlasu
    int   spotShadowSlotCount;    // 0 = cienie spotów niedostępne w tej klatce
};

// Mirror struktury Plu::SpotLightGPU (RenderUtils.h). std430 wyrównuje vec3 do 16 B i pozwala
// następnemu skalarowi wypełnić lukę — stąd pary (vec3, float).
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

// Atlas cieni spotów — warstwa per slot, indeksowana zmienną per-światło. Legalne dla warstwy
// tablicy tekstur (w odróżnieniu od indeksowania tablicy SAMPLERÓW) — ten sam powód, dla którego
// kaskady są warstwami jednej tekstury. Slot 14: samplery silnika rosną od 15 W DÓŁ, tekstury
// materiału od 0 w górę (patrz komentarz przy shadowCascades).
layout(binding = 14) uniform sampler2DArrayShadow spotShadowMaps;

// ==========================================================
// Parametry materiału (auto-wykrywane przez ShaderCodeParser)
// Tylko sampler2D / float / vec3 — pozostałe typy nie mają setterów w RenderFromMaterial.
// ==========================================================

// Mapy PBR
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D occlusionMap;

// Flagi użycia map (true = używaj mapy, false = używaj wartości skalarnej poniżej).
// Pozwala materiałowi bez przypisanej tekstury działać poprawnie (niepodpięty sampler
// nie jest bindowany przez RenderFromMaterial, więc bez tej flagi czytałby śmieci/slot 0).
uniform bool useAlbedoMap;
uniform bool useNormalMap;
uniform bool useMetallicMap;
uniform bool useRoughnessMap;
uniform bool useOcclusionMap;

// Wartości bazowe / mnożniki
uniform vec3  albedoColor;       // tint albedo (i fallback gdy brak mapy)
uniform float metallicFactor;    // [0..1]
uniform float roughnessFactor;   // [0..1]
uniform float occlusionStrength; // siła AO [0..1]
uniform float normalStrength;    // siła normal mappingu

// Prosty ambient zastępczy (brak IBL w silniku — patrz uwagi w PR).
uniform vec3  ambientColor;

const float PI = 3.14159265359;

// ----------------------------------------------------------
// Funkcje BRDF Cook-Torrance
// ----------------------------------------------------------

// Rozkład mikrofacetów (GGX / Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-6);
}

// Geometria — Schlick-GGX z aproksymacją k dla światła bezpośredniego
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith — uwzględnia geometrię od strony światła i obserwatora
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// Fresnel — Schlick
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Wspólne ciało Cook-Torrance dla WSZYSTKICH świateł bezpośrednich: zwraca (kD*albedo/PI +
// specular), czyli wkład bez radiancji, cienia i NdotL — te mnoży wołający, bo różnią się per typ
// światła (kierunkowe nie ma tłumienia ani stożka). Wynik dla światła kierunkowego jest identyczny
// co do bitu z dawnym kodem inline, więc spot nie duplikuje BRDF-u, a dir nie zmienia wyglądu.
vec3 EvaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 1e-4;
    vec3 specular     = numerator / denominator;

    // Energia: kS = F, kD to reszta; metale nie mają dyfuzji.
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    return kD * albedo / PI + specular;
}

// ----------------------------------------------------------
// Cień kaskadowy — zwraca widoczność światła [0..1] (1 = pełne światło, 0 = w cieniu).
//
// Budżet biasu (świadomie rozdzielony, bez literałów w GLSL):
//  * caster — front-face culling in the shadow pass (Renderer.cpp), no polygon offset on
//    triangles: only faces turned away from the light reach the map, and this shader samples
//    the shadow only where NdotL > 0, so a lit surface never compares against itself;
//  * receiver — normal-offset w TEKSELACH tej kaskady (niżej) + depth-bias w metrach
//    przeliczony na CPU na [0,1] głębi kaskady (cascades[i].params.z z UBO).
// ----------------------------------------------------------

// Dysk Vogela: `count` próbek rozłożonych po złotym kącie. W przeciwieństwie do stałej tablicy
// Poissona jest liczony analitycznie, więc pcfTapCount jest ciągłym suwakiem jakości — przy 8
// próbkach daje ten sam rozkład co dawny dysk, przy 16-32 wypełnia promień bez dziur.
vec2 VogelDiskSample(int index, int count, float phase)
{
    const float kGoldenAngle = 2.39996323;
    float radius = sqrt((float(index) + 0.5) / float(count));
    float theta  = float(index) * kGoldenAngle + phase;
    return radius * vec2(cos(theta), sin(theta));
}

// Interleaved gradient noise (Jimenez) — deterministyczna funkcja współrzędnej piksela.
// Obraca dysk per piksel: krawędź, która przy stałym wzorze układa się w schodki wielkości
// teksela kaskady, rozsypuje się na dither o szerokości jednego piksela ekranu. To jedyny
// sposób, żeby MAŁY promień PCF (ostry cień) nie wyglądał na pikselowaty.
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Jedno sprzętowe porównanie: zwraca przefiltrowany bilinearnie wynik (refDepth <= głębia mapy),
// czyli 2x2 PCF za JEDNO pobranie. Dzięki temu każdy tap dysku jest już sub-tekselowy, a N tapów
// daje ~4N efektywnych porównań z N pobrań.
float TapShadow(vec2 atlasUv, float refDepth)
{
    return texture(shadowCascades, vec3(atlasUv, refDepth));
}

float FilterCascade(int cascade, vec3 worldPos, vec3 normal, float slope)
{
    // Normal-offset liczony w tekselach TEJ kaskady: to jedyna skala, w której "przesuń próbkę
    // poza własny cień" znaczy to samo w kaskadzie bliskiej i dalekiej.
    float texelWorld  = cascades[cascade].params.y;
    float offsetScale = texelWorld * normalBiasScale * clamp(0.5 + slope, 0.5, 3.0);
    vec3  offsetPos   = worldPos + normal * offsetScale;

    // Projekcja kaskady jest ortho, więc w == 1 — dzielenie perspektywiczne jest zbędne.
    vec4 fragPosLS  = cascades[cascade].viewProj * vec4(offsetPos, 1.0);
    vec3 projCoords = fragPosLS.xyz * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 1.0;
    // Explicit range test, unlike the layered map this replaces: a cascade's neighbour in the
    // atlas is ANOTHER cascade, not the sampler's border colour, so falling off the edge would
    // read a stranger's depth instead of "lit".
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0))))
        return 1.0;

    vec2 atlasScale  = cascades[cascade].atlasScaleBias.xy;
    vec2 atlasOffset = cascades[cascade].atlasScaleBias.zw;
    vec2 atlasUv     = projCoords.xy * atlasScale + atlasOffset;

    float refDepth = projCoords.z - cascades[cascade].params.z * (1.0 + 2.0 * slope);

    // Jedno pobranie to już sprzętowy 2x2 PCF, więc przy zerowym promieniu (albo jednej próbce)
    // dysk jest zbędny — to najostrzejszy możliwy cień, ograniczony wyłącznie gęstością tekseli.
    int taps = clamp(pcfTapCount, 1, MAX_PCF_TAPS);
    if (taps == 1 || pcfRadiusTexels <= 0.0)
    {
        return TapShadow(atlasUv, refDepth);
    }

    // The disk has to stay inside this cascade's rectangle. Half a texel of margin, because the
    // hardware tap is bilinear and reaches into the neighbouring texel on its own — without it a
    // wide radius smears the cascade packed next door into this one's shadow edge.
    vec2 clampMin = atlasOffset + 0.5 * invAtlasSize;
    vec2 clampMax = atlasOffset + atlasScale - 0.5 * invAtlasSize;

    // Radius is in texels and one atlas texel is one cascade texel, so a single inverse atlas
    // size converts it for every cascade regardless of their differing resolutions.
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

// Wybór kaskady po głębi w przestrzeni widoku (dodatnia odległość od kamery).
int SelectCascade(float depthView)
{
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount - 1; i++)
    {
        if (depthView < cascades[i].params.x) { return i; }
    }
    return cascade;
}

// ----------------------------------------------------------
// Contact shadows — krótki promień maszerowany przez głębię sceny, w przestrzeni ekranu.
//
// Po co, skoro są kaskady: teksel kaskady ma stały rozmiar w METRACH (2*Radius/Resolution) i
// rośnie z kwadratem odległości splitu, więc detal poniżej ~2 cm przestaje rzucać cień długo
// przed końcem zasięgu kaskady — i żadna rozdzielczość tego nie odzyskuje w rozsądnym VRAM-ie.
// Tutaj jednostką jest PIKSEL EKRANU, więc rozdzielczość cienia skaluje się z tym, co widać.
// Cena: promień widzi wyłącznie to, co jest w buforze głębi, czyli nie rzuca cienia z obiektów
// poza kadrem i za innymi obiektami. Dlatego to dodatek do kaskad na krótkim dystansie, a nie
// ich zamiennik.
// ----------------------------------------------------------

// Odległość od kamery (dodatnia, w metrach) z wartości [0,1] bufora głębi. Współczynniki
// wyciągnięte z macierzy projekcji, więc near/far nie muszą jechać osobnym uniformem:
// dla perspektywy ndcZ = (P22*zView + P32) / -zView, co odwraca się do wzoru niżej.
float LinearizeSceneDepth(float depth01)
{
    float ndcZ = depth01 * 2.0 - 1.0;
    return projection[3][2] / (ndcZ + projection[2][2]);
}

float ContactShadow(vec3 worldPos, vec3 normal, vec3 L, float slope)
{
    if (contactShadowSteps <= 0) return 1.0;

    // Wygaszanie przy świetle STYCZNYM. Tam promień biegnie niemal równolegle do powierzchni,
    // więc o trafieniu decyduje dyskretyzacja bufora głębi, a nie geometria — to źródło resztki
    // acne, której bias nie usuwa (a przy wartościach, które by ją usunęły, bias zjada detale).
    // Te same miejsca mają N·L bliskie zeru, czyli są ledwo oświetlone i cień kontaktowy nie ma
    // tam czego przyciemnić. Zamiast walczyć biasem, oddajemy pole dokładnie tam, gdzie efekt
    // jest niewidoczny, a artefakt najsilniejszy.
    float NdotL = dot(normal, L);
    float grazingFade = smoothstep(0.0, 0.3, NdotL);
    if (grazingFade <= 0.0) return 1.0;

    // Start odsunięty wzdłuż NORMALNEJ, skalowany tangensem kąta padania. Sam offset wzdłuż
    // promienia (niżej) nie wystarcza: przy świetle padającym stycznie promień biegnie niemal
    // równolegle do powierzchni, więc przesunięcie go do przodu prawie nie oddala go od niej i
    // pierwsze próbki łapią własną geometrię fragmentu — postrzępione, szarpane zaciemnienie na
    // płaskich powierzchniach, czyli acne. Skalowanie przez tan(θ) to ta sama reguła, którą
    // stosują biasy kaskad: wymagany odstęp rośnie jak tangens, nie liniowo.
    vec3 originWorld = worldPos + normal * (contactShadowBias * (1.0 + 2.0 * slope));

    // Marsz w przestrzeni WIDOKU, nie ekranu: kroki są wtedy równe w metrach, więc "25 cm
    // długości" i "5 cm grubości" znaczą to samo blisko i daleko, pod każdym kątem. Wersja
    // krokująca po pikselach musiałaby te wielkości przeliczać per fragment.
    vec3 rayOriginView = (view * vec4(originWorld, 1.0)).xyz;
    vec3 rayDirView    = normalize(mat3(view) * L);

    // Bias wzdłuż promienia — bez niego pierwsza próbka trafia we własną powierzchnię fragmentu
    // i cała oświetlona geometria robi się ciemna.
    rayOriginView += rayDirView * contactShadowBias;

    int steps = min(contactShadowSteps, MAX_CONTACT_STEPS);

    // Rozrzut startu per piksel. Bez niego równe kroki dają nieruchome prążki tam, gdzie promień
    // mija okluder o włos — ten sam powód, dla którego dysk PCF jest obracany.
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy);

    for (int i = 1; i <= steps; i++)
    {
        // Rozkład KWADRATOWY, nie równomierny. Przy równych krokach cała rozdzielczość promienia
        // to length/steps — przy 25 cm i 12 krokach ponad 2 cm, czyli więcej niż detal, który ma
        // rzucić cień, więc promień po prostu nad nim przeskakuje. Kwadrat zagęszcza próbki tam,
        // gdzie leżą detale (pierwszy krok to ~1 mm przy 16 krokach), a rzadsze dalekie kroki
        // zachowują zasięg za tę samą liczbę pobrań.
        float t = (float(i) - 0.5 + jitter) / float(steps);
        vec3 samplePosView = rayOriginView + rayDirView * (contactShadowLength * t * t);

        vec4 clip = projection * vec4(samplePosView, 1.0);
        // Promień wyszedł za kamerę albo poza kadr — dalej nie ma czego czytać, a nie ma też
        // powodu udawać cienia z danych, których nie mamy.
        if (clip.w <= 0.0) break;
        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;

        float sceneDist = LinearizeSceneDepth(texture(sceneDepthTexture, uv).r);
        float rayDist   = -samplePosView.z;
        float diff      = rayDist - sceneDist;

        // Trafienie = promień jest ZA powierzchnią sceny, ale nie głębiej niż jej zakładana
        // grubość. Górne ograniczenie jest konieczne, bo bufor głębi trzyma jedną powierzchnię,
        // a nie bryłę: bez niego odległe tło zasłaniałoby wszystko, co jest przed nim.
        if (diff > 0.0 && diff < contactShadowThickness)
        {
            // Wygaszanie przy końcu zasięgu: trafienie w ostatnich krokach nie może być tak
            // mocne jak przy samej powierzchni, bo granica długości promienia rysowałaby wtedy
            // twardą krawędź w poprzek sceny. `t` to postęp wzdłuż promienia (patrz wyżej).
            // Do tego wygaszanie stycznego kąta — patrz grazingFade na początku funkcji.
            return mix(1.0, smoothstep(0.75, 1.0, t), grazingFade);
        }
    }
    return 1.0;
}

float ShadowVisibility(vec3 worldPos, vec3 normal, float depthView, float slope)
{
    // Contact shadows są niezależne od kaskad — działają też, gdy CSM jest wyłączone.
    float contact = ContactShadow(worldPos, normal, normalize(-dirLightDir), slope);

    if (cascadeCount <= 0) return contact;

    int   cascade    = SelectCascade(depthView);
    float visibility = FilterCascade(cascade, worldPos, normal, slope);

    // Przenikanie kaskad: na ostatnim cascadeBlendFraction zakresu kaskady mieszamy jej wynik
    // z wynikiem następnej. Bez tego skok rozmiaru teksela rysuje wyraźny szew w poprzek sceny.
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

    // Wygaszanie na dystansie: zamiast twardej krawędzi na końcu ostatniej kaskady cień
    // rozpływa się w pełne światło.
    float fade = clamp((depthView - shadowFadeStart) / max(shadowFadeEnd - shadowFadeStart, 1e-4), 0.0, 1.0);
    visibility = mix(visibility, 1.0, fade);

    // Najciemniejszy wygrywa. Mnożenie byłoby błędem: w miejscu, gdzie oba źródła widzą ten sam
    // okluder — a to normalny przypadek, bo kaskada łapie sylwetkę, a promień jej krawędź —
    // podwajałoby zaciemnienie i obrysowywało każdy detal czarną obwódką.
    return min(visibility, contact);
}

// ----------------------------------------------------------
// Cień światła stożkowego — widoczność [0..1]. Trzy różnice względem FilterCascade:
//  * dzielenie perspektywiczne (lp.xyz / lp.w) — projekcja spota jest perspektywiczna, więc
//    w != 1 (ortho kaskady to gwarantuje i dlatego tam go nie ma);
//  * odrzucenie lp.w <= 0 — fragmenty ZA wierzchołkiem stożka rzutują się na odbicie mapy,
//    czyli fałszywy cień po drugiej stronie lampy;
//  * normal-offset skalowany DYSTANSEM: teksel projekcji perspektywicznej rośnie liniowo
//    z odległością od źródła, więc stała per-światło nie wystarcza.
// ----------------------------------------------------------
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
    // Brak testu zakresu UV — atlas ma CLAMP_TO_BORDER z borderem 1.0, więc poza stożkiem
    // porównanie zawsze wypada "oświetlony".

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
    // Ten sam dysk Vogela i ten sam szum co przy kaskadach — jedna implementacja filtra dla
    // obu typów cieni.
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

void main()
{
    // --- Albedo (mapy są wgrywane jako linear, więc gdy używamy mapy sRGB->linear ręcznie) ---
    vec3 albedo = albedoColor;
    if (useAlbedoMap)
    {
        vec3 texel = texture(albedoMap, TexCoord).rgb;
        // Stopgap dopóki silnik nie wgrywa albedo w GL_SRGB8_ALPHA8.
        texel = pow(texel, vec3(2.2));
        albedo *= texel;
    }

    // --- Metallic / Roughness / AO ---
    float metallic  = metallicFactor;
    if (useMetallicMap)
        metallic *= texture(metallicMap, TexCoord).r;

    float roughness = roughnessFactor;
    if (useRoughnessMap)
        roughness *= texture(roughnessMap, TexCoord).r;
    roughness = clamp(roughness, 0.04, 1.0); // unikamy zerowej szorstkości

    float ao = 1.0;
    if (useOcclusionMap)
        ao = mix(1.0, texture(occlusionMap, TexCoord).r, occlusionStrength);

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

    // Reflektancja przy padaniu prostopadłym: dielektryki ~0.04, metale = albedo.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Głębia w przestrzeni widoku — trzeci wiersz macierzy view × FragPos (potrzebna tylko
    // składowa z, bez pełnego mnożenia mat4 × vec4). Steruje wyborem kaskady i debug tintem,
    // więc liczona przed gałęzią światła.
    float depthView = abs(dot(vec4(view[0][2], view[1][2], view[2][2], view[3][2]), FragPos));

    // ----------------------------------------------------------
    // Światło bezpośrednie — pojedyncze światło kierunkowe
    // ----------------------------------------------------------
    vec3 Lo = vec3(0.0);
    {
        vec3 L = normalize(-dirLightDir); // ku źródłu światła (dirLightDir = kierunek lotu promieni)
        float NdotL = max(dot(N, L), 0.0);

        // Early-out: cały wkład światła bezpośredniego (BRDF + próbkowanie cienia w
        // ShadowVisibility) jest na końcu mnożony przez NdotL, więc dla fragmentów odwróconych
        // od światła (NdotL == 0, ~połowa każdego obiektu) wynik to gwarantowane zero — nie licz
        // niczego. Wynik identyczny co do bitu.
        if (NdotL > 0.0)
        {
            vec3 radiance = dirLightColor.rgb * dirLightColor.w; // kolor * intensywność
            vec3 brdf     = EvaluateBRDF(N, V, L, albedo, roughness, metallic, F0);

            // --- Cień kaskadowy dla światła kierunkowego ---
            // slope = tan(θ) kąta padania — wymagany bias rośnie właśnie jak tan, a nie liniowo:
            // przy kącie ślizgowym (NdotL→0) dąży do nieskończoności. Clampujemy cos od dołu, by
            // tan nie wybuchł, i tniemy slope do 5 (poza tym powierzchnia i tak ledwie świeci).
            float cosT  = max(NdotL, 0.1);
            float slope = min(sqrt(1.0 - cosT * cosT) / cosT, 5.0);
            float shadow = ShadowVisibility(FragPos.xyz, N, depthView, slope);

            Lo += shadow * brdf * radiance * NdotL;
        }
    }

    // ----------------------------------------------------------
    // Światła stożkowe — pętla po liście indeksów (dziś globalnej, docelowo per klaster).
    //
    // Trzy wczesne odrzucenia niżej robią całą robotę wydajnościową w forward renderingu: każde
    // z nich wycina światło ZANIM policzymy BRDF i ZANIM ruszymy mapę cienia, a to one kosztują.
    // ----------------------------------------------------------
    for (int i = 0; i < spotLightCount; i++)
    {
        SpotLightGPU light = spotLights[spotLightIndices[spotLightOffset + i]];

        vec3  toLight = light.position - FragPos.xyz;
        float distSq  = dot(toLight, toLight);
        if (distSq > light.range * light.range) continue;   // #1 poza zasięgiem

        float dist = sqrt(distSq);
        vec3  L    = toLight / max(dist, 1e-4);

        // Kąt między kierunkiem LOTU światła a kierunkiem do fragmentu (-L).
        float cosAngle = dot(-L, light.direction);
        if (cosAngle <= light.outerConeCos) continue;       // #2 poza stożkiem

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;                         // #3 odwrócone od światła

        // Odwrotność kwadratu z oknem wygaszającym (Frostbite/UE): samo 1/d² nigdy nie osiąga
        // zera, więc światło urywałoby się skokiem dokładnie na `range`. Okno sprowadza je do
        // zera gładko, a wykładnik 4 trzyma je blisko czystego 1/d² przez większość zasięgu.
        float rangeRatio  = dist / max(light.range, 1e-4);
        float window      = clamp(1.0 - rangeRatio * rangeRatio * rangeRatio * rangeRatio, 0.0, 1.0);
        float attenuation = (1.0 / (distSq + 1e-4)) * window * window;

        // Miękka krawędź stożka między kątem wewnętrznym a zewnętrznym.
        float spot = smoothstep(light.outerConeCos, light.innerConeCos, cosAngle);

        float cosT  = max(NdotL, 0.1);
        float slope = min(sqrt(1.0 - cosT * cosT) / cosT, 5.0);
        float shadow = SpotShadowVisibility(light, FragPos.xyz, N, dist, slope);

        vec3 radiance = light.color * attenuation * spot;
        Lo += shadow * EvaluateBRDF(N, V, L, albedo, roughness, metallic, F0) * radiance * NdotL;
    }

    // Ambient zastępczy (brak IBL) — AO przyciemnia tylko ambient.
    vec3 ambient = ambientColor * albedo * ao;

    vec3 color = ambient + Lo;

    // Debug: pokoloruj pikselom przypisaną kaskadę (View → ustawienia sceny). Strefy przenikania
    // widać jako gradient między sąsiednimi pasami.
    if (debugVisualizeCascades != 0 && cascadeCount > 0)
    {
        const vec3 kCascadeTint[MAX_CASCADE_COUNT] = vec3[](
            vec3(1.0, 0.4, 0.4), vec3(0.4, 1.0, 0.4),
            vec3(0.4, 0.6, 1.0), vec3(1.0, 1.0, 0.4),
            vec3(1.0, 0.5, 1.0), vec3(0.4, 1.0, 1.0)
        );
        color *= kCascadeTint[SelectCascade(depthView)];
    }

    // Tonemapping (Reinhard) + korekcja gamma — brak osobnego passa wyjściowego w silniku,
    // a bufor główny jest liniowy (RGBA8), więc robimy to tutaj.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
