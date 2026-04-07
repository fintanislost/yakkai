#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float resWidth;
    float resHeight;
    float scanlineIntensity;   // 0.0 = off, 1.0 = full black lines
    float curvature;           // 0.0 = flat, 1.0 = heavy barrel distortion
    float aberration;          // 0.0 = off, 1.0 = strong RGB split
    float vignetteStrength;    // 0.0 = off, 1.0 = heavy edge darkening
    float phosphorIntensity;   // 0.0 = off, 1.0 = strong RGB subpixel mask
    float brightness;          // 0.0 = no boost, 1.0 = 2x brightness
    float vibrance;            // 0.0 = normal, 1.0 = heavily boosted saturation
    float zoom;                // 0.0 = no zoom, 1.0 = 20% zoom in
};

layout(binding = 1) uniform sampler2D source;

vec2 barrelDistort(vec2 uv, float amt) {
    vec2 cc = uv - 0.5;
    float dist = dot(cc, cc);
    return uv + cc * dist * amt;
}

void main() {
    vec2 uv = qt_TexCoord0;

    // Zoom — scale UVs inward from center to crop edges
    float zoomFactor = 1.0 + zoom * 0.2;
    uv = (uv - 0.5) / zoomFactor + 0.5;

    // Barrel distortion (screen curvature)
    float curveAmt = curvature * 0.15;
    uv = barrelDistort(uv, curveAmt);

    // Black outside curved bounds
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0) * qt_Opacity;
        return;
    }

    // Chromatic aberration (RGB channel offset)
    float aberr = aberration * 0.004;
    float r = texture(source, uv + vec2(aberr, 0.0)).r;
    float g = texture(source, uv).g;
    float b = texture(source, uv - vec2(aberr, 0.0)).b;
    vec3 color = vec3(r, g, b);

    // Scanlines — 3px period so lines are visible on 1440p+ displays.
    // The sin produces a smooth wave; intensity controls how dark the
    // troughs get, ranging from subtle to full black.
    float scanline = sin(uv.y * resHeight * 3.14159265 / 3.0) * 0.5 + 0.5;
    color *= mix(1.0, scanline, scanlineIntensity * 0.85);

    // Phosphor dot pattern (RGB subpixel simulation)
    float px = mod(gl_FragCoord.x, 3.0);
    vec3 mask = vec3(
        smoothstep(0.0, 1.0, 1.0 - abs(px - 0.5)),
        smoothstep(0.0, 1.0, 1.0 - abs(px - 1.5)),
        smoothstep(0.0, 1.0, 1.0 - abs(px - 2.5))
    );
    color *= mix(vec3(1.0), mask, phosphorIntensity * 0.3);

    // Vignette — darkened edges
    vec2 vig = uv * (1.0 - uv);
    float vigFactor = pow(vig.x * vig.y * 15.0, 0.25);
    color *= mix(1.0, vigFactor, vignetteStrength);

    // Digital vibrance (saturation boost)
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, 1.0 + vibrance * 1.5);

    // Brightness compensation
    color *= 1.0 + brightness;

    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0) * qt_Opacity;
}
