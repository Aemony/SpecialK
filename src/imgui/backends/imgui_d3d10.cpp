// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

// ImGui DirectX10 binding (experimental), based on the upstream imgui_impl_dx10
// https://github.com/ocornut/imgui

//
// Experimental D3D10 overlay renderer.
//
//   D3D10 games run on a D3D11 device internally, and Special K treats them as
//     D3D11 for everything except drawing: commands submitted through the D3D11
//       immediate context of a D3D10 device are silently discarded. So the
//         shared D3D11 code path (ImGui init, input, NewFrame, ...) runs as
//           usual, and only the final draw is redirected here, where it is
//             submitted through the ID3D10Device instead.
//

#include <SpecialK/stdafx.h>

#include <d3d10.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_d3d10.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"  D3D10  "

namespace
{
  struct SK_ImGui_D3D10_Ctx
  {
    SK_ComPtr <ID3D10Device>              pDevice;
    SK_ComPtr <ID3D10Buffer>              pVB;
    SK_ComPtr <ID3D10Buffer>              pIB;
    int                                   VBSize = 0;
    int                                   IBSize = 0;
    SK_ComPtr <ID3D10VertexShader>        pVS;
    SK_ComPtr <ID3D10InputLayout>         pLayout;
    SK_ComPtr <ID3D10Buffer>              pVSConstants;
    SK_ComPtr <ID3D10PixelShader>         pPSFont;  // A8 font atlas
    SK_ComPtr <ID3D10SamplerState>        pSampler;
    SK_ComPtr <ID3D10ShaderResourceView>  pFontSRV;
    ImTextureID                           fontTexID = nullptr;
    SK_ComPtr <ID3D10RasterizerState>     pRasterizer;
    SK_ComPtr <ID3D10BlendState>          pBlend;
    SK_ComPtr <ID3D10DepthStencilState>   pDepthStencil;
    bool                                  failed    = false;
  } _ctx;

  using D3D10StateBlockMaskEnableAll_pfn = HRESULT (WINAPI *)(D3D10_STATE_BLOCK_MASK*);
  using D3D10CreateStateBlock_pfn        = HRESULT (WINAPI *)(ID3D10Device*, D3D10_STATE_BLOCK_MASK*, ID3D10StateBlock**);

  const char* const _vs_src = R"(
    cbuffer vertexBuffer : register (b0) { float4x4 ProjectionMatrix; };
    struct VS_INPUT  { float2 pos : POSITION;    float4 col : COLOR0; float2 uv : TEXCOORD0; };
    struct PS_INPUT  { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
    PS_INPUT main (VS_INPUT input)
    {
      PS_INPUT output;
      output.pos = mul (ProjectionMatrix, float4 (input.pos.xy, 0.f, 1.f));
      output.col = input.col;
      output.uv  = input.uv;
      return output;
    })";

