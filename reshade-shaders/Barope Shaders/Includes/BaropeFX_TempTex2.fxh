#pragma once

namespace BFXTemp2 {
    texture2D BFX_RenderTex2 { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
    sampler2D RenderTex { Texture = BFX_RenderTex2; MagFilter = POINT; MinFilter = POINT; MipFilter = Point; };
    sampler2D RenderTexLinear { Texture = BFX_RenderTex2; };
    storage2D s_RenderTex { Texture = BFX_RenderTex2; };
}