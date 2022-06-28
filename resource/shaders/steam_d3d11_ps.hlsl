#pragma warning ( disable : 3571 )

#include "HDR/common_defs.hlsl"

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR;
  float2 uv  : TEXCOORD0;
  float2 uv2 : TEXCOORD1;
};

sampler   PS_QUAD_Sampler   : register (s0);
Texture2D PS_QUAD_Texture2D : register (t0);

float4 main (PS_INPUT input) : SV_Target
{
  float4 gamma_exp  = float4 (input.uv2.yyy, 1.f);
  float4 linear_mul = float4 (input.uv2.xxx, 1.f);

  float4 out_col =
    PS_QUAD_Texture2D.Sample (PS_QUAD_Sampler, input.uv);

  out_col =
    float4 (RemoveSRGBCurve (input.col.rgb * out_col.rgb),
                             input.col.a   * out_col.a);

  // Negative = HDR10
  if (linear_mul.x < 0.0)
  {
    out_col.rgb =
      ApplyREC2084Curve ( REC709toREC2020 (out_col.rgb),
                            -linear_mul.x );

  }

  // Positive = scRGB
  else
    out_col *= linear_mul;

  return
    float4 (out_col.rgb, saturate (out_col.a));
}