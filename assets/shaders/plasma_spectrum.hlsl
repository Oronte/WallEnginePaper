// @title Plasma spectral
// @description Plasma coloré qui respire avec le son, spectre audio en bas de l'écran et halo autour du curseur.
// @author WallEnginePaper
// Plasma + spectre audio : plasma colore qui respire avec le volume, barres du spectre en bas
// et halo autour du curseur.
//
// @param float saturation 0.75 0.0 1.0  "Saturation"
// @param float barHeight  0.22 0.0 0.6  "Hauteur des barres"
// @param bool  showBars   true          "Afficher le spectre"
// @param float glow       0.15 0.0 0.6  "Halo du curseur"
// @param float beatPulse  0.5  0.0 1.5  "Pulsation sur le beat"

float3 hsv2rgb(float3 c)
{
    float3 p = abs(frac(c.xxx + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(float3(1.0, 1.0, 1.0), saturate(p - 1.0), c.y);
}

void mainImage(out float4 fragColor, in float2 fragCoord)
{
    float2 uv = fragCoord / iResolution.xy;
    float2 p = (fragCoord - 0.5 * iResolution.xy) / iResolution.y + iParallax * 0.04;
    p *= 1.0 - 0.06 * beatPulse * iBeat;   // leger zoom a chaque beat
    float t = iTime * 0.3;

    float v = sin(p.x * 3.0 + t)
            + sin(p.y * 4.0 - t * 1.3)
            + sin((p.x + p.y) * 3.0 + t * 0.7)
            + sin(length(p * 4.0 + float2(sin(t), cos(t))) * 2.0);

    float3 color = hsv2rgb(float3(v * 0.12 + t * 0.05 + 0.04 * iTreble, saturation,
                                  0.28 + 0.2 * iAudioLevel + 0.18 * iBass + 0.25 * beatPulse * iBeat));

    if (showBars)
    {
        float x = uv.x * 64.0;
        int band = (int)x;
        float fx = frac(x);
        float h = 0.01 + AudioBand(band) * barHeight;
        float bar = step(uv.y, h) * smoothstep(0.0, 0.1, fx) * smoothstep(1.0, 0.9, fx);
        color = lerp(color, hsv2rgb(float3(uv.x + t * 0.1, 0.55, 1.0)), bar * 0.85);
    }

    float2 m = iMouse.xy / iResolution.y;
    float d = length(fragCoord / iResolution.y - m);
    color += glow * iMouseInside * exp(-d * 10.0);

    fragColor = float4(color, 1.0);
}
