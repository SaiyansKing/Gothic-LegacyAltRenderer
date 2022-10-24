    Texture2D theTexture : register(t0);
    float4 constants : register(c0);
    SamplerState theSampler = sampler_state
    {
        addressU = Clamp;
        addressV = Clamp;
        mipfilter = NONE;
        minfilter = POINT;
        magfilter = POINT;
    };

    struct PixelShaderInput
    {
        float4 pos : SV_POSITION;
        float2 tex : TEXCOORD0;
        float4 color : COLOR0;
    };

    float4 main(PixelShaderInput input) : SV_TARGET
    {
        float4 color = theTexture.Sample(theSampler, input.tex);
        color = saturate(lerp(float4(0.5, 0.5, 0.5, 0.5), color, constants.z));
        return saturate(pow(color.rgba * constants.y, constants.x));
    }