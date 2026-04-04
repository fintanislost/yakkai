# Color Lab — Active Project

## Scene: Workshop 3476236738

**Status:** Textures extracted and confirmed to have vivid colors (dark blue desk, bright sky window, purple-blue characters). Our renderer outputs them washed/desaturated. Investigating pipeline color loss.

## Key Finding
- BC3/DXT5 textures decoded to PNG show rich, saturated colors
- The same textures through our Vulkan renderer appear washed out and muted
- The color loss happens somewhere in the render pipeline, not in the texture data

## Textures
Extracted to `textures/` directory. Key assets:
- `窗户.png` — Window/sky (bright blue, white clouds)
- `桌.png` — Desk surface (deep navy blue)
- `1.png` — Puppet crop sheet: 砂糖/sleeping character (purple-blue)
- `2.png` — Puppet crop sheet: 星野/sitting character (dark blue clothing)
- `衣.png` / `衣2.png` — Clothing pieces

## Preview
`preview.gif` — Workshop preview showing target render quality

## Pipeline Investigation
The color flows through:
1. `.tex` file → VFS decrypts → LZ4 decompresses → BC3 raw blocks
2. Vulkan `VK_FORMAT_BC3_UNORM_BLOCK` → GPU hardware decode → linear RGBA
3. `genericimage4` shader: `color = texture(g_Texture0, uv) * g_Color4`
4. Translucent blend: `srcAlpha * src + (1-srcAlpha) * dst`
5. Composelayer: color grading + Gaussian blur
6. Screen output

Suspect areas for color loss:
- **UNORM vs SRGB decode** — UNORM treats sRGB-encoded data as linear, washing out colors
- **Translucent blend with semi-transparent layers** — alpha < 1 blends with bright clear color
- **Effect chain alpha handling** — ping-pong targets cleared to (0,0,0,0)
- **Composelayer final output** — blend mode may reduce saturation
