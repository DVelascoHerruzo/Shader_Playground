#pragma once

namespace BFXTemp3 {
    texture2D BFX_RenderTex3 { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
    sampler2D RenderTex { Texture = BFX_RenderTex3; MagFilter = POINT; MinFilter = POINT; MipFilter = Point; };
    sampler2D RenderTexLinear { Texture = BFX_RenderTex3; };
    storage2D s_RenderTex { Texture = BFX_RenderTex3; };
}