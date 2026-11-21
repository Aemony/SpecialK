#pragma once

// Experimental D3D10 overlay renderer; see src/imgui/backends/imgui_d3d10.cpp

#include <dxgi.h>
#include "../imgui.h"

// True if config.apis.d3d10.experimental is on and the SwapChain belongs to a D3D10 device
bool SK_D3D10_IsD3D10SwapChain              (IDXGISwapChain* pSwapChain);

bool ImGui_ImplDX10_RenderDrawData          (ImDrawData* draw_data, IDXGISwapChain* pSwapChain);
void ImGui_ImplDX10_InvalidateDeviceObjects (void);

// DXGI Flip Model unbinds the backbuffer from the D3D10 output merger on every
//   Present; Blt Model (what D3D10 games are written for) does not. Games that
//     bind their render target once and keep drawing (e.g. cutscene players)
//       then render nothing. Save the bindings before Present and restore them
//         afterwards, but only for D3D10 SwapChains that SK converted to Flip Model.
struct SK_D3D10_FlipModelRebind
{
  SK_D3D10_FlipModelRebind (IDXGISwapChain* pSwapChain);
 ~SK_D3D10_FlipModelRebind (void);

  void restore (void);

private:
  struct ID3D10Device*            pDevice   = nullptr;
  struct ID3D10RenderTargetView*  rtvs  [8] = { };
  struct ID3D10DepthStencilView*  pDSV      = nullptr;
};
