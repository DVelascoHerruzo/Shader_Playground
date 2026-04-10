// Includes
#include "Includes/BaropeFX_Common.fxh"
#include "Includes/BaropeFX_TempTex1.fxh"

// Number of Palettes
#ifndef BFX_PALETTE_COUNT
    #define BFX_PALETTE_COUNT 4
#endif

// -----------------------------------------------Menu Settings-------------------------------------------------

// Manual input for color Palette, 8 bit
#if BFX_PALETTE_COUNT > 0
uniform float3 _Color1 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 1";
    ui_type = "color";
> = float3(0.0, 0.0, 0.0);
#endif

#if BFX_PALETTE_COUNT > 1
uniform float3 _Color2 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 2";
    ui_type = "color";
> = float3(0.1, 0.1, 0.1);
#endif

#if BFX_PALETTE_COUNT > 2
uniform float3 _Color3 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 3";
    ui_type = "color";
> = float3(0.2, 0.2, 0.2);
#endif

#if BFX_PALETTE_COUNT > 3
uniform float3 _Color4 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 4";
    ui_type = "color";
> = float3(0.3, 0.3, 0.3);
#endif

#if BFX_PALETTE_COUNT > 4
uniform float3 _Color5 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 5";
    ui_type = "color";
> = float3(0.4, 0.4, 0.4);
#endif

#if BFX_PALETTE_COUNT > 5
uniform float3 _Color6 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 6";
    ui_type = "color";
> = float3(0.6, 0.6, 0.6);
#endif

#if BFX_PALETTE_COUNT > 6
uniform float3 _Color7 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 7";
    ui_type = "color";
> = float3(0.8, 0.8, 0.8);
#endif

#if BFX_PALETTE_COUNT > 7
uniform float3 _Color8 <
    ui_category_closed = true;
    ui_category = "Manual Colors";
    ui_label = "Color 8";
    ui_type = "color";
> = float3(1.0, 1.0, 1.0);
#endif

// Random Palette setting
uniform bool _UseRandomPalette <
    ui_label = "Generate Random Palette";
> = false;

//Color count
uniform int _ColorCount <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_min = 3; ui_max = 16;
    ui_label = "Color Count";
    ui_type = "slider";
> = 4;

//Random seed selector setting
uniform int _Seed <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_min = 0; ui_max = 100000000;
    ui_label = "Seed";
    ui_type = "slider";
> = 0;

// Which Hue combo to use
uniform uint _HueMode <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_type = "combo";
    ui_label = "Hue Mode";
    ui_tooltip = "What combination of Hue values to use";
    ui_items = "Monochromatic\0"
                "Analogous\0"
                "Complementary\0"
                "Triadic Complementary\0"
                "Tetradic Complementary\0";
> = 0;

// Hue Valeu Variance Selection Setting
uniform float2 _HueContrast <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_label = "Hue Contrast";
    ui_tooltip = "Minimum/Maximum for how much the hue increases across the palette";
    ui_type = "drag";
> = float2(0.0f, 1.0f);

// Lumninance Valeu Selection Setting
uniform float2 _Luminance < 
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_label = "Luminance";
    ui_tooltip = "Minimum/Maximum for the luminance of the palette";
    ui_type = "drag";
> = float2(0.0f, 1.0f);


// Lumninance Valeu Variance Selection Setting
uniform float2 _LuminanceContrast <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_label = "Luminance Contrast";
    ui_tooltip = "Minimum/Maximum for how much the luminance varies across the palette";
    ui_type = "drag";
> = float2(0.0f, 1.0f);

// Chroma Valeu Selection Setting
uniform float2 _Chroma <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_label = "Chroma";
    ui_tooltip = "Minimum/Maximum for the chroma of the palette";
    ui_type = "drag";
> = float2(0.0f, 1.0f);

// Chroma Valeu Variance Selection Setting
uniform float2 _ChromaContrast <
    ui_category_closed = true;
    ui_category = "Random Palette Settings";
    ui_label = "Chroma Contrast";
    ui_tooltip = "Minimum/Maximum for how much the chroma varies across the palette";
    ui_type = "drag";
> = float2(0.0f, 1.0f);

