#pragma once

// Experimental D3D10 overlay renderer; see src/imgui/backends/imgui_d3d10.cpp

#include <dxgi.h>
#include "../imgui.h"

// True if config.apis.d3d10.experimental is on and the SwapChain belongs to a D3D10 device
bool SK_D3D10_IsD3D10SwapChain              (IDXGISwapChain* pSwapChain);

bool ImGui_ImplDX10_RenderDrawData          (ImDrawData* draw_data, IDXGISwapChain* pSwapChain);
void ImGui_ImplDX10_InvalidateDeviceObjects (void);