  const char* const _ps_src = R"(
    struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
    sampler   sampler0 : register (s0);
    Texture2D texture0 : register (t0);
    float4 main (PS_INPUT input) : SV_Target
    {
#ifdef FONT
      return input.col * float4 (1.f, 1.f, 1.f, texture0.Sample (sampler0, input.uv).a);
#else
      return input.col * texture0.Sample (sampler0, input.uv);
#endif
    })";

  SK_ComPtr <ID3DBlob>
  SK_D3D10_CompileImGuiShader (const char* src, const char* target, const D3D_SHADER_MACRO* defines)
  {
    SK_ComPtr <ID3DBlob> pCode, pErrors;

    if (FAILED (SK_D3D_Compile (src, strlen (src), nullptr, defines, nullptr,
                                "main", target, 0, 0, &pCode.p, &pErrors.p)))
    {
      SK_LOGi0 ( L"ImGui D3D10 shader (%hs) failed to compile: %hs", target,
                   pErrors != nullptr ? (const char *)pErrors->GetBufferPointer ()
                                      : "(no compiler available)" );
      return nullptr;
    }

    return pCode;
  }

  bool
  SK_D3D10_CreateFontTexture (ID3D10Device* pDevice)
  {
    ImGuiIO& io =
      ImGui::GetIO ();

    unsigned char* pixels = nullptr;
    int            width  = 0,
                   height = 0;

    // Same atlas format as the D3D11 backend, so both can coexist
    io.Fonts->GetTexDataAsAlpha8 (&pixels, &width, &height);

    if (pixels == nullptr)
      return false;

    D3D10_TEXTURE2D_DESC
      desc                  = { };
      desc.Width            = width;
      desc.Height           = height;
      desc.MipLevels        = 1;
      desc.ArraySize        = 1;
      desc.Format           = DXGI_FORMAT_A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.Usage            = D3D10_USAGE_IMMUTABLE;
      desc.BindFlags        = D3D10_BIND_SHADER_RESOURCE;

    D3D10_SUBRESOURCE_DATA
      init             = { };
      init.pSysMem     = pixels;
      init.SysMemPitch = width;

    SK_ComPtr <ID3D10Texture2D> pTex;

    if (FAILED (pDevice->CreateTexture2D (&desc, &init, &pTex.p)))
      return false;

    _ctx.pFontSRV = nullptr;

    if (FAILED (pDevice->CreateShaderResourceView (pTex, nullptr, &_ctx.pFontSRV.p)))
      return false;

    // The D3D11 backend owns io.Fonts->TexID; remember which ID means "font atlas"
    _ctx.fontTexID = io.Fonts->TexID;

    return true;
  }

  bool
  SK_D3D10_CreateDeviceObjects (ID3D10Device* pDevice)
  {
    static const D3D_SHADER_MACRO font_defines [] = { { "FONT", "1" }, { nullptr, nullptr } };

    auto pVSCode  = SK_D3D10_CompileImGuiShader (_vs_src, "vs_4_0", nullptr);
    auto pPSFCode = SK_D3D10_CompileImGuiShader (_ps_src, "ps_4_0", font_defines);

    if (! (pVSCode && pPSFCode))
      return false;

    if (FAILED (pDevice->CreateVertexShader (pVSCode->GetBufferPointer  (), pVSCode->GetBufferSize  (), &_ctx.pVS.p))     ||
        FAILED (pDevice->CreatePixelShader  (pPSFCode->GetBufferPointer (), pPSFCode->GetBufferSize (), &_ctx.pPSFont.p)))
      return false;

    const D3D10_INPUT_ELEMENT_DESC layout [] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof (ImDrawVert, pos), D3D10_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof (ImDrawVert, uv),  D3D10_INPUT_PER_VERTEX_DATA, 0 },
      { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof (ImDrawVert, col), D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };

    if (FAILED (pDevice->CreateInputLayout (layout, 3, pVSCode->GetBufferPointer (),
                                                       pVSCode->GetBufferSize    (), &_ctx.pLayout.p)))
      return false;

    D3D10_BUFFER_DESC
      cb_desc                = { };
      cb_desc.ByteWidth      = sizeof (float) * 16;
      cb_desc.Usage          = D3D10_USAGE_DYNAMIC;
      cb_desc.BindFlags      = D3D10_BIND_CONSTANT_BUFFER;
      cb_desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

    if (FAILED (pDevice->CreateBuffer (&cb_desc, nullptr, &_ctx.pVSConstants.p)))
      return false;

    D3D10_BLEND_DESC
      blend                          = { };
      blend.BlendEnable [0]          = TRUE;
      blend.SrcBlend                 = D3D10_BLEND_SRC_ALPHA;
      blend.DestBlend                = D3D10_BLEND_INV_SRC_ALPHA;
      blend.BlendOp                  = D3D10_BLEND_OP_ADD;
      blend.SrcBlendAlpha            = D3D10_BLEND_ONE;
      blend.DestBlendAlpha           = D3D10_BLEND_INV_SRC_ALPHA;
      blend.BlendOpAlpha             = D3D10_BLEND_OP_ADD;
      blend.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;

    D3D10_RASTERIZER_DESC
      raster                 = { };
      raster.FillMode        = D3D10_FILL_SOLID;
      raster.CullMode        = D3D10_CULL_NONE;
      raster.ScissorEnable   = TRUE;
      raster.DepthClipEnable = TRUE;

    D3D10_DEPTH_STENCIL_DESC
      depth                  = { };
      depth.DepthEnable      = FALSE;
      depth.DepthWriteMask   = D3D10_DEPTH_WRITE_MASK_ALL;
      depth.DepthFunc        = D3D10_COMPARISON_ALWAYS;
      depth.StencilEnable    = FALSE;
      depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp =
      depth.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
      depth.FrontFace.StencilFunc   = D3D10_COMPARISON_ALWAYS;
      depth.BackFace                = depth.FrontFace;

    D3D10_SAMPLER_DESC
      sampler                = { };
      sampler.Filter         = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
      sampler.AddressU       = D3D10_TEXTURE_ADDRESS_WRAP;
      sampler.AddressV       = D3D10_TEXTURE_ADDRESS_WRAP;
      sampler.AddressW       = D3D10_TEXTURE_ADDRESS_WRAP;
      sampler.ComparisonFunc = D3D10_COMPARISON_ALWAYS;
      sampler.MaxLOD         = D3D10_FLOAT32_MAX;

    if (FAILED (pDevice->CreateBlendState        (&blend,   &_ctx.pBlend.p))        ||
        FAILED (pDevice->CreateRasterizerState   (&raster,  &_ctx.pRasterizer.p))   ||
        FAILED (pDevice->CreateDepthStencilState (&depth,   &_ctx.pDepthStencil.p)) ||
        FAILED (pDevice->CreateSamplerState      (&sampler, &_ctx.pSampler.p)))
      return false;

    return
      SK_D3D10_CreateFontTexture (pDevice);
  }

  bool
  SK_D3D10_EnsureBuffers (ID3D10Device* pDevice, const ImDrawData* draw_data)
  {
    if (_ctx.pVB == nullptr || _ctx.VBSize < draw_data->TotalVtxCount)
    {
      _ctx.pVB    = nullptr;
      _ctx.VBSize = draw_data->TotalVtxCount + 5000;

      D3D10_BUFFER_DESC
        desc                = { };
        desc.Usage          = D3D10_USAGE_DYNAMIC;
        desc.ByteWidth      = _ctx.VBSize * sizeof (ImDrawVert);
        desc.BindFlags      = D3D10_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

      if (FAILED (pDevice->CreateBuffer (&desc, nullptr, &_ctx.pVB.p)))
        return false;
    }

    if (_ctx.pIB == nullptr || _ctx.IBSize < draw_data->TotalIdxCount)
    {
      _ctx.pIB    = nullptr;
      _ctx.IBSize = draw_data->TotalIdxCount + 10000;

      D3D10_BUFFER_DESC
        desc                = { };
        desc.Usage          = D3D10_USAGE_DYNAMIC;
        desc.ByteWidth      = _ctx.IBSize * sizeof (ImDrawIdx);
        desc.BindFlags      = D3D10_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

      if (FAILED (pDevice->CreateBuffer (&desc, nullptr, &_ctx.pIB.p)))
        return false;
    }

    return true;
  }
}