sampler2D PaletteSwap { Texture = BFXTemp1::BFX_RenderTex1; MagFilter = POINT; MinFilter = POINT; MipFilter = POINT; };
float4 PS_EndPass(float4 position : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET { return tex2D(PaletteSwap, uv).rgba; }

// -----------------------------------------------Helper Matices-------------------------------------------------

// sRGB to XYZ 
static const float3x3 RGB_TO_XYZ = float3x3(
    0.412165612, 0.211859107, 0.0883097947,
    0.536275208, 0.6807189584, 0.2818474174,
    0.0514575653, 0.107406579, 0.6302613616);

// XYZ to OKLAB
static const float3x3 XYZ_TO_OKLAB = float3x3(
    +0.2104542553, +1.9779984951, +0.0259040371,
    +0.7936177850, -2.4285922050, +0.7827717662,
    +0.0040720468, +0.4505937099, -0.8086757660);

// OKLAB to XYZ
static const float3x3 OKLAB_TO_XYZ = float3x3(
    +4.0767416621, -1.2684380046, -0.0041960863,
    -3.3077115913, +2.6097574011, -0.7034186147,
    +0.2309699292, -0.3413193965, +1.7076147010);

// XYZ to sRGB
static const float3x3 XYZ_TO_RGB = float3x3(
    1, 1, 1,
    +0.3963377774f, -0.1055613458f, -0.0894841775f,
    +0.2158037573f, -0.0638541728f, -1.2914855480f);

// -----------------------------------------------Helper Operations-------------------------------------------------

// OKLAB color space to RGB color space conversion
float3 OKLABtoRGB(float3 col) {
    col = mul(col, XYZ_TO_RGB);
    col = col * col * col;
    col = mul(col, OKLAB_TO_XYZ);
    return col;
}

// OKLCH Color encoder coordinates to OKLAB to RGB
float3 OKLCHtoRGB(float3 col){
    float3 oklab = 0.0f;
    oklab.r = col.r;
    oklab.g = col.g * cos(col.b);
    oklab.b = col.g * sin(col.b);

    return OKLABtoRGB(oklab);
}

//Hashing function 
float hash(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 0x789221U) + 0x1376312589U;
    return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
}

// -----------------------------------------------Passes-------------------------------------------------

float4 PS_PaletteSwap (float4 position : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
    uint seed = _Seed;

    // Randomized variables
    // Hue
    float hueBase = hash(seed) * 2 * BFX_PI;
    float hueContrast = lerp(_HueContrast.x, _HueContrast.y, hash(seed + 2));

    // Luminance
    float L = lerp(_Luminance.x, _Luminance.y, hash(seed + 13));
    float luminanceContrast = lerp(_LuminanceContrast.x, _LuminanceContrast.y, hash(seed + 3));

    // Chroma
    float C = lerp(_Chroma.x, _Chroma.y, hash(seed + 5));
    float chromaContrast = lerp(_ChromaContrast.x, _ChromaContrast.y, hash(seed + 7));

    float3 colors[16]; // Maximum of 16 colors in the palette

    // Iterates trough the color to select the Pallete based on the OKLAB color space
    // and the selected Hue base Settings
    for(int i = 0; i < _ColorCount; i++){
        float linearIterator = (float)i / (_ColorCount - 1);

        float hueOffset = hueContrast * linearIterator * 2 * BFX_PI + (BFX_PI / 4.0f);

        if(_HueMode == 0) hueOffset *= 0.0;
        if(_HueMode == 1) hueOffset *= 0.25;
        if(_HueMode == 2) hueOffset *= 0.33;
        if(_HueMode == 3) hueOffset *= 0.66;
        if(_HueMode == 4) hueOffset *= 0.75;

        float luminanceOffset = L + luminanceContrast * linearIterator;
        float chromaOffset = C + chromaContrast * linearIterator;

        colors[i] = OKLCHtoRGB( float3(luminanceOffset, chromaOffset, hueBase + hueOffset));
    }

    float newUV = saturate(tex2D(ReShade::BackBuffer, uv).r);

    int paletteIndex = floor(newUV * BFX_PALETTE_COUNT) + 1;
    if (newUV == 1)
        paletteIndex = BFX_PALETTE_COUNT;

    if(_UseRandomPalette){
        paletteIndex = floor(newUV * _ColorCount) + 1;
        if (newUV == 1)
            paletteIndex = _ColorCount;
    }

    float3 color = 0;

    switch (paletteIndex) {

        #if BFX_PALETTE_COUNT > 0
            case 1:
                color = _Color1;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 1
            case 2:
                color = _Color2;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 2
            case 3:
                color = _Color3;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 3
            case 4:
                color = _Color4;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 4
            case 5:
                color = _Color5;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 5
            case 6:
                color = _Color6;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 6
            case 7:
                color = _Color7;
            break;
        #endif

        #if BFX_PALETTE_COUNT > 7
            case 8:
                color = _Color8;
            break;
        #endif

        default:
        break;
    }

    if(_UseRandomPalette){
        color = colors[paletteIndex - 1];
    } 

    return float4(saturate(color), 1.0f);

}

// -----------------------------------------------Technique-------------------------------------------------

technique BFX_PaletteSwap < ui_label = "Palette Swap"; ui_tooltip = "Swap greyscale colors with other colors"; > {
    pass {
        RenderTarget = BFXTemp1::BFX_RenderTex1;

        VertexShader = PostProcessVS;
        PixelShader = PS_PaletteSwap;
    }

    pass EndPass {

        VertexShader = PostProcessVS;
        PixelShader = PS_EndPass;
    }
}

