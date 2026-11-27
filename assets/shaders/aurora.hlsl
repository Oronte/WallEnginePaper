// @title Aurore boréale
// @description Trois rideaux d'aurore au-dessus des montagnes, parallaxe souris et réaction aux basses.
// @author WallEnginePaper
// Aurora - fond par defaut de WallEnginePaper.
// Aurore boreale en trois rideaux au-dessus d'une chaine de montagnes, avec parallaxe souris
// (montagnes, etoiles et aurore bougent a des profondeurs differentes) et reaction aux basses.
//
// @param float intensity  1.0  0.2 2.0  "Intensité de l'aurore"
// @param color skyColor   0.015 0.025 0.07  "Couleur du ciel"
// @param float audioBoost 0.6  0.0 2.0  "Réaction aux basses"
// @param float starDensity 1.0 0.0 2.0  "Densité d'étoiles"

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    [unroll] for (int i = 0; i < 5; ++i)
    {
        value += amplitude * valueNoise(p);
        p = p * 2.03 + float2(17.1, 9.2);
        amplitude *= 0.5;
    }
    return value;
}

void mainImage(out float4 fragColor, in float2 fragCoord)
{
    float2 uv = fragCoord / iResolution.xy;
    float aspect = iResolution.x / iResolution.y;
    float bass = AudioBass() * audioBoost;

    // Ciel : degrade vertical
    float3 color = lerp(skyColor.rgb * 2.2, skyColor.rgb * 0.6, uv.y);

    // Etoiles (plan le plus lointain : parallaxe faible)
    float2 starUv = float2(uv.x * aspect, uv.y) * 220.0 + iParallax * 3.0;
    float2 cell = floor(starUv);
    float rnd = hash21(cell);
    float2 starPos = float2(hash21(cell + 7.1), hash21(cell + 3.3)) - 0.5;
    float starDist = length(frac(starUv) - 0.5 - starPos * 0.6);
    float threshold = 1.0 - 0.012 * starDensity;
    float twinkle = 0.6 + 0.4 * sin(iTime * (2.0 + rnd * 4.0) + rnd * 60.0);
    float star = step(threshold, rnd) * smoothstep(0.12, 0.0, starDist) * twinkle;
    color += star * smoothstep(0.25, 0.9, uv.y) * float3(0.9, 0.95, 1.0);

    // Aurore : trois rideaux a des profondeurs differentes
    float2 p = float2((uv.x - 0.5) * aspect, uv.y);
    [unroll] for (int layer = 0; layer < 3; ++layer)
    {
        float fl = (float)layer;
        float2 lp = p + iParallax * (0.015 + 0.012 * fl);
        float t = iTime * (0.035 + 0.015 * fl);
        float height = 0.48 + 0.1 * fl + (fbm(float2(lp.x * (1.1 + 0.35 * fl) + t + fl * 11.0, t * 0.6)) - 0.5) * 0.45;
        float d = lp.y - height;
        float curtain = exp(-max(d, 0.0) * (5.0 + 2.0 * fl)) * smoothstep(-0.015, 0.0, d);
        float rays = 0.55 + 0.45 * valueNoise(float2(lp.x * 38.0 + fl * 7.0, t * 6.0));
        float3 tone = lerp(float3(0.10, 1.00, 0.55), float3(0.55, 0.30, 1.00), fl * 0.45 + 0.25 * sin(t * 2.0 + lp.x));
        color += tone * curtain * rays * intensity * (0.45 + bass) * 0.55;
    }

    // Montagnes : deux plans, le plus proche bouge le plus avec la souris
    float far = 0.20 + 0.07 * fbm(float2(uv.x * 2.2 * aspect + iParallax.x * 0.05, 3.0));
    color = lerp(color, float3(0.012, 0.02, 0.04), smoothstep(far + 0.002, far - 0.002, uv.y));
    float nearRidge = 0.11 + 0.08 * fbm(float2(uv.x * 3.1 * aspect + iParallax.x * 0.12, 9.0));
    color = lerp(color, float3(0.004, 0.007, 0.014), smoothstep(nearRidge + 0.002, nearRidge - 0.002, uv.y));

    fragColor = float4(color, 1.0);
}
