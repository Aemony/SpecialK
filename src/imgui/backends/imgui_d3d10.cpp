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
#include <d3d10_1.h>
#include <SpecialK/render/dxgi/dxgi_util.h>

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

// Every early exit below would otherwise leave the overlay invisible with
//   nothing in the log; report each kind of failure once.
#define SK_D3D10_LOG_ONCE(failure, ...)                         \
  do {                                                          \
    static volatile LONG _logged_##failure = FALSE;             \
    if (! InterlockedExchange (&_logged_##failure, TRUE))       \
      SK_LOGi0 (__VA_ARGS__);                                   \
  } while (0)

// Like SK_D3D10_IsD3D10SwapChain (...), but without logging and regardless of
//   ExperimentalD3D10: that setting only turns on the overlay, while
//     the Flip Model fixes keyed off this test are needed by every D3D10 game whose
//       SwapChain SK wraps (Shattered Horizon crashed creating an RTV on the emulated
//         D3D11 backbuffer with the setting off). Used when wrapping every SwapChain.
// Answering to ID3D10Device does not make a device a D3D10 device. Once anything
//   calls ID3D11Device1::CreateDeviceContextState (..., __uuidof (ID3D10Device1), ...)
//     on a D3D11 device -- even if that call fails -- the device answers to the D3D10
//       interfaces for good, and ID3D10Device1::GetFeatureLevel reports its D3D11
//         feature level. Its immediate context still renders D3D11, so the D3D10
//           overlay drawn there is never seen, and the D3D10 Flip Model handling
//             would be applied to a D3D11 game.
//
//   Seen in Cities XXL (D3D11, feature level 11_0; most likely the Steam overlay
//     makes that call): "SwapChain ... uses D3D10.1 at feature level 11_0" and
//       no overlay with ExperimentalD3D10=1.
//
//   Real D3D10 games create their device through d3d10.dll or d3d10_1.dll, which
//     cannot create feature levels above 10_1; Cities XXL loads neither.
bool
SK_D3D10_IsD3D10RuntimeDevice (IUnknown* pDevice)
{
  if (pDevice == nullptr)
    return false;

  if ( SK_GetModuleHandleW (L"d3d10.dll")   == nullptr &&
       SK_GetModuleHandleW (L"d3d10_1.dll") == nullptr )
    return false;

  SK_ComPtr <ID3D10Device1> pDev1;

  if ( SUCCEEDED (pDevice->QueryInterface (__uuidof (ID3D10Device1), (void **)&pDev1.p)) &&
       pDev1->GetFeatureLevel () > D3D10_FEATURE_LEVEL_10_1 )
    return false;

  return true;
}

// GetDevice (ID3D10Device), but only for devices of the D3D10 runtimes (see above)
static HRESULT
SK_D3D10_GetSwapChainDevice (IDXGISwapChain* pSwapChain, ID3D10Device** ppDevice)
{
  *ppDevice = nullptr;

  const HRESULT hr =
    pSwapChain->GetDevice (__uuidof (ID3D10Device), (void **)ppDevice);

  if (FAILED (hr))
    return hr;

  if (! SK_D3D10_IsD3D10RuntimeDevice (*ppDevice))
  {
    SK_ComPtr <ID3D10Device1> pDev1;
    (*ppDevice)->QueryInterface (__uuidof (ID3D10Device1), (void **)&pDev1.p);

    const UINT level =
      pDev1.p != nullptr ? pDev1->GetFeatureLevel () : 0;

    SK_D3D10_LOG_ONCE ( D3D11WithD3D10Interface,
      L"SwapChain %p answers to ID3D10Device, but its device is a D3D11 device "
      L"(feature level %d_%d, %ws); treating it as D3D11.", pSwapChain,
        (level >> 12) & 0xF, (level >> 8) & 0xF,
          level > D3D10_FEATURE_LEVEL_10_1 ? L"above what D3D10 can create"
                                           : L"no D3D10 runtime loaded" );

    std::exchange (*ppDevice, nullptr)->Release ();

    return E_NOINTERFACE;
  }

  return hr;
}

bool
SK_D3D10_SwapChainUsesD3D10 (IDXGISwapChain* pSwapChain)
{
  if (pSwapChain == nullptr)
    return false;

  SK_ComPtr <ID3D10Device> pDev10;

  return
    SUCCEEDED (SK_D3D10_GetSwapChainDevice (pSwapChain, &pDev10.p));
}

bool
SK_D3D10_IsD3D10SwapChain (IDXGISwapChain* pSwapChain)
{
  if (! config.apis.d3d10.enabled () || pSwapChain == nullptr)
    return false;

  SK_ComPtr <ID3D10Device> pDev10;

  const HRESULT hr =
    SK_D3D10_GetSwapChainDevice (pSwapChain, &pDev10.p);

  if (FAILED (hr))
  {
    // Expected for real D3D11 games; for a D3D10 game it means the overlay is
    //   drawn through the D3D11 renderer, whose commands D3D10 discards.
    SK_D3D10_LOG_ONCE ( NoD3D10Device,
      L"SwapChain %p has no ID3D10Device (hr=%x); using the D3D11 overlay renderer. "
      L"If this is a D3D10 game, the overlay will not be visible.", pSwapChain, hr );
    return false;
  }

  return true;
}

// The device alone cannot tell D3D10 from D3D10.1: devices made by either runtime
//   answer to ID3D10Device1, and both report feature level 10_0 unless the game
//     asked D3D10CreateDevice[AndSwapChain]1 for something else. So:
//
//   * Feature level 10_1 or 9_x: only the D3D10.1 runtime can create those
//   * Feature level 10_0: D3D10.1 if only its runtime is loaded (MT Framework
//                           games use D3D10.1 at feature level 10_0 and never
//                             load d3d10.dll)
bool
SK_D3D10_IsD3D10_1Device (ID3D10Device1* pDev1, D3D10_FEATURE_LEVEL1* pLevel = nullptr)
{
  const D3D10_FEATURE_LEVEL1 level =
    pDev1->GetFeatureLevel ();

  if (pLevel != nullptr)
     *pLevel = level;

  return
    ( level != D3D10_FEATURE_LEVEL_10_0 ) ||
    ( SK_GetModuleHandleW (L"d3d10_1.dll") != nullptr &&
      SK_GetModuleHandleW (L"d3d10.dll")   == nullptr );
}

// IWrapDXGISwapChain constructors, for SwapChains of D3D10 devices: offer
//   Experimental D3D10 if this is the first SwapChain SK wraps (d3d11.cpp)
void
SK_D3D10_OfferExperimentalSupport (IDXGISwapChain* pSwapChain)
{
  extern void SK_D3D10_OfferExperimentalSupport (bool bD3D10_1, LONG lMaxLiveSwapChains);

  if (config.apis.d3d10.setting != SK_NoPreference || pSwapChain == nullptr)
    return;

  SK_ComPtr <ID3D10Device1> pDev1;

  if (FAILED (pSwapChain->GetDevice (__uuidof (ID3D10Device1), (void **)&pDev1.p)))
    return;

  // The constructor has already counted this SwapChain
  SK_D3D10_OfferExperimentalSupport (SK_D3D10_IsD3D10_1Device (pDev1), 1);
}

const wchar_t*
SK_D3D10_GetAPIName (IDXGISwapChain* pSwapChain)
{
  SK_ComPtr <ID3D10Device1> pDev1;

  if ( pSwapChain == nullptr ||
       FAILED (pSwapChain->GetDevice (__uuidof (ID3D10Device1), (void **)&pDev1.p)) )
    return L"D3D10";

  D3D10_FEATURE_LEVEL1 level = D3D10_FEATURE_LEVEL_10_0;

  const bool bD3D10_1 =
    SK_D3D10_IsD3D10_1Device (pDev1, &level);

  SK_D3D10_LOG_ONCE ( APIName,
    L"SwapChain %p uses %ws at feature level %d_%d.", pSwapChain,
      bD3D10_1 ? L"D3D10.1" : L"D3D10", (level >> 12) & 0xF, (level >> 8) & 0xF );

  return
    bD3D10_1 ? L"D3D10.1" : L"D3D10";
}

bool
ImGui_ImplDX10_RenderDrawData (ImDrawData* draw_data, IDXGISwapChain* pSwapChain)
{
  if (draw_data == nullptr || pSwapChain == nullptr)
    return false;

  SK_ComPtr <ID3D10Device> pDevice;

  if (const HRESULT hr = pSwapChain->GetDevice (__uuidof (ID3D10Device), (void **)&pDevice.p); FAILED (hr))
  {
    SK_D3D10_LOG_ONCE (GetDevice, L"Overlay not drawn: IDXGISwapChain::GetDevice (ID3D10Device) failed (hr=%x)", hr);
    return false;
  }

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
  {
    SK_D3D10_LOG_ONCE (EnsureBuffers, L"Overlay not drawn: could not create vertex/index buffers (%d vertices, %d indices)",
                                        draw_data->TotalVtxCount, draw_data->TotalIdxCount);
    return false;
  }

  // Upload vertex / index data
  ImDrawVert* vtx_dst = nullptr;
  ImDrawIdx*  idx_dst = nullptr;

  if (const HRESULT hr = _ctx.pVB->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&vtx_dst); FAILED (hr))
  {
    SK_D3D10_LOG_ONCE (MapVB, L"Overlay not drawn: vertex buffer Map failed (hr=%x)", hr);
    return false;
  }

  if (const HRESULT hr = _ctx.pIB->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&idx_dst); FAILED (hr))
  {
    SK_D3D10_LOG_ONCE (MapIB, L"Overlay not drawn: index buffer Map failed (hr=%x)", hr);
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

    if (const HRESULT hr = _ctx.pVSConstants->Map (D3D10_MAP_WRITE_DISCARD, 0, (void **)&mvp); FAILED (hr))
    {
      SK_D3D10_LOG_ONCE (MapCB, L"Overlay not drawn: constant buffer Map failed (hr=%x)", hr);
      return false;
    }

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

  if (const HRESULT hr = pSwapChain->GetBuffer (0, __uuidof (ID3D10Texture2D), (void **)&pBackBuffer.p); FAILED (hr))
  {
    SK_D3D10_LOG_ONCE (GetBuffer, L"Overlay not drawn: IDXGISwapChain::GetBuffer (0, ID3D10Texture2D) failed (hr=%x)", hr);
    return false;
  }

  D3D10_TEXTURE2D_DESC bb_desc = { };
  pBackBuffer->GetDesc (&bb_desc);

  // Describe what we are drawing into once; the swapchain's window should be the game's.
  SK_RunOnce (
    SK_LOGi0 ( L"Drawing overlay into SwapChain %p (HWND=%p, game HWND=%p): %ux%u %hs, %u sample(s)",
                 pSwapChain, swap_desc.OutputWindow, game_window.hWnd, bb_desc.Width, bb_desc.Height,
                 SK_DXGI_FormatToStr (bb_desc.Format).data (), bb_desc.SampleDesc.Count )
  );

  // SK's swapchain wrapper may hand out a typeless (fake) backbuffer, e.g. while
  //   overriding the swap effect or for HDR; a view without a format fails on it.
  D3D10_RENDER_TARGET_VIEW_DESC
    rtv_desc               = { };
    rtv_desc.Format        = DirectX::IsTypeless (bb_desc.Format) ?
                             DirectX::MakeTypelessUNORM (
                             DirectX::MakeTypelessFLOAT (bb_desc.Format)) : bb_desc.Format;
    rtv_desc.ViewDimension = bb_desc.SampleDesc.Count > 1 ? D3D10_RTV_DIMENSION_TEXTURE2DMS
                                                          : D3D10_RTV_DIMENSION_TEXTURE2D;

  if (const HRESULT hr = pDevice->CreateRenderTargetView (pBackBuffer, &rtv_desc, &pRTV.p); FAILED (hr))
  {
    SK_D3D10_LOG_ONCE ( CreateRTV, L"Overlay not drawn: CreateRenderTargetView failed (hr=%x, backbuffer %hs, view %hs)", hr,
                          SK_DXGI_FormatToStr (bb_desc.Format).data (), SK_DXGI_FormatToStr (rtv_desc.Format).data () );
    return false;
  }

  // Save the game's pipeline state (D3D10.1-only games may not have d3d10.dll loaded)
  static auto _GetD3D10Proc = [](const char* szFunc) -> FARPROC
  {
    FARPROC proc =
      SK_GetProcAddress (L"d3d10.dll",   szFunc);
    return proc != nullptr ? proc :
      SK_GetProcAddress (L"d3d10_1.dll", szFunc);
  };

  static auto _D3D10StateBlockMaskEnableAll =
    (D3D10StateBlockMaskEnableAll_pfn)_GetD3D10Proc ("D3D10StateBlockMaskEnableAll");
  static auto _D3D10CreateStateBlock =
    (D3D10CreateStateBlock_pfn)       _GetD3D10Proc ("D3D10CreateStateBlock");

  if (_D3D10StateBlockMaskEnableAll == nullptr || _D3D10CreateStateBlock == nullptr)
  {
    SK_D3D10_LOG_ONCE (NoStateBlock, L"D3D10 state block functions not found; the game's pipeline state will be cleared after drawing the overlay.");
  }

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

SK_D3D10_FlipModelRebind::SK_D3D10_FlipModelRebind (IDXGISwapChain* pSwapChain)
{
  // Runs on every Present of every DXGI game; skip the GetDevice (...) query
  //   unless SK has wrapped a D3D10 SwapChain. Only those can have been converted
  //     to Flip Model by SK, which is the only case that needs the rebind.
  extern volatile LONG SK_DXGI_LiveWrappedD3D10SwapChains;

  if (ReadAcquire (&SK_DXGI_LiveWrappedD3D10SwapChains) == 0)
    return;

  // Needed whether or not Experimental D3D10 (the overlay) is enabled
  if (! SK_D3D10_SwapChainUsesD3D10 (pSwapChain))
    return;

  DXGI_SWAP_CHAIN_DESC desc = { };

  if ( FAILED (pSwapChain->GetDesc (&desc)) ||
       desc.SwapEffect < DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
       SK_GetCurrentRenderBackend ().active_traits.bOriginallyFlip )
    return; // Not Flip Model, or the game asked for Flip Model itself

  if (FAILED (pSwapChain->GetDevice (__uuidof (ID3D10Device), (void **)&pDevice)))
  {
    pDevice = nullptr;
    return;
  }

  pDevice->OMGetRenderTargets (D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &pDSV);
}

void
SK_D3D10_FlipModelRebind::restore (void)
{
  if (pDevice == nullptr)
    return;

  bool any_bound = (pDSV != nullptr);
  for (auto* rtv : rtvs) any_bound |= (rtv != nullptr);

  if (any_bound)
  {
    pDevice->OMSetRenderTargets (D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, pDSV);
  }

  for (auto*& rtv : rtvs) if (rtv != nullptr) { rtv->Release (); rtv = nullptr; }
  if (pDSV != nullptr) { pDSV->Release (); pDSV = nullptr; }

  std::exchange (pDevice, nullptr)->Release ();
}

SK_D3D10_FlipModelRebind::~SK_D3D10_FlipModelRebind (void)
{
  restore ();
}
