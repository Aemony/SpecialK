#pragma warning ( disable : 3571 )

#include "HDR/common_defs.hlsl"

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
  float2 uv2 : TEXCOORD1;
  float3 uv3 : TEXCOORD2;
};

cbuffer viewportDims : register (b0)
{
  float4 viewport;
  float4 rtv_type;
};

sampler   sampler0    : register (s0);
Texture2D texture0    : register (t0);
//Texture2D hdrUnderlay : register (t1);
//Texture2D hdrHUD      : register (t2);

float4 main (PS_INPUT input) : SV_Target
{
  float4 out_col =
    texture0.Sample (sampler0, input.uv),
        orig_col =
         out_col;

  float  ui_alpha = saturate (input.col.a ) * saturate (out_col.a );
  float3 ui_color =           input.col.rgb *           out_col.rgb;

  //
  // HDR (HDR10 or scRGB)
  //
  if (viewport.z > 0.f)
  {
    bool hdr10 = (input.uv3.x < 0.0);
        
    if ( input.uv2.x > 0.0f &&
         input.uv2.y > 0.0f )
    {
      out_col.rgb =
        pow (
          RemoveSRGBCurve (out_col.rgb),
                input.uv2.yyy
            ) * input.uv2.xxx;
      out_col.a   = 1.0f;
    }

    else
    {
      out_col =
        float4 ( RemoveSRGBCurve (        ui_color ),
          1.0f - RemoveSRGBAlpha ( 1.0f - ui_alpha ) );
    }

    float hdr_scale  = hdr10 ? ( -input.uv3.x / 10000.0 )
                             :    input.uv3.x;

    float hdr_offset = 0.0f;//hdr10 ? 0.0f : input.uv3.z;
                            //hdr_scale -= hdr_offset;

    float4 hdr_out =
      float4 (   ( hdr10 ?
        LinearToST2084 (
          REC709toREC2020 (              out_col.rgb * ui_alpha ) * hdr_scale ) :
     Clamp_scRGB_StripNaN ( expandGamut (out_col.rgb * hdr_scale, 0.0333) )
                 )                                   + hdr_offset, 
                   hdr10 ?         LinearToPQY (       ui_alpha, 5.5)
                         :                             out_col.a );

    hdr_out.g = (orig_col.g <= 0.00013 && orig_col.g >= -0.00013) ? 0.0f : hdr_out.g;
    hdr_out.r = (orig_col.r <= 0.00013 && orig_col.r >= -0.00013) ? 0.0f : hdr_out.r;
    hdr_out.b = (orig_col.b <= 0.00013 && orig_col.b >= -0.00013) ? 0.0f : hdr_out.b;
    hdr_out.a = (orig_col.a <= 0.00013 && orig_col.a >= -0.00013) ? 0.0f : hdr_out.a;

    float alpha_mul =
      (hdr10 ? 1.0
             : ui_alpha ); // Use linear alpha in scRGB
    
    if (hdr10)
    {
      hdr_out.rgba =    clamp (hdr_out.rgba, 0.0, 1.0);
      hdr_out.rgba *=
        smoothstep ( 0.006978,
                     0.016667, hdr_out.rgba );
    }

    return
      float4 ( hdr_out.rgb * alpha_mul,
               hdr_out.a );
  }

  //
  // SDR (sRGB/Rec 709) -- We use a linear view for consistency with HDR blending
  //
  if (rtv_type.x != 0.0f) // sRGB View
  {
    out_col = float4 ( RemoveSRGBCurve (       ui_color) * (1.0f - RemoveSRGBAlpha (1.0f - ui_alpha)),
                1.0f - RemoveSRGBAlpha (1.0f - ui_alpha) );
  }
  else
  {
    out_col = float4 (ui_color * ui_alpha,
                                 ui_alpha);
  }
  
  return out_col;
};