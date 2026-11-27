// @title Voyage stellaire
// @description Champ d'étoiles en profondeur sur une nébuleuse, chaque couche avec sa propre parallaxe.
// @author WallEnginePaper
// Champ d'etoiles en profondeur : quatre couches qui defilent vers l'observateur, chacune avec
// une parallaxe proportionnelle a sa proximite, sur une nebuleuse lente.
//
// @param float density 1.0  0.3 2.0        "Densité"
// @param float warp    0.12 0.0 1.0        "Vitesse"
// @param color nebula  0.35 0.15 0.60      "Couleur de la nébuleuse"

float hash21(float2 p)
{
    p = frac(p * float2(233.34, 851.73));
    p += dot(p, p + 23.45);
    return frac(p.x * p.y);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x),
                lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}

float3 StarLayer(float2 uv)
{
    float3 color = float3(0.0, 0.0, 0.0);
    float2 gv = frac(uv) - 0.5;
    float2 id = floor(uv);
    float n = hash21(id);
    float size = frac(n * 345.32);
    float2 offset = float2(n, frac(n * 34.0)) - 0.5;
    float d = length(gv - offset * 0.8);
    float star = (0.015 * (0.4 + size)) / max(d, 1e-3);
    star *= smoothstep(0.5, 0.1, d);
    float3 tint = lerp(float3(1.0, 0.75, 0.55), float3(0.6, 0.8, 1.0), frac(n * 2345.2));
    color += star * tint * step(0.25, size);
    return color;
}

void mainImage(out float4 fragColor, in float2 fragCoord)
{
    float2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float t = iTime * warp;

    float cloud = valueNoise(uv * 2.0 + iParallax * 0.1 + t * 0.2) * valueNoise(uv * 4.3 - t * 0.1);
    float3 color = nebula.rgb * cloud * 0.35;

    [unroll] for (int i = 0; i < 4; ++i)
    {
        float depth = frac((float)i * 0.25 + t);
        float scale = lerp(18.0, 0.6, depth) / density;
        float fade = depth * smoothstep(1.0, 0.85, depth);
        float2 parallax = iParallax * (0.02 + depth * 0.2);
        color += StarLayer((uv + parallax) * scale + (float)i * 453.2) * fade;
    }

    color *= 1.0 + iAudioLevel * 0.8;
    fragColor = float4(color, 1.0);
}