void
ImGui_ImplDX10_InvalidateDeviceObjects (void)
{
  _ctx = SK_ImGui_D3D10_Ctx { };
}

bool
SK_D3D10_IsD3D10SwapChain (IDXGISwapChain* pSwapChain)
{
  if (! config.apis.d3d10.experimental || pSwapChain == nullptr)
    return false;

  SK_ComPtr <ID3D10Device> pDev10;

  return
    SUCCEEDED (pSwapChain->GetDevice (__uuidof (ID3D10Device), (void **)&pDev10.p));
}

bool
ImGui_ImplDX10_RenderDrawData (ImDrawData* draw_data, IDXGISwapChain* pSwapChain)
{
  if (draw_data == nullptr || pSwapChain == nullptr)
    return false;

  SK_ComPtr <ID3D10Device> pDevice;

  if (FAILED (pSwapChain->GetDevice (__uuidof (ID3D10Device), (void **)&pDevice.p)))
    return false;

  if (_ctx.pDevice.p != pDevice.p)
  {
    ImGui_ImplDX10_InvalidateDeviceObjects ();

    _ctx.pDevice = pDevice;

    if (! SK_D3D10_CreateDeviceObjects (pDevice))
    {
      SK_LOGi0 (L"Failed to create ImGui D3D10 device objects; overlay disabled.");
      _ctx.failed = true;
    }

    else
      SK_LOGi0 (L"ImGui D3D10 renderer initialized.");
  }

  if (_ctx.failed)
    return false;

  // The D3D11 backend normally sets these during its draw (for the next frame),
  //   which is skipped for D3D10. Without DisplaySize, ImGui lays out for a
  //     128x128 screen; SK also stores the backbuffer size in
  //       DisplayFramebufferScale, which the cursor code uses to map the mouse
  //         into ImGui space (left at 1x1, every click lands near 0,0).
  DXGI_SWAP_CHAIN_DESC swap_desc = { };
  if (SUCCEEDED (pSwapChain->GetDesc (&swap_desc)))
  {
    const ImVec2 backbuffer_size (
      static_cast <float> (swap_desc.BufferDesc.Width),
      static_cast <float> (swap_desc.BufferDesc.Height) );

    ImGui::GetIO ().DisplaySize             = backbuffer_size;
    ImGui::GetIO ().DisplayFramebufferScale = backbuffer_size;
  }

  // The font atlas was rebuilt (e.g. font scale changed)
  if (ImGui::GetIO ().Fonts->TexID != _ctx.fontTexID)
    SK_D3D10_CreateFontTexture (pDevice);

  if (draw_data->TotalVtxCount <= 0 || draw_data->DisplaySize.x <= 0.0f
                                    || draw_data->DisplaySize.y <= 0.0f)
    return true;

  if (! SK_D3D10_EnsureBuffers (pDevice, draw_data))
    return false;

  // Upload vertex / index data
  ImDrawVert* vtx_dst = nullptr;
  ImDrawIdx*  idx_dst = nullptr;

  if (FAILED (_ctx.pVB->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&vtx_dst)))
    return false;

  if (FAILED (_ctx.pIB->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&idx_dst)))
  {
    _ctx.pVB->Unmap ();
    return false;
  }

  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList* cmd_list = draw_data->CmdLists [n];

    memcpy (vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof (ImDrawVert));
    memcpy (idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof (ImDrawIdx));

    vtx_dst += cmd_list->VtxBuffer.Size;
    idx_dst += cmd_list->IdxBuffer.Size;
  }

  _ctx.pVB->Unmap ();
  _ctx.pIB->Unmap ();

  // Orthographic projection
  {
    float* mvp = nullptr;

    if (FAILED (_ctx.pVSConstants->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&mvp)))
      return false;

    const float L = draw_data->DisplayPos.x;
    const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float T = draw_data->DisplayPos.y;
    const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    const float ortho [16] = {
      2.0f/(R-L),   0.0f,         0.0f, 0.0f,
      0.0f,         2.0f/(T-B),   0.0f, 0.0f,
      0.0f,         0.0f,         0.5f, 0.0f,
      (R+L)/(L-R),  (T+B)/(B-T),  0.5f, 1.0f,
    };

    memcpy (mvp, ortho, sizeof (ortho));

    _ctx.pVSConstants->Unmap ();
  }

  // The back buffer RTV is created per-frame, so that nothing here holds a
  //   reference to the back buffer when the game calls ResizeBuffers.
  SK_ComPtr <ID3D10Texture2D>        pBackBuffer;
  SK_ComPtr <ID3D10RenderTargetView> pRTV;

  if (FAILED (pSwapChain->GetBuffer (0, __uuidof (ID3D10Texture2D), (void **)&pBackBuffer.p)))
    return false;

  D3D10_TEXTURE2D_DESC bb_desc = { };
  pBackBuffer->GetDesc (&bb_desc);

  if (FAILED (pDevice->CreateRenderTargetView (pBackBuffer, nullptr, &pRTV.p)))
    return false;

  // Save the game's pipeline state
  static auto _D3D10StateBlockMaskEnableAll =
    (D3D10StateBlockMaskEnableAll_pfn)SK_GetProcAddress (L"d3d10.dll", "D3D10StateBlockMaskEnableAll");
  static auto _D3D10CreateStateBlock =
    (D3D10CreateStateBlock_pfn)       SK_GetProcAddress (L"d3d10.dll", "D3D10CreateStateBlock");

  SK_ComPtr <ID3D10StateBlock> pStateBlock;


  if (_D3D10StateBlockMaskEnableAll != nullptr && _D3D10CreateStateBlock != nullptr)
  {
    D3D10_STATE_BLOCK_MASK                  mask = { };
    _D3D10StateBlockMaskEnableAll         (&mask);
    if (SUCCEEDED (_D3D10CreateStateBlock (pDevice, &mask, &pStateBlock.p)))
      pStateBlock->Capture ();
  }

  // Setup render state
  D3D10_VIEWPORT
    vp          = { };
    vp.Width    = bb_desc.Width;
    vp.Height   = bb_desc.Height;
    vp.MaxDepth = 1.0f;

  const UINT  stride = sizeof (ImDrawVert),
              offset = 0;
  const float blend_factor [4] = { 0.f, 0.f, 0.f, 0.f };

  pDevice->ClearState             ();
  pDevice->OMSetRenderTargets     (1, &pRTV.p, nullptr);
  pDevice->RSSetViewports         (1, &vp);
  pDevice->IASetInputLayout       (_ctx.pLayout);
  pDevice->IASetVertexBuffers     (0, 1, &_ctx.pVB.p, &stride, &offset);
  pDevice->IASetIndexBuffer       (_ctx.pIB, sizeof (ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT
                                                                     : DXGI_FORMAT_R32_UINT, 0);
  pDevice->IASetPrimitiveTopology (D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pDevice->VSSetShader            (_ctx.pVS);
  pDevice->VSSetConstantBuffers   (0, 1, &_ctx.pVSConstants.p);
  pDevice->PSSetSamplers          (0, 1, &_ctx.pSampler.p);
  pDevice->OMSetBlendState        (_ctx.pBlend, blend_factor, 0xffffffff);
  pDevice->OMSetDepthStencilState (_ctx.pDepthStencil, 0);
  pDevice->RSSetState             (_ctx.pRasterizer);

  // Draw
  int    vtx_offset = 0,
         idx_offset = 0;
  ImVec2 clip_off   = draw_data->DisplayPos;

  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList* cmd_list = draw_data->CmdLists [n];

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer [cmd_i];

      // User callbacks are written against the D3D11 backend; skip them
      if (pcmd->UserCallback != nullptr)
        continue;

      const ImVec2 clip_min (pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
      const ImVec2 clip_max (pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);

      if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
        continue;

      // Only the font atlas exists on the D3D10 device; other textures are
      //   D3D11 views that cannot be sampled here yet, so skip those draws.
      if (pcmd->GetTexID () != _ctx.fontTexID)
        continue;

      const D3D10_RECT r = {
        (LONG)clip_min.x, (LONG)clip_min.y,
        (LONG)clip_max.x, (LONG)clip_max.y
      };

      pDevice->PSSetShader          (_ctx.pPSFont);
      pDevice->PSSetShaderResources (0, 1, &_ctx.pFontSRV.p);
      pDevice->RSSetScissorRects    (1, &r);
      pDevice->DrawIndexed          (pcmd->ElemCount, pcmd->IdxOffset + idx_offset,
                                                      pcmd->VtxOffset + vtx_offset);
    }

    idx_offset += cmd_list->IdxBuffer.Size;
    vtx_offset += cmd_list->VtxBuffer.Size;
  }

  // Restore the game's pipeline state
  if (pStateBlock != nullptr)
    pStateBlock->Apply ();
  else
    pDevice->ClearState ();

  return true;
}
