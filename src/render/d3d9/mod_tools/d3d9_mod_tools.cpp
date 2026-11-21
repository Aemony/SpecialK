/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/

#include <SpecialK/stdafx.h>
#include <SpecialK/resource.h>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"D3D9 Tools"

#include <SpecialK/render/d3d9/d3d9_backend.h>
#include <SpecialK/render/d3d9/d3d9_texmgr.h>
#include <SpecialK/render/d3d9/d3d9_device.h>
#include <SpecialK/render/d3d9/d3d9_screenshot.h>

#include <imgui/backends/imgui_d3d9.h>

#include <d3d9.h>

using namespace SK::D3D9;

//
// D3D9 Render Mod Tools
//
//   The texture, render target, shader and vertex buffer views that make up
//   the "D3D9 Render Mod Tools" window.
//

// State that belongs to the D3D9 backend (d3d9.cpp)
extern std::unique_ptr <SK_Thread_HybridSpinlock> lock_vs;
extern std::unique_ptr <SK_Thread_HybridSpinlock> lock_ps;
extern std::unique_ptr <SK_Thread_HybridSpinlock> lock_vb;

extern SK_LazyGlobal <std::unordered_map <uint32_t, ShaderDisassembly>> ps_disassembly;
extern SK_LazyGlobal <std::unordered_map <uint32_t, ShaderDisassembly>> vs_disassembly;

void
SK_D3D9_DrawFileList (bool& can_scroll)
{
  const float font_size =
    ImGui::GetFont ()->FontSize * ImGui::GetIO ().FontGlobalScale;

  ImGui::PushItemWidth (500.0f);

  struct enumerated_source_s
  {
    std::string            name       = "invalid";
    std::vector <uint32_t> checksums  = { };

    struct {
      std::vector < std::pair < uint32_t, SK::D3D9::TexRecord > >
                 records              = {  };
      uint64_t   size                 = 0ULL;
    } streaming, blocking;

    uint64_t     totalSize (void) const { return streaming.size + blocking.size; };
  };

  static std::vector <enumerated_source_s> sources;
  static SK::D3D9::TexList                 injectable;
  static std::vector < std::wstring >      archives;
  static bool                              list_dirty = true;
  static int                               sel        = 0;

  auto EnumerateSource =
    [](unsigned int archive_no) ->
      enumerated_source_s
      {
        enumerated_source_s source = { };

        char szFileName [MAX_PATH + 2] = { };

        if (archive_no != std::numeric_limits <unsigned int>::max ()) {
          sprintf (szFileName, "%ws", archives [archive_no].c_str ());
        }

        else strncpy_s (szFileName, MAX_PATH, "Regular Filesystem", _TRUNCATE);

        source.name = szFileName;

        for ( auto& it : injectable )
        {
          if (it.second.archive == archive_no)
          {
            switch (it.second.method)
            {
              case DontCare:
              case Streaming:
                source.streaming.records.emplace_back (it.first, it.second);
                source.streaming.size += it.second.size;
                break;

              case Blocking:
                source.blocking.records.emplace_back (it.first, it.second);
                source.blocking.size += it.second.size;
                break;
            }

            source.checksums.emplace_back (it.first);
          }
        }

        return source;
      };

  if (list_dirty)
  {
    SK::D3D9::TextureManager& tex_mgr =
      SK_D3D9_GetTextureManager ();

    tex_mgr.getInjectableTextures (injectable);
    tex_mgr.getTextureArchives    (archives);

    sources.clear  ();

        sel = 0;
    int idx = 0;

    // First the .7z Data Sources
    for ( auto& it : archives )
    {
      std::ignore = it;

      sources.emplace_back (EnumerateSource (idx++));
    }

    // Then the Straight Filesystem
    sources.emplace_back (EnumerateSource (std::numeric_limits <unsigned int>::max ()));

    list_dirty = false;
  }

  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleColor (ImGuiCol_Border,             ImVec4 (0.4f, 0.6f, 0.9f, 1.0f));

#define FILE_LIST_WIDTH  (font_size * 20)
#define FILE_LIST_HEIGHT (font_size * (sources.size () + 3))

  ImGui::BeginChild ( ImGui::GetID ("Source List"),
                        ImVec2 ( FILE_LIST_WIDTH, FILE_LIST_HEIGHT ),
                          true,
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened );

  if (ImGui::IsWindowHovered ())
    can_scroll = false;

  auto DataSourceTooltip =
    [](int sel) ->
      void
      {
        auto& source =
          sources [sel];

        ImGui::BeginTooltip ();
        ImGui::TextColored  (ImVec4 (0.9f, 0.7f, 0.3f, 1.f), "Data Source  (%s)", source.name.c_str ());
        ImGui::Separator    ();

        ImGui::BeginGroup      (    );
        ImGui::TextUnformatted ( "Total Size:     ");
        ImGui::TextUnformatted ( "Blocking Data:  ");
        ImGui::TextUnformatted ( "Streaming Data: ");
        ImGui::EndGroup        (    );

        ImGui::SameLine        (    );

        ImGui::BeginGroup      (    );
        ImGui::TextUnformatted ( "" );
        ImGui::Text            ( "%4lu File%c", source.blocking.records.size  (),
                                                source.blocking.records.size  () != 1 ? 's' : ' ' );
        ImGui::Text            ( "%4lu File%c", source.streaming.records.size (),
                                                source.streaming.records.size () != 1 ? 's' : ' ' );
        ImGui::EndGroup        (    );

        ImGui::SameLine        (    );

        ImGui::BeginGroup      (    );
        ImGui::Text            ( "  %#5.2f MiB",
                                   (double)source.totalSize ()   / (1024.0 * 1024.0) );
        ImGui::Text            ( " (%#5.2f MiB)",
                                   (double)source.blocking.size  / (1024.0 * 1024.0) );
        ImGui::Text            ( " (%#5.2f MiB)",
                                   (double)source.streaming.size / (1024.0 * 1024.0) );
        ImGui::EndGroup        (    );

        ImGui::EndTooltip    ();
      };

  if (! sources.empty ())
  {
    static      int last_sel = 0;
    static bool sel_changed  = false;

    if (sel != last_sel)
      sel_changed = true;

    last_sel = sel;

    for ( int line = 0; line < sk::narrow_cast <int> (sources.size ()); line++)
    {
      if (line == sel)
      {
        bool selected = true;
        ImGui::Selectable (sources [line].name.c_str (), &selected);

        if (sel_changed)
        {
          ImGui::SetScrollHereY       (0.5f); // 0.0f:top, 0.5f:center, 1.0f:bottom
          ImGui::SetKeyboardFocusHere (    );

          sel_changed = false;
        }
      }

      else
      {
        bool selected = false;

        if (ImGui::Selectable (sources [line].name.c_str (), &selected))
        {
          sel_changed = true;
          //tex_dbg_idx                 =  line;
          sel                         =  line;
          //debug_tex_id                =  textures_used_last_dump [line];
        }
      }

      if (ImGui::IsItemHovered ())
        DataSourceTooltip (line);
    }

    ImGui::EndChild      ();

    ImVec2 list_size = ImGui::GetItemRectSize ();

    ImGui::PopStyleColor ();
    ImGui::PopStyleVar   ();

    ImGui::SameLine      ();

    ImGui::BeginGroup    ();

    ImGui::PushStyleColor  (ImGuiCol_Border, ImVec4 (0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::BeginChild ( ImGui::GetID ("Texture Selection"),
                           ImVec2 (font_size * 30, list_size.y - font_size * 2),
                             true );

    if (ImGui::IsWindowHovered ())
      can_scroll = false;

    SK::D3D9::TextureManager& tex_mgr =
      SK_D3D9_GetTextureManager ();

    ImGui::BeginGroup ();
    for ( auto it : sources [sel].checksums )
    {
      const TexRecord& inject_tex =
        tex_mgr.getInjectableTexture (it);

      if (inject_tex.size != 0)
      {
        ImGui::TextColored ( ImVec4 (0.9f, 0.6f, 0.3f, 1.0f), " %08x    ", it );
      }
    }
    ImGui::EndGroup   ();
    ImGui::SameLine   ();
    ImGui::BeginGroup ();
    for ( auto it : sources [sel].checksums )
    {
      const TexRecord& inject_tex =
        tex_mgr.getInjectableTexture (it);

      if (inject_tex.size != 0)
      {
        bool streaming =
          inject_tex.method == Streaming;

        ImGui::TextColored ( streaming ?
                               ImVec4 ( 0.2f,  0.90f, 0.3f, 1.0f ) :
                               ImVec4 ( 0.90f, 0.3f,  0.2f, 1.0f ),
                                 "  %s    ",
                                   streaming ? "Streaming" :
                                               " Blocking" );
      }
    }
    ImGui::EndGroup   ();
    ImGui::SameLine   ();
    ImGui::BeginGroup ();
    for ( auto it : sources [sel].checksums )
    {
      const TexRecord& inject_tex =
        tex_mgr.getInjectableTexture (it);

      if (inject_tex.size != 0)
      {
        ImGui::TextColored ( ImVec4 (1.f, 1.f, 1.f, 1.f), "%#5.2f MiB  ",
                            (double)inject_tex.size / (1024.0 * 1024.0) );
      }
    }
    ImGui::EndGroup   ();


    ImGui::EndChild      (   );
    ImGui::PopStyleColor ( 1 );

    if (trigger_reset == reset_stage_e::Clear)
    {
      if (ImGui::Button ("  Refresh Data Sources  "))
      {
        tex_mgr.refreshDataSources ();
        list_dirty = true;
      }

      ImGui::SameLine ();

      if (ImGui::Button ("  Reload All Textures  "))
      {
        SK_D3D9_TriggerReset (false);
      }

      if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("Not all games support this, and some may lockup... reloading individual textures is always safe");
    }

    ImGui::EndGroup ();
  }

  else
    ImGui::EndChild   ();

  ImGui::PopItemWidth ();
}



void
SK_D3D9_LiveShaderClassView (SK::D3D9::ShaderClass shader_type, bool& can_scroll)
{
  ImGui::BeginGroup ();

  static float last_width = 256.0f;
  const  float font_size  = ImGui::GetFont ()->FontSize * ImGui::GetIO ().FontGlobalScale;

  struct shader_class_imp_s
  {
    std::vector <std::string> contents   = {  };
    bool                      dirty      = true;
    uint32_t                  last_sel   =    0;
    int                            sel   =   -1;
    float                     last_ht    = 256.0f;
    ImVec2                    last_min   = ImVec2 (0.0f, 0.0f);
    ImVec2                    last_max   = ImVec2 (0.0f, 0.0f);
  };

  static auto& _last_frame = last_frame.get ();

  struct {
    shader_class_imp_s vs;
    shader_class_imp_s ps;
  } static list_base;

  shader_class_imp_s*
    list    = ( shader_type == SK::D3D9::ShaderClass::Pixel ? &list_base.ps :
                                                              &list_base.vs );

  SK::D3D9::ShaderTracker*
    tracker = ( shader_type == SK::D3D9::ShaderClass::Pixel ? tracked_ps.getPtr () :
                                                              tracked_vs.getPtr () );

  std::vector <uint32_t>
    shaders   ( shader_type == SK::D3D9::ShaderClass::Pixel ? _last_frame.pixel_shaders.cbegin  () :
                                                              _last_frame.vertex_shaders.cbegin (),
                shader_type == SK::D3D9::ShaderClass::Pixel ? _last_frame.pixel_shaders.cend    () :
                                                              _last_frame.vertex_shaders.cend   () );

  std::unordered_map <uint32_t, SK::D3D9::ShaderDisassembly>&
    disassembly = ( shader_type == SK::D3D9::ShaderClass::Pixel ? *ps_disassembly :
                                                                  *vs_disassembly );

  const char*
    szShaderWord =  shader_type == SK::D3D9::ShaderClass::Pixel ? "Pixel" :
                                                                  "Vertex";

  if (list->dirty)
  {
        list->sel = -1;
    int idx       =  0;
        list->contents.clear ();

    // The underlying list is unsorted for speed, but that's not at all
    //   intuitive to humans, so sort the thing when we have the RT view open.
    std::sort ( shaders.begin (),
                shaders.end   () );



    for ( auto it : shaders )
    {
      char szDesc [16] = { };

      sprintf (szDesc, "%08" PRIxPTR, (uintptr_t)it);

      list->contents.emplace_back (szDesc);

      if ((uint32_t)it == list->last_sel)
      {
        list->sel = idx;
        //tracked_rt.tracking_tex = render_textures [sel];
      }

      ++idx;
    }
  }

  if (ImGui::IsMouseHoveringRect (list->last_min, list->last_max))
  {
         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) { list->sel--;  ImGui::GetIO ().WantCaptureKeyboard = true; }
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) { list->sel++;  ImGui::GetIO ().WantCaptureKeyboard = true; }
  }

  ImGui::PushStyleVar   (ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor (ImGuiCol_Border, ImVec4 (0.9f, 0.7f, 0.5f, 1.0f));

  ImGui::BeginChild ( ImGui::GetID (szShaderWord),
                      ImVec2 ( font_size * 7.0f, std::max (font_size * 15.0f, list->last_ht)), true,
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened );

  if (ImGui::IsWindowHovered ())
  {
    can_scroll = false;

    ImGui::BeginTooltip ();
    ImGui::TextColored  (ImVec4 (0.9f, 0.6f, 0.2f, 1.0f), "You can cancel all render passes using the selected %s shader to disable an effect", szShaderWord);
    ImGui::Separator    ();
    ImGui::BulletText   ("Press [ while the mouse is hovering this list to select the previous shader");
    ImGui::BulletText   ("Press ] while the mouse is hovering this list to select the next shader");
    ImGui::EndTooltip   ();

         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) { list->sel--;  ImGui::GetIO ().WantCaptureKeyboard = true; }
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) { list->sel++;  ImGui::GetIO ().WantCaptureKeyboard = true; }
  }

  if (! shaders.empty ())
  {
    struct {
      int  last_sel    = 0;
      bool sel_changed = false;
    } static shader_state [3];

    int&  last_sel    = shader_state [static_cast <int> (shader_type)].last_sel;
    bool& sel_changed = shader_state [static_cast <int> (shader_type)].sel_changed;

    if (list->sel != last_sel)
      sel_changed = true;

    last_sel = list->sel;

    for ( size_t line = 0; line < shaders.size (); line++ )
    {
      if (line == static_cast <size_t> (list->sel))
      {
        bool selected    = true;

        ImGui::Selectable (list->contents [line].c_str (), &selected);

        if (sel_changed)
        {
          ImGui::SetScrollHereY       (0.5f);
          ImGui::SetKeyboardFocusHere (    );

          sel_changed     = false;
          list->last_sel  = (uint32_t)shaders [list->sel];
          tracker->crc32c = (uint32_t)shaders [list->sel];
        }
      }

      else
      {
        bool selected    = false;

        if (ImGui::Selectable (list->contents [line].c_str (), &selected))
        {
          sel_changed     = true;
          list->sel       = (int)line;
          list->last_sel  = (uint32_t)shaders [list->sel];
          tracker->crc32c = (uint32_t)shaders [list->sel];
        }
      }
    }
  }

  ImGui::EndChild      ();
  ImGui::PopStyleColor ();

  ImGui::SameLine      ();
  ImGui::BeginGroup    ();

  if (ImGui::IsItemHovered (ImGuiHoveredFlags_RectOnly)) {
         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) list->sel--;
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) list->sel++;
  }

  if (tracker->crc32c != 0x00)
  {
    ImGui::BeginGroup ();
    ImGui::Checkbox ( shader_type == SK::D3D9::ShaderClass::Pixel ? "Cancel Draws Using Selected Pixel Shader" :
                                                                    "Cancel Draws Using Selected Vertex Shader",
                        &tracker->cancel_draws );  ImGui::SameLine ();

    LONG num_draws =
     ReadAcquire (&tracker->num_draws);

    if (tracker->cancel_draws)
      ImGui::TextDisabled ("%lu Skipped Draw%c Last Frame (%lu textures)", num_draws, num_draws != 1 ? 's' : ' ', tracker->used_textures.size () );
    else
      ImGui::TextDisabled ("%lu Draw%c Last Frame         (%lu textures)", num_draws, num_draws != 1 ? 's' : ' ', tracker->used_textures.size () );

    ImGui::Checkbox ( shader_type == SK::D3D9::ShaderClass::Pixel ? "Clamp Texture Coordinates For Selected Pixel Shader" :
                                                                    "Clamp Texture Coordinates For Selected Vertex Shader",
                        &tracker->clamp_coords );

    ImGui::Separator      ();
    ImGui::EndGroup       ();

    if (ImGui::IsItemHovered (ImGuiHoveredFlags_RectOnly) && ! tracker->used_textures.empty ())
    {
      ImGui::BeginTooltip ();

      D3DFORMAT fmt = D3DFMT_UNKNOWN;

      for ( auto it : tracker->used_textures )
      {
        IDirect3DBaseTexture9* pTex = it;//ISKTextureD3D9* pTex = tex_mgr.getTexture (it)->d3d9_tex;

        if (pTex /*&& pTex->pTex*/)
        {
          D3DSURFACE_DESC desc;
          if (SUCCEEDED (((IDirect3DTexture9 *)pTex/*->pTex*/)->GetLevelDesc (0, &desc)))
          {
            fmt = desc.Format;
            ImGui::Image ( pTex/*->pTex*/, ImVec2  ( std::max (64.0f, (float)desc.Width / 16.0f),
          ((float)desc.Height / (float)desc.Width) * std::max (64.0f, (float)desc.Width / 16.0f) ),
                                       ImVec2  (0,0),             ImVec2  (1,1),
                                       ImColor (255,255,255,255), ImColor (242,242,13,255) );
          }

          ImGui::SameLine ();

          ImGui::BeginGroup ();
          ImGui::TextUnformatted ("Texture:");
          ImGui::TextUnformatted ("Format: ");
          ImGui::EndGroup   ();

          ImGui::SameLine    ();

          ImGui::BeginGroup  ();
          ImGui::Text        ("%08lx", it);
          ImGui::Text        ("%hs",   SK_D3D9_FormatToStr (fmt).c_str ());
          ImGui::EndGroup    ();
        }
      }

      ImGui::EndTooltip ();
    }

    SK_ImGui_AutoFont fixed_font (ImGui::GetIO ().Fonts->Fonts[1]);

    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.80f, 0.80f, 1.0f, 1.0f));
    ImGui::TextWrapped    (disassembly [tracker->crc32c].header.c_str ());

    ImGui::SameLine       ();
    ImGui::BeginGroup     ();
    ImGui::TreePush       ("");
    ImGui::Spacing        (); ImGui::Spacing ();
    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.666f, 0.666f, 0.666f, 1.0f));

    ImGui::PushItemWidth (font_size * 25);

    char szName [128] = { };

    ImGui::PushID (tracker->crc32c);

    for ( auto& it : tracker->constants )
    {
      ImGui::PushID (it.RegisterIndex);

      if (! it.struct_members.empty ())
      {
        ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (0.9f, 0.1f, 0.7f, 1.0f));
        ImGui::TextUnformatted (it.Name);
        ImGui::PopStyleColor   ();

        for ( auto& it2 : it.struct_members )
        {
          ImGui::PushID (it2.RegisterIndex);

          bool tweakable = false;

          if (it2.Type == D3DXPT_FLOAT)
          {
            snprintf ( szName, 127, "[%s] %-32s",
                         shader_type == SK::D3D9::ShaderClass::Pixel ? "ps" :
                                                                       "vs",
                           it2.Name );

            if (it2.Class == D3DXPC_VECTOR ||
                it2.Class == D3DXPC_SCALAR)
            {
              switch (it2.Columns)
              {
                case 1:
                  tweakable = true;
                  ImGui::Checkbox    (szName, &it2.Override);
                  ImGui::SameLine    ( );
                  ImGui::InputFloat  ("", it2.Data);
                  break;
                case 2:
                  tweakable = true;
                  ImGui::Checkbox    (szName, &it2.Override);
                  ImGui::SameLine    ( );
                  ImGui::InputFloat2 ("", it2.Data);
                  break;
                case 3:
                  tweakable = true;
                  ImGui::Checkbox    (szName, &it2.Override);
                  ImGui::SameLine    ( );
                  ImGui::InputFloat3 ("", it2.Data);
                  break;
                case 4:
                  tweakable = true;
                  ImGui::Checkbox    (szName, &it2.Override);
                  ImGui::SameLine    ( );
                  ImGui::InputFloat4 ("", it2.Data);
                  break;
              }
            }
          }

          if (! tweakable)
          {
            snprintf ( szName, 127, "[%s] %-32s :(%c%-3u)",
                         shader_type == SK::D3D9::ShaderClass::Pixel ? "ps" :
                                                                       "vs",
                           it2.Name,
                             it2.RegisterSet != D3DXRS_SAMPLER ? 'c' : 's',
                               it2.RegisterIndex );

            ImGui::TreePush (""); ImGui::TextColored (ImVec4 (0.45f, 0.75f, 0.45f, 1.0f), szName); ImGui::TreePop ();
          }

          ImGui::PopID ();
        }

        ImGui::Separator ();
      }

      else
      {
        bool tweakable = false;

        snprintf ( szName, 127, "[%s] %-32s",
                     shader_type == SK::D3D9::ShaderClass::Pixel ? "ps" :
                                                                   "vs",
                       it.Name );

        if (it.Type == D3DXPT_FLOAT)
        {
          if (it.Class == D3DXPC_VECTOR ||
              it.Class == D3DXPC_SCALAR)
          {
            switch (it.Columns)
            {
              case 1:
                tweakable = true;
                ImGui::Checkbox    (szName, &it.Override);
                ImGui::SameLine    ( );
                ImGui::InputFloat  ("", it.Data);
                break;
              case 2:
                tweakable = true;
                ImGui::Checkbox    (szName, &it.Override);
                ImGui::SameLine    ( );
                ImGui::InputFloat2 ("", it.Data);
                break;
              case 3:
                tweakable = true;
                ImGui::Checkbox    (szName, &it.Override);
                ImGui::SameLine    ( );
                ImGui::InputFloat3 ("", it.Data);
                break;
              case 4:
                tweakable = true;
                ImGui::Checkbox    (szName, &it.Override);
                ImGui::SameLine    ( );
                ImGui::InputFloat4 ("", it.Data);
                break;
            }
          }
        }

        if (it.Type == D3DXPT_BOOL)
        {
          if (it.Class == D3DXPC_SCALAR)
          {
            switch (it.Columns)
            {
              case 1:
                tweakable = true;
                ImGui::Checkbox (szName, &it.Override);
                ImGui::SameLine ( );
                ImGui::Checkbox ("Enable", reinterpret_cast <bool *> (it.Data));
                break;
            }
          }
        }

        if (! tweakable)
        {
          snprintf ( szName, 127, "[%s] %-32s :(%c%-3u)",
                       shader_type == SK::D3D9::ShaderClass::Pixel ? "ps" :
                                                                     "vs",
                         it.Name,
                           it.RegisterSet != D3DXRS_SAMPLER ? 'c' : 's',
                             it.RegisterIndex );

          ImGui::TreePush (""); ImGui::TextColored (ImVec4 (0.45f, 0.75f, 0.45f, 1.0f), szName); ImGui::TreePop ();
        }
      }
      ImGui::PopID ();
    }
    ImGui::PopItemWidth   ();
    ImGui::TreePop        ();
    ImGui::EndGroup       ();

    ImGui::Separator      ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.99f, 0.99f, 0.01f, 1.0f));
    ImGui::TextWrapped    (disassembly [tracker->crc32c].code.c_str ());

    ImGui::Separator      ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.5f, 0.95f, 0.5f, 1.0f));
    ImGui::TextWrapped    (disassembly [tracker->crc32c].footer.c_str ());

    ImGui::PopStyleColor  (4);
    ImGui::PopID          ( );
  }
  else
    tracker->cancel_draws = false;

  ImGui::EndGroup      ();

  list->last_ht    = ImGui::GetItemRectSize ().y;

  list->last_min   = ImGui::GetItemRectMin ();
  list->last_max   = ImGui::GetItemRectMax ();

  ImGui::PopStyleVar   ();
  ImGui::EndGroup      ();
}

void
SK_LiveVertexStreamView (bool& can_scroll)
{
  static int filter_type = 0; // ALL

  ImGui::BeginGroup ();

  static float last_width = 256.0f;
  const  float font_size  = ImGui::GetFont ()->FontSize * ImGui::GetIO ().FontGlobalScale;

  struct vertex_stream_s
  {
    std::vector <std::string> contents;
    bool                      dirty      = true;
    uintptr_t                 last_sel   =    0;
    int                            sel   =   -1;
    float                     last_ht    = 256.0f;
    ImVec2                    last_min   = ImVec2 (0.0f, 0.0f);
    ImVec2                    last_max   = ImVec2 (0.0f, 0.0f);
  };

  struct {
    vertex_stream_s stream0;
  } static list_base;

  vertex_stream_s*
    list    = &list_base.stream0;

  SK::D3D9::VertexBufferTracker*
    tracker = tracked_vb.getPtr ();

  static auto& _last_frame = last_frame.get ();

  std::vector <IDirect3DVertexBuffer9 *> buffers;

  switch (filter_type)
  {
    case 2:
      for (auto& it : _last_frame.vertex_buffers.immutable) if (it != nullptr) buffers.emplace_back (it);
      break;
    case 1:
      for (auto& it : _last_frame.vertex_buffers.dynamic)   if (it != nullptr) buffers.emplace_back (it);
      break;
    case 0:
      for (auto& it : _last_frame.vertex_buffers.immutable) if (it != nullptr) buffers.emplace_back (it);
      for (auto& it : _last_frame.vertex_buffers.dynamic)   if (it != nullptr) buffers.emplace_back (it);
      break;
  };

  if (list->dirty)
  {
        list->sel = -1;
    int idx       =  0;
        list->contents.clear ();

    // The underlying list is unsorted for speed, but that's not at all
    //   intuitive to humans, so sort the thing when we have the RT view open.
    std::sort ( buffers.begin (),
                buffers.end   () );



    for ( auto it : buffers )
    {
      char szDesc [16] = { };

      sprintf (szDesc, "%08" PRIxPTR, (uintptr_t)it);

      list->contents.emplace_back (szDesc);

      if ((uintptr_t)it == list->last_sel)
      {
        list->sel = idx;
        //tracked_rt.tracking_tex = render_textures [sel];
      }

      ++idx;
    }
  }

  if (ImGui::IsMouseHoveringRect (list->last_min, list->last_max))
  {
         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) { list->sel--;  ImGui::GetIO ().WantCaptureKeyboard = true; }
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) { list->sel++;  ImGui::GetIO ().WantCaptureKeyboard = true; }
  }

  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleColor (ImGuiCol_Border, ImVec4 (0.9f, 0.7f, 0.5f, 1.0f));

  ImGui::BeginChild ( ImGui::GetID ("Stream 0"),
                      ImVec2 ( font_size * 7.0f, std::max (font_size * 15.0f, list->last_ht)),
                        true,
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened );

  if (ImGui::IsWindowHovered ())
  {
    can_scroll = false;

    ImGui::BeginTooltip ();
    ImGui::TextColored  (ImVec4 (0.9f, 0.6f, 0.2f, 1.0f), "You can cancel all render passes using the selected vertex buffer to debug a model");
    ImGui::Separator    ();
    ImGui::BulletText   ("Press [ while the mouse is hovering this list to select the previous shader");
    ImGui::BulletText   ("Press ] while the mouse is hovering this list to select the next shader");
    ImGui::EndTooltip   ();

         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) { list->sel--;  ImGui::GetIO ().WantCaptureKeyboard = true; }
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) { list->sel++;  ImGui::GetIO ().WantCaptureKeyboard = true; }
  }

  if (! buffers.empty ())
  {
    struct {
      int  last_sel    = 0;
      bool sel_changed = false;
    } static stream [3];

    int&  last_sel    = stream [0].last_sel;
    bool& sel_changed = stream [0].sel_changed;

    if (list->sel != last_sel)
      sel_changed = true;

    last_sel = list->sel;

    for ( size_t line = 0; line < buffers.size (); line++ )
    {
      if (line == static_cast <size_t> (list->sel))
      {
        bool selected    = true;

        ImGui::Selectable (list->contents [line].c_str (), &selected);

        if (sel_changed)
        {
          ImGui::SetScrollHereY (0.5f);

          sel_changed            = false;
          list->last_sel         = (uintptr_t)buffers [list->sel];
          tracker->vertex_buffer =            buffers [list->sel];
        }
      }

      else
      {
        bool selected    = false;

        if (ImGui::Selectable (list->contents [line].c_str (), &selected))
        {
          sel_changed            = true;
          list->sel              = (int)line;
          list->last_sel         = (uintptr_t)buffers [list->sel];
          tracker->vertex_buffer =            buffers [list->sel];
        }
      }
    }
  }

  else {
    tracker->vertex_buffer = nullptr;
  }

  ImGui::EndChild      ();
  ImGui::PopStyleColor ();

  ImGui::SameLine      ();
  ImGui::BeginGroup    ();

  if (ImGui::IsItemHovered (ImGuiHoveredFlags_RectOnly))
  {
         if (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) { list->sel--;  ImGui::GetIO ().WantCaptureKeyboard = true; }
    else if (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) { list->sel++;  ImGui::GetIO ().WantCaptureKeyboard = true; }

    if ( ImGui::IsKeyPressed (ImGuiKey_W, false) &&
         ImGui::GetIO ().KeysDown [VK_CONTROL]   &&
         ImGui::GetIO ().KeysDown [VK_SHIFT  ] )
    {
      ImGui::GetIO ().WantCaptureKeyboard = true;

      if (tracker->vertex_buffer != nullptr)
      {
        bool wireframe =
          tracker->wireframes.count   (tracker->vertex_buffer);

        if (wireframe)
          tracker->wireframes.erase   (tracker->vertex_buffer);
        else
          tracker->wireframes.emplace (tracker->vertex_buffer);
      }
    }
  }

  ImGui::Combo ("Vertex Buffer Filter", &filter_type, "  All   Geometry (Huge List)\0"
                                                      "Dynamic Geometry (Models)\0"
                                                      " Static Geometry (World)\0\0", 3 );

  ImGui::Checkbox ("Cancel Draws Using Selected Vertex Buffer",  &tracker->cancel_draws);  ImGui::SameLine ();

  LONG num_draws = ReadAcquire (&tracker->num_draws);
  LONG instanced = ReadAcquire (&tracker->instanced);

  if (tracker->cancel_draws)
    ImGui::TextDisabled ("%lu Skipped Draw%c Last Frame [%lu Instanced]", num_draws, num_draws != 1 ? 's' : ' ', instanced);
  else
    ImGui::TextDisabled ("%lu Draw%c Last Frame [%lu Instanced]        ", num_draws, num_draws != 1 ? 's' : ' ', instanced);

  ImGui::Checkbox ("Highlight Selected Vertex Buffer (Wireframe)", &tracker->wireframe);

  ImGui::Separator ();


  if ( tracker->vertex_buffer != nullptr &&
         ( _last_frame.vertex_buffers.dynamic.count   (tracker->vertex_buffer) ||
           _last_frame.vertex_buffers.immutable.count (tracker->vertex_buffer) ) )
  {
    bool wireframe =
      tracker->wireframes.count (tracker->vertex_buffer);

    D3DVERTEXBUFFER_DESC                             desc = { };
    if (SUCCEEDED (tracker->vertex_buffer->GetDesc (&desc)))
    {
      ImGui::BeginGroup   ();
      ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding, 20.0f);

      ImVec4 border_color = wireframe ? ImVec4 (1.0f, 0.5f, 0.5f, 1.0f) :
                              tracker->wireframe ?
                                ImVec4 (0.5f, 0.5f, 1.0f, 1.0f) :
                                ImVec4 (0.6f, 0.6f, 0.6f, 1.0f);

      ImGui::PushStyleColor (ImGuiCol_Border, border_color);

      static int last_count = 1;

      ImGui::BeginChild     ( ImGui::GetID ("Buffer Overview"),
                              ImVec2 ( font_size * 44.0f,
                                       font_size * 8.0f +
                                       font_size * (float)last_count ),
                                true,
                                  ImGuiWindowFlags_AlwaysAutoResize );

      ImGui::BeginGroup  ();
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), "Format:  ");
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), "Type:    ");
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), "Usage:   ");
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), "Size:    ");
      ImGui::EndGroup    ();

      ImGui::SameLine    ();

      ImGui::BeginGroup  ();
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.4f, 1.0f), "%hs",  SK_D3D9_FormatToStr (desc.Format).c_str ());
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.4f, 1.0f), "%s",  desc.Type == D3DRTYPE_VERTEXBUFFER ? "Vertex Buffer" :
                                                                  desc.Type == D3DRTYPE_INDEXBUFFER  ? "Index Buffer"  :
                                                                                                       "Unknown?!" );
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.4f, 1.0f), "%hs",  SK_D3D9_UsageToStr (desc.Usage).c_str ());
      ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.4f, 1.0f), "%lu", desc.Size);
      ImGui::EndGroup    ();

      last_count = 0;

      for (auto vtx_decl : tracker->vertex_decls)
      {
        ++last_count;

        static D3DVERTEXELEMENT9 elem_decl [MAXD3DDECLLENGTH];
        static UINT              num_elems;

        auto SK_D3D9_DeclTypeToStr = [](D3DDECLTYPE type) ->
          const char*
          {
            switch (type)
            {
              case D3DDECLTYPE_FLOAT1:     return "float";
              case D3DDECLTYPE_FLOAT2:     return "float2";
              case D3DDECLTYPE_FLOAT3:     return "float3";
              case D3DDECLTYPE_FLOAT4:     return "float4";
              case D3DDECLTYPE_D3DCOLOR:   return "D3DCOLOR";
              case D3DDECLTYPE_UBYTE4:     return "ubyte4";
              case D3DDECLTYPE_SHORT2:     return "short2";
              case D3DDECLTYPE_SHORT4:     return "short4";
              case D3DDECLTYPE_UBYTE4N:    return "ubyte4 (UNORM)";
              case D3DDECLTYPE_SHORT2N:    return "short2 (SNORM)";
              case D3DDECLTYPE_SHORT4N:    return "short4 (SNORM)";
              case D3DDECLTYPE_USHORT2N:   return "short2 (UNORM)";
              case D3DDECLTYPE_USHORT4N:   return "short4 (UNORM)";
              case D3DDECLTYPE_UDEC3:      return "udec3";
              case D3DDECLTYPE_DEC3N:      return "dec3 (NORM)";
              case D3DDECLTYPE_FLOAT16_2:  return "half2";
              case D3DDECLTYPE_FLOAT16_4:  return "half4";
              case D3DDECLTYPE_UNUSED:     return "UNUSED";
            }

            return "UNKNOWN";
          };

        auto SK_D3D9_DeclUsageToStr = [](D3DDECLUSAGE usage, int idx, int part) ->
          const char*
          {
            static char szOut [64] = { '\0' };

            switch (part)
            {
              case 0:
                switch (usage)
                {
                  case D3DDECLUSAGE_POSITION:     snprintf (szOut, 64, "POSITION      "); break;
                  case D3DDECLUSAGE_BLENDWEIGHT:  snprintf (szOut, 64, "BLENDWEIGHT   "); break;
                  case D3DDECLUSAGE_BLENDINDICES: snprintf (szOut, 64, "BLENDINDICES  "); break;
                  case D3DDECLUSAGE_NORMAL:       snprintf (szOut, 64, "NORMAL        "); break;
                  case D3DDECLUSAGE_PSIZE:        snprintf (szOut, 64, "PSIZE         "); break;
                  case D3DDECLUSAGE_TEXCOORD:     snprintf (szOut, 64, "TEXCOORD      "); break;
                  case D3DDECLUSAGE_TANGENT:      snprintf (szOut, 64, "TANGENT       "); break;
                  case D3DDECLUSAGE_BINORMAL:     snprintf (szOut, 64, "BINORMAL      "); break;
                  case D3DDECLUSAGE_TESSFACTOR:   snprintf (szOut, 64, "TESSFACTOR    "); break;
                  case D3DDECLUSAGE_POSITIONT:    snprintf (szOut, 64, "POSITIONT     "); break;
                  case D3DDECLUSAGE_COLOR:        snprintf (szOut, 64, "COLOR         "); break;
                  case D3DDECLUSAGE_FOG:          snprintf (szOut, 64, "FOG           "); break;
                  case D3DDECLUSAGE_DEPTH:        snprintf (szOut, 64, "DEPTH         "); break;
                  case D3DDECLUSAGE_SAMPLE:       snprintf (szOut, 64, "SV_SampleIndex"); break;
                };
                break;

              case 1:
                switch (usage)
                {
                  case D3DDECLUSAGE_POSITION:     snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_BLENDWEIGHT:  snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_BLENDINDICES: snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_NORMAL:       snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_PSIZE:        snprintf (szOut, 64, " "         ); break;
                  case D3DDECLUSAGE_TEXCOORD:     snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_TANGENT:      snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_BINORMAL:     snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_TESSFACTOR:   snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_POSITIONT:    snprintf (szOut, 64, " "         ); break;
                  case D3DDECLUSAGE_COLOR:        snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_FOG:          snprintf (szOut, 64, " "         ); break;
                  case D3DDECLUSAGE_DEPTH:        snprintf (szOut, 64, " [%i]", idx); break;
                  case D3DDECLUSAGE_SAMPLE:       snprintf (szOut, 64, " "         ); break;
                };
                break;
            }

            return szOut;
          };

        if (SUCCEEDED (vtx_decl->GetDeclaration (elem_decl, &num_elems)))
        {
          ImGui::Separator  ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ++last_count;

              ImGui::TextColored (ImVec4 (0.9f, 0.9f, 0.9f, 1.0f),    "Stream %3lu ", elem_decl [i].Stream);
            }
          }
          ImGui::EndGroup   ();
          ImGui::SameLine   ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ImGui::TextColored (ImVec4 (0.66f, 0.66f, 0.66f, 1.0f), "(+%02lu): ",
                                  elem_decl [i].Offset);
            }
          }
          ImGui::EndGroup   ();
          ImGui::SameLine   ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ImGui::TextColored (ImVec4 (0.35f, 0.85f, 0.35f, 1.0f), "%16s ",
                                  SK_D3D9_DeclTypeToStr ((D3DDECLTYPE)elem_decl [i].Type));
            }
          }
          ImGui::EndGroup   ();
          ImGui::SameLine   ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ImGui::TextColored (ImVec4 (0.6f, 0.6f, 1.0f, 1.0f), R"("Attrib%02lu")", i);
            }
          }
          ImGui::EndGroup   ();
          ImGui::SameLine   ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), " : %s",
                    SK_D3D9_DeclUsageToStr ((D3DDECLUSAGE)elem_decl [i].Usage, elem_decl [i].UsageIndex, 0));
            }
          }
          ImGui::EndGroup   ();
          ImGui::SameLine   ();
          ImGui::BeginGroup ();
          for (UINT i = 0; i < num_elems; i++)
          {
            if (elem_decl [i].Type != D3DDECLTYPE_UNUSED)
            {
              ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), " : %s",
                    SK_D3D9_DeclUsageToStr ((D3DDECLUSAGE)elem_decl [i].Usage, elem_decl [i].UsageIndex, 1));
            }
          }
          ImGui::EndGroup   ();

          --last_count;
        }
      }
      ImGui::EndChild ();

      ImGui::PopStyleColor ();
      ImGui::PopStyleVar   ();
      ImGui::EndGroup      ();

#if 0
      if (ImGui::IsItemHoveredRect () && tracker->textures.size ())
      {
        ImGui::BeginTooltip ();

        D3DFORMAT fmt = D3DFMT_UNKNOWN;

        for ( auto it : tracker->textures )
        {
          ISKTextureD3D9* pTex = tex_mgr.getTexture (it)->d3d9_tex;

          if (pTex && pTex->pTex)
          {
            D3DSURFACE_DESC desc;
            if (SUCCEEDED (pTex->pTex->GetLevelDesc (0, &desc)))
            {
              fmt = desc.Format;
              ImGui::Image ( pTex->pTex, ImVec2  ( std::max (64.0f, (float)desc.Width / 16.0f),
        ((float)desc.Height / (float)desc.Width) * std::max (64.0f, (float)desc.Width / 16.0f) ),
                                         ImVec2  (0,0),             ImVec2  (1,1),
                                         ImColor (255,255,255,255), ImColor (242,242,13,255) );
            }

            ImGui::SameLine ();

            ImGui::BeginGroup ();
            ImGui::Text       ("Texture: %08lx", it);
            ImGui::Text       ("Format:  %hs",   SK_D3D9_FormatToStr (fmt).c_str ());
            ImGui::EndGroup   ();
          }
        }

        ImGui::EndTooltip ();
      }
#endif

      ImGui::SameLine ();
      ImGui::Checkbox ("Always Draw This Buffer In Wireframe", &wireframe);

      if (wireframe)
        tracker->wireframes.emplace (tracker->vertex_buffer);
      else if (tracker->wireframes.count (tracker->vertex_buffer))
        tracker->wireframes.erase   (tracker->vertex_buffer);
    }

    if ((! tracker->vertex_shaders.empty ()) || (! tracker->pixel_shaders.empty ()))
    {
      ImGui::Separator ( );
      ImGui::Columns   (2);

      for ( auto it : tracker->vertex_shaders )
        ImGui::Text ("Vertex Shader: %08x", it);

      ImGui::NextColumn ();

      for ( auto it : tracker->pixel_shaders )
        ImGui::Text ("Pixel Shader: %08x", it);

      ImGui::Columns   (1);
    }
  }
  else
    tracker->cancel_draws = false;

  ImGui::EndGroup      ( );

  list->last_ht  = ImGui::GetItemRectSize ().y;

  list->last_min = ImGui::GetItemRectMin ( );
  list->last_max = ImGui::GetItemRectMax ( );

  ImGui::PopStyleVar   ( );
  ImGui::EndGroup      ( );
}

extern int32_t  tex_dbg_idx;
extern uint32_t debug_tex_id;
extern std::set <uint32_t> textures_used_last_dump;

bool
SK_D3D9_TextureModDlg (void)
{
  const float font_size = ImGui::GetFont ()->FontSize * ImGui::GetIO ().FontGlobalScale;
  bool        show_dlg  = true;

  ImGuiIO& io =
    ImGui::GetIO ();


  auto HandleKeyboard = [&](void)
  {
    extern std::set <uint32_t> textures_used_last_dump;

    if ( ImGui::IsKeyPressed (ImGuiKey_RightBracket, false) ||
         ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false) )
    {
      tex_dbg_idx += (ImGui::IsKeyPressed (ImGuiKey_RightBracket, false)) ? 1 : 0;
      tex_dbg_idx -= (ImGui::IsKeyPressed (ImGuiKey_LeftBracket,  false)) ? 1 : 0;

      if (tex_dbg_idx < 0 || (textures_used_last_dump.empty ()))
      {
        tex_dbg_idx  = -1;
        debug_tex_id =  0;
      }

      else
      {
        if (tex_dbg_idx >= sk::narrow_cast <int32_t> (textures_used_last_dump.size ()))
        {
          tex_dbg_idx =
            std::max (0UL, sk::narrow_cast <uint32_t> (textures_used_last_dump.size ()) - 1UL);
        }
      }

      if (tex_dbg_idx >= 0)
      {
        debug_tex_id = 0;
        int      idx = 0;

        for (auto it : textures_used_last_dump)
        {
          if (tex_dbg_idx == idx++)
          {
            debug_tex_id = it;
            break;
          }
        }
      }

      SK::D3D9::TextureManager& tex_mgr =
        SK_D3D9_GetTextureManager ();

      tex_mgr.updateOSD ();
    }
  };


  static auto& _last_frame = last_frame.get ();
  static auto& _known_objs = known_objs.get ();
  static auto& _tracked_vs = tracked_vs.get ();
  static auto& _tracked_ps = tracked_ps.get ();
  static auto& _tracked_vb = tracked_vb.get ();
  static auto& _tracked_rt = tracked_rt.get ();



  ImGui::SetNextWindowSize            (ImVec2 (io.DisplaySize.x * 0.66f, io.DisplaySize.y * 0.42f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints ( /*ImVec2 (768.0f, 384.0f),*/
                                       ImVec2 (io.DisplaySize.x * 0.16f, io.DisplaySize.y * 0.16f),
                                       ImVec2 (io.DisplaySize.x * 0.96f, io.DisplaySize.y * 0.96f));

  static std::string ver_str =
    SK_FormatString ("D3D9 Render Mod Toolkit (v %s)", SK_GetVersionStrA ());

  ImGui::Begin ( ver_str.c_str (), &show_dlg );

  bool can_scroll = ImGui::IsWindowFocused () && ImGui::IsMouseHoveringRect ( ImVec2 (ImGui::GetWindowPos ().x,                             ImGui::GetWindowPos ().y),
                                                                              ImVec2 (ImGui::GetWindowPos ().x + ImGui::GetWindowSize ().x, ImGui::GetWindowPos ().y + ImGui::GetWindowSize ().y) );

  ImGui::PushItemWidth (ImGui::GetWindowWidth () * 0.666f);

  if (ImGui::CollapsingHeader ("How Texture Mods Work"))
  {
    ImGui::BeginChild (ImGui::GetID ("ModDescription"), ImVec2 (font_size * 66.0f, font_size * 27.0f), true);
      ImGui::TextColored    (ImVec4 (0.9f, 0.7f, 0.5f, 1.0f), "Replacing a Texture");
      ImGui::Separator      ();

      ImGui::TreePush ("");
        ImGui::BulletText ("Dump the texture: select it under Live Texture View and use Dump Texture to Disk,");
        ImGui::BulletText ("   or turn on Dump ALL Textures at Load under Misc. Settings.");
        ImGui::BulletText ("Dumps are written to  <ResourceRoot>\\dump\\textures\\<game>\\<format>\\<checksum>.dds");
        ImGui::BulletText ("Edit the image, keep the file name, and save it as a .dds in one of these:");
        ImGui::TreePush   ("");
          ImGui::BulletText ("<ResourceRoot>\\inject\\textures\\streaming\\    Loaded in the background (recommended)");
          ImGui::BulletText ("<ResourceRoot>\\inject\\textures\\blocking\\     The game waits for it; no pop-in, but can stutter");
          ImGui::BulletText ("<ResourceRoot>\\inject\\textures\\               Treated the same as streaming");
        ImGui::TreePop    (  );
        ImGui::BulletText ("Press Refresh Data Sources; textures the game loads from then on are replaced.");
      ImGui::TreePop  ();

      ImGui::TextWrapped    ( "\nA replacement does not have to match the size, format or number of mipmaps of the original. "
                              "Many replacements can also be packed into .7z archives placed in  <ResourceRoot>\\inject\\ ;"
                              " put them in a folder named streaming or blocking inside of the archive to choose how they load.\n\n" );

      ImGui::TextColored    (ImVec4 (0.9f, 0.7f, 0.5f, 1.0f), "Checksums");
      ImGui::Separator      ();

      ImGui::TextWrapped    ( "\nThe file name is the checksum that identifies a texture. For most textures that is the checksum "
                              "of the top-level image. Textures the game loads from a file using D3DX are identified by the "
                              "checksum of that file instead, but a replacement named after the Pixel Checksum shown in Live "
                              "Texture View is accepted for them as well, and works no matter how the game loads the texture.\n\n" );

      ImGui::TextColored    (ImVec4 (0.9f, 0.7f, 0.5f, 1.0f), "Good to Know");
      ImGui::Separator      ();

      ImGui::TreePush ("");
        ImGui::BulletText ("Compressed textures can only be dumped if Enable On-Demand Texture Dumping was on when they were created.");
        ImGui::BulletText ("Cube and volume textures are only replaced when the game loads them from a file using D3DX.");
        ImGui::BulletText ("Select a texture and use the Original / Injected previews to compare the two in game.");
      ImGui::TreePop  ();
    ImGui::EndChild         ();
  }

  if (ImGui::CollapsingHeader("Injectable Data Sources", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
  {
    SK_D3D9_DrawFileList  (can_scroll);
  }

  if (ImGui::CollapsingHeader ("Live Texture View", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen))
  {
    SK::D3D9::TextureManager& tex_mgr =
      SK_D3D9_GetTextureManager ();

    HandleKeyboard ();

    static float last_ht    = 256.0f;
    static float last_width = 256.0f;

    static std::vector <std::string> list_contents;
    static bool                      list_dirty     = false;
    static int                       sel            =     0;

    ImGui::BeginChild ( ImGui::GetID ("D3D9_ToolHeadings"),
                          ImVec2 (font_size * 66.0f, font_size * 2.5f),
                            false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding |
                              ImGuiWindowFlags_NavFlattened           | ImGuiWindowFlags_AlwaysAutoResize );

    if (ImGui::Button ("  Refresh Textures  "))
    {
      SK_ICommandProcessor& command =
        *SK_Render_InitializeSharedCVars ();

      command.ProcessCommandLine ("Textures.Trace true");

      tex_mgr.updateOSD ();

      list_dirty = true;
    }

    if (ImGui::IsItemHovered ()) ImGui::SetTooltip ("Refreshes the set of texture checksums used in the last frame drawn.");

    ImGui::SameLine ();

    if (ImGui::Button (" Clear Debug "))
    {
      sel                         = -1;
      debug_tex_id                =  0;
      textures_used_last_dump.clear ();
      last_ht                     =  0;
      last_width                  =  0;
    }

    if (ImGui::IsItemHovered ()) ImGui::SetTooltip ("Exits texture debug mode.");

    ImGui::SameLine ();

    bool need_reset_graphics = false;

    if (ImGui::Checkbox ("Enable On-Demand Texture Dumping",    &config.textures.on_demand_dump)) need_reset_graphics = true;

    if (ImGui::IsItemHovered ())
    {
      ImGui::BeginTooltip ();
      ImGui::TextColored  (ImVec4 (0.9f, 0.7f, 0.3f, 1.f), "Enable dumping DXT compressed textures from VRAM.");
      ImGui::Separator    ();
      ImGui::Text         ("Drivers may not be able to manage texture memory as efficiently, and you should turn this option off when not modifying textures.\n\n");
      ImGui::BulletText   ("If this is your first time enabling this feature, the dump button will not work until you reload all textures in-game.");
      ImGui::EndTooltip   ();
    }

    ImGui::SameLine ();

    ImGui::Checkbox ("Highlight Selected Texture in Game",    &config.textures.highlight_debug_tex);

    ImGui::Separator ();
    ImGui::EndChild  ();

    if (list_dirty)
    {
      list_contents.clear ();
           sel = tex_dbg_idx;

      if (debug_tex_id == 0)
        last_ht = 0;

      for ( auto it : textures_used_last_dump )
      {
        char szDesc [16] = { };

        sprintf (szDesc, "%08x", it);

        list_contents.emplace_back (szDesc);
      }
    }

    ImGui::BeginGroup ();

    ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor (ImGuiCol_Border, ImVec4 (0.9f, 0.7f, 0.5f, 1.0f));

    ImGui::BeginChild ( ImGui::GetID ("Item List"),
                        ImVec2 ( font_size * 6.0f, std::max (font_size * 15.0f, last_ht)),
                          true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened );

    if (ImGui::IsWindowHovered ())
      can_scroll = false;

    if (! textures_used_last_dump.empty ())
    {
      static      int last_sel = 0;
      static bool sel_changed  = false;
    
      if (sel != last_sel)
        sel_changed = true;
    
      last_sel = sel;
    
      for ( int line = 0; line < sk::narrow_cast <int> (textures_used_last_dump.size ()); line++)
      {
        if (line == sel)
        {
          bool selected = true;
          ImGui::Selectable (list_contents [line].c_str (), &selected);
    
          if (sel_changed)
          {
            ImGui::SetScrollHereY (0.5f); // 0.0f:top, 0.5f:center, 1.0f:bottom
            sel_changed = false;
          }
        }
    
        else
        {
          bool selected = false;
    
          if (ImGui::Selectable (list_contents [line].c_str (), &selected))
          {
            sel_changed  = true;
            tex_dbg_idx  =  line;
            sel          =  line;
    
            int idx = 0;
            for (auto it : textures_used_last_dump)
            {
              if (line == idx++)
                debug_tex_id = it;
            }
            //debug_tex_id =  textures_used_last_dump [line];
          }
        }
      }
    }
    
    ImGui::EndChild      ();
    ImGui::PopStyleColor ();
    
    if (ImGui::IsItemHovered ())
    {
      ImGui::BeginTooltip ();
      ImGui::TextColored  (ImVec4 (0.9f, 0.6f, 0.2f, 1.0f), R"(If highlighting is enabled, the "debug" texture will blink to make identifying textures easier.)");
      ImGui::Separator    ();
      ImGui::BulletText   ("Press %hs to select the previous texture from this list", SK_WideCharToUTF8 (virtKeyCodeToHumanKeyName [VK_OEM_4]).c_str ());
      ImGui::BulletText   ("Press %hs to select the next texture from this list",     SK_WideCharToUTF8 (virtKeyCodeToHumanKeyName [VK_OEM_6]).c_str ());
      ImGui::EndTooltip   ();
    }

    ImGui::SameLine     ();
    ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding, 20.0f);

    last_ht    = std::max (last_ht,    16.0f);
    last_width = std::max (last_width, 16.0f);
    
    if (debug_tex_id != 0x00)
    {
      SK::D3D9::Texture* pTex =
        tex_mgr.getTexture (debug_tex_id);
    
      extern bool __remap_textures;
             bool has_alternate = ( pTex                         != nullptr &&
                                    pTex->d3d9_tex               != nullptr &&
                                    pTex->d3d9_tex->pTexOverride != nullptr &&
               SK_ValidatePointer ( pTex->d3d9_tex->pTex, true ) );
     
      if ( pTex                 != nullptr &&
           pTex->d3d9_tex       != nullptr &&
           pTex->d3d9_tex->pTex != nullptr && SK_ValidatePointer (
           pTex->d3d9_tex->pTex, true                            )
         )
      {
        D3DSURFACE_DESC desc;
    
        if (SUCCEEDED (pTex->d3d9_tex->pTex->GetLevelDesc (0, &desc)))
        {
          ImVec4 border_color = config.textures.highlight_debug_tex ?
                                    ImVec4 (0.3f, 0.3f, 0.3f, 1.0f) :
                                    (pTex->d3d9_tex->getDrawTexture () != pTex->d3d9_tex->pTex)
                                                                    ? ImVec4 (0.5f,  0.5f,  0.5f, 1.0f) :
                                                                      ImVec4 (0.3f,  1.0f,  0.3f, 1.0f);

          ImGui::PushStyleColor (ImGuiCol_Border, border_color);
          
          ImGui::BeginGroup     ();
          ImGui::BeginChild     ( ImGui::GetID ("Item Selection"),
                                  ImVec2 ( std::max (font_size * 19.0f, (float)desc.Width + 24.0f),
                                (float)desc.Height + font_size * 11.0f),
                                    true,
                                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NavFlattened );

          if ((! config.textures.highlight_debug_tex) && has_alternate)
          {
            if (ImGui::IsItemHovered ())
            {
              if (pTex->d3d9_tex->img_to_use != ISKTextureD3D9::ContentPreference::Original)
                 ImGui::SetTooltip ("Click me to make this the always visible version");
              else
              {
                ImGui::BeginTooltip ( );
                ImGui::Text         ("Click me to use the system-wide texture preference");
                ImGui::Separator    ( );
                ImGui::BulletText   ( "The system-wide preference is currently:  (%s)",
                                        __remap_textures ? "Draw injected textures" :
                                                           "Draw original textures" );
                ImGui::EndTooltip   ( );
              }
            }

            // Allow the user to toggle texture override by clicking the frame
            if (SK_ImGui_IsItemClicked ())
            {
              pTex->d3d9_tex->toggleOriginal ();
            }
          }

          last_width  = (float)desc.Width;
          last_ht     = (float)desc.Height + font_size * 11.0f;


          int num_lods =
            pTex->d3d9_tex->pTex->GetLevelCount ();

          ImGui::BeginGroup      (                  );
          ImGui::TextUnformatted ( "Dimensions:   " );
          ImGui::TextUnformatted ( "Format:       " );
          ImGui::TextUnformatted ( "Data Size:    " );
          ImGui::TextUnformatted ( "Load Time:    " );
          ImGui::TextUnformatted ( "References:   " );
          ImGui::EndGroup        (                  );
          
          ImGui::SameLine   ();
          
          ImGui::BeginGroup ();
          ImGui::Text ("%lux%lu (%lu %s)",
                       desc.Width, desc.Height,
                       num_lods, num_lods > 1 ? "LODs" : "LOD");
          ImGui::Text ("%hs",
                       SK_D3D9_FormatToStr (desc.Format).c_str ( ));
          ImGui::Text ("%.3f MiB",
            (double)pTex->d3d9_tex->tex_size / ( 1024.0f * 1024.0f ));
          ImGui::Text ("%.3f ms",
                       pTex->load_time);
          ImGui::Text ("%lu",
                       pTex->refs.load ());


          //extern std::unordered_map <uint32_t, int32_t>            injected_refs;
          //ImGui::Text ("%lu", injected_refs [pTex->crc32c]);
          ImGui::EndGroup   ();

          // Textures loaded by D3DX9 are identified by the checksum of the file
          //   they came from, but can also be replaced using this checksum.
          if ( pTex->d3d9_tex->pixel_crc32c != 0x00 &&
               pTex->d3d9_tex->pixel_crc32c != pTex->d3d9_tex->tex_crc32c )
          {
            ImGui::Text ("Pixel Checksum:  %08x", pTex->d3d9_tex->pixel_crc32c);

            if (ImGui::IsItemHovered ())
            {
              ImGui::SetTooltip (
                "A replacement named after this checksum works no matter how the game loads the texture."
              );
            }
          }

          ImGui::Separator  ();
          
          static bool flip_vertical0   = false;
          static bool flip_horizontal0 = false;
          
          ImGui::Checkbox ("Flip Vertically##D3D9_FlipVertical0",     &flip_vertical0);   ImGui::SameLine ( );
          ImGui::Checkbox ("Flip Horizontally##D3D9_FlipHorizontal0", &flip_horizontal0);
          
          if (! tex_mgr.isTextureDumped (debug_tex_id))
          {
            if ( ImGui::Button ("  Dump Texture to Disk  ###DumpTexture") )
            {
              tex_mgr.dumpTexture (desc.Format, debug_tex_id, pTex->d3d9_tex->pTex);
            }
          
            //if (config.textures.quick_load && ImGui::IsItemHovered ())
            //  ImGui::SetTooltip ("Turn off Texture QuickLoad to use this feature.");
          }
          
          else
          {
            if ( ImGui::Button ("  Delete Dumped Texture from Disk  ###DumpTexture") )
            {
              tex_mgr.deleteDumpedTexture (desc.Format, debug_tex_id);
            }
          }

          ImVec2 uv0 (flip_horizontal0 ? 1.0f : 0.0f, flip_vertical0 ? 1.0f : 0.0f);
          ImVec2 uv1 (flip_horizontal0 ? 0.0f : 1.0f, flip_vertical0 ? 0.0f : 1.0f);
          
          ImGui::PushStyleColor  (ImGuiCol_Border, ImVec4 (0.95f, 0.95f, 0.05f, 1.0f));
          ImGui::BeginChildFrame (ImGui::GetID ("ChildFrame_XXX"), ImVec2 ((float)desc.Width + 8, (float)desc.Height + 8),
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar );
          ImGui::Image           ( pTex->d3d9_tex->pTex,
                                     ImVec2 ((float)desc.Width, (float)desc.Height),
                                       uv0,                       uv1,
                                       ImColor (255,255,255,255), ImColor (255,255,255,128)
                                 );
          ImGui::EndChildFrame   ();
          ImGui::PopStyleColor   ();
          ImGui::EndChild        ();
          ImGui::EndGroup        ();
          ImGui::PopStyleColor   ();
        }
      }

      if (has_alternate)
      {
        ImGui::SameLine ();

        D3DSURFACE_DESC desc;
        
        if (            pTex->d3d9_tex != nullptr               &&
                        pTex->d3d9_tex->pTexOverride != nullptr &&
             SUCCEEDED (pTex->d3d9_tex->pTexOverride->GetLevelDesc (0, &desc)) )
        {
          bool override_tex = (pTex->d3d9_tex->getDrawTexture () == pTex->d3d9_tex->pTexOverride);

          ImVec4 border_color = config.textures.highlight_debug_tex ?
                                  ImVec4 (0.3f, 0.3f, 0.3f, 1.0f) :
                   override_tex ? ImVec4 (0.3f, 1.0f, 0.3f, 1.0f) :
                                  ImVec4 (0.5f, 0.5f, 0.5f, 1.0f);

          ImGui::PushStyleColor  (ImGuiCol_Border, border_color);
        
          ImGui::BeginGroup ();
          ImGui::BeginChild ( ImGui::GetID ("Item Selection2"),
                              ImVec2 ( std::max (font_size * 19.0f, (float)desc.Width  + 24.0f),
                                                                    (float)desc.Height + font_size * 10.0f),
                                true,
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NavFlattened );
          ImGui::PopStyleColor ();

          if (! config.textures.highlight_debug_tex)
          {
            if (ImGui::IsItemHovered ())
            {
              if (pTex->d3d9_tex->img_to_use != ISKTextureD3D9::ContentPreference::Override)
                ImGui::SetTooltip ("Click me to make this the always visible version");
              else
              {
                ImGui::BeginTooltip ( );
                ImGui::Text         ("Click me to use the system-wide texture preference");
                ImGui::Separator    ( );
                ImGui::BulletText   ( "The system-wide preference is currently:  (%s)",
                                        __remap_textures ? "Draw injected textures" :
                                                           "Draw original textures" );
                ImGui::EndTooltip   ( );
              }
            }

            // Allow the user to toggle texture override by clicking the frame
            if (SK_ImGui_IsItemClicked ())
            {
              pTex->d3d9_tex->toggleOverride ();
            }
          }

          last_width  = std::max (last_width, (float)desc.Width);
          last_ht     = std::max (last_ht,    (float)desc.Height + font_size * 10.0f);

          bool injected  =
            (tex_mgr.getInjectableTexture (debug_tex_id).size != 0),
               reloading = false;

          int num_lods =
            pTex->d3d9_tex->pTexOverride->GetLevelCount ();

          ImGui::BeginGroup      ();
          ImGui::TextUnformatted ( "Dimensions:   " );
          ImGui::TextUnformatted ( "Format:       " );
          ImGui::TextUnformatted ( "Data Size:    " );
          ImGui::EndGroup        ();

          ImGui::SameLine   ();

          ImGui::BeginGroup ();
          ImGui::Text       ( "%lux%lu  (%lu %s)",
                                desc.Width, desc.Height,
                                   num_lods, num_lods > 1 ? "LODs" : "LOD" );
          ImGui::Text       ( "%hs",
                                SK_D3D9_FormatToStr (desc.Format).c_str () );
          ImGui::Text       ( "%.3f MiB",
                                (double)pTex->d3d9_tex->override_size / (1024.0f * 1024.0f) );
          ImGui::EndGroup   ();

          ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), injected ? "Injected Texture" :
                                                                          "Resampled Texture" );

          ImGui::Separator  ();

          static bool flip_vertical1   = false;
          static bool flip_horizontal1 = false;

          ImGui::Checkbox ("Flip Vertically##D3D9_FlipVertical1",     &flip_vertical1);   ImGui::SameLine ( );
          ImGui::Checkbox ("Flip Horizontally##D3D9_FlipHorizontal1", &flip_horizontal1);

          if (injected)
          {
            if ( ImGui::Button ("  Reload This Texture  ") && tex_mgr.reloadTexture (debug_tex_id) )
            {
              reloading = true;

              tex_mgr.updateOSD ();
            }
          }

          else {
            ImGui::Button ("  Resample This Texture  "); // NO-OP, but preserves alignment :P
          }

          if (! reloading)
          {
            ImVec2 uv0 (flip_horizontal1 ? 1.0f : 0.0f, flip_vertical1 ? 1.0f : 0.0f);
            ImVec2 uv1 (flip_horizontal1 ? 0.0f : 1.0f, flip_vertical1 ? 0.0f : 1.0f);

            ImGui::PushStyleColor  (ImGuiCol_Border, ImVec4 (0.95f, 0.95f, 0.05f, 1.0f));
            ImGui::BeginChildFrame (ImGui::GetID ("ChildFrame_YYY"), ImVec2 ((float)desc.Width + 8, (float)desc.Height + 8),
                                    ImGuiWindowFlags_NoInputs    |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Image           ( pTex->d3d9_tex->pTexOverride,
                                       ImVec2 ((float)desc.Width, (float)desc.Height),
                                       uv0,                       uv1,
                                       ImColor (255,255,255,255), ImColor (255,255,255,128)
                                   );
            ImGui::EndChildFrame   ( );
            ImGui::PopStyleColor   ( );
          }

          ImGui::EndChild          ( );
          ImGui::EndGroup          ( );
        }
      }
    }
    ImGui::EndGroup                ( );
    ImGui::PopStyleVar             (2);
  }

  if (ImGui::CollapsingHeader ("Live Render Target View"))
  {
    SK::D3D9::TextureManager& tex_mgr =
      SK_D3D9_GetTextureManager ();

    static float last_ht    = 256.0f;
    static float last_width = 256.0f;

    static std::vector <std::string> list_contents;
    static bool                      list_dirty     = true;
    static uintptr_t                 last_sel_ptr   =    0;
    static int                            sel       =   -1;

    std::vector <IDirect3DBaseTexture9*> render_textures;
           tex_mgr.getUsedRenderTargets (render_textures);

    _tracked_rt.tracking_tex = nullptr;

    if (list_dirty)
    {
          sel = -1;
      int idx =  0;
          list_contents.clear ();

      // The underlying list is unsorted for speed, but that's not at all
      //   intuitive to humans, so sort the thing when we have the RT view open.
      std::sort ( render_textures.begin (),
                  render_textures.end   (),
        []( IDirect3DBaseTexture9 *a,
            IDirect3DBaseTexture9 *b )
        {
          return (uintptr_t)a < (uintptr_t)b;
        }
      );


      for ( auto& it : render_textures )
      {
        char szDesc [16] = { };

        sprintf (szDesc, "%" PRIxPTR, (uintptr_t)it);

        list_contents.emplace_back (szDesc);

        if ((uintptr_t)it == last_sel_ptr)
        {
          sel = idx;
          _tracked_rt.tracking_tex = render_textures [sel];
        }

        ++idx;
      }
    }

    ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor (ImGuiCol_Border, ImVec4 (0.9f, 0.7f, 0.5f, 1.0f));

    ImGui::BeginChild ( ImGui::GetID ("Item List2"),
                        ImVec2 ( font_size * 6.0f, std::max (font_size * 15.0f, last_ht)),
                          true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened );

    if (ImGui::IsWindowHovered ())
      can_scroll = false;

    if (! render_textures.empty ())
    {
      static      int last_sel = 0;
      static bool sel_changed  = false;
      
      if (sel != last_sel)
        sel_changed = true;

      last_sel = sel;

      for ( int line = 0; line < sk::narrow_cast <int> (render_textures.size ()); line++ )
      {
        SK_ComPtr <IDirect3DTexture9>                                         pTex;
        if (SUCCEEDED (render_textures [line]->QueryInterface (IID_PPV_ARGS (&pTex.p))))
        {
          D3DSURFACE_DESC                        desc = { };
          if (SUCCEEDED (pTex->GetLevelDesc (0, &desc)))
          {
            if (line == sel)
            {
              bool selected = true;
              ImGui::Selectable (list_contents [line].c_str (), &selected);
      
              if (sel_changed)
              {
                ImGui::SetScrollHereY (0.5f); // 0.0f:top, 0.5f:center, 1.0f:bottom
                ImGui::SetKeyboardFocusHere ( );
      
                sel_changed = false;
              }
            }
      
            else
            {
              bool selected = false;
      
              if (ImGui::Selectable (list_contents [line].c_str (), &selected))
              {
                 sel_changed  = true;
                 sel          =  line;
                 last_sel_ptr = (uintptr_t)render_textures [sel];
                _tracked_rt.tracking_tex = render_textures [sel];
              }
            }
          }
        }
      }
    }
    ImGui::EndChild      ();

    ImGui::PopStyleColor ();
    ImGui::PopStyleVar   ();

    ImGui::BeginGroup    ();

    SK_ComPtr <IDirect3DTexture9> pTex = nullptr;
    
    if ((! render_textures.empty ()) && sel >= 0)
           render_textures [sel]->QueryInterface (IID_PPV_ARGS (&pTex.p));
    
    if (pTex != nullptr)
    {
      D3DSURFACE_DESC                        desc = { };
      if (SUCCEEDED (pTex->GetLevelDesc (0, &desc)))
      {
        size_t shaders =
          std::max ( _tracked_rt.pixel_shaders.size  (),
                     _tracked_rt.vertex_shaders.size () );

        // Some Render Targets are MASSIVE, let's try to keep the damn things on the screen ;)
        const float effective_width  = std::min (0.75f * ImGui::GetIO ().DisplaySize.x, static_cast <float> (desc.Width)  / 2.0f);
        const float effective_height = std::min (0.75f * ImGui::GetIO ().DisplaySize.y, static_cast <float> (desc.Height) / 2.0f);

        ImGui::SameLine   ();

        ImGui::PushStyleColor
                          (ImGuiCol_Border, ImVec4 (0.5f, 0.5f, 0.5f, 1.0f) );
        ImGui::BeginChild (ImGui::GetID ("Item Selection3"),
                           ImVec2 ( std::max (font_size * 30.0f, effective_width  + 24.0f),
                                    std::max (256.0f,            effective_height + font_size * 4.0f + (float)shaders * font_size) ),
                             true,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened);

        last_width  = effective_width;
        last_ht     = effective_height + font_size * 4.0f + (float)shaders * font_size;

        //extern std::wstring
        //SK_D3D9_FormatToStr (D3DFORMAT Format, bool include_ordinal = true);


        ImGui::Text ( "Dimensions:   %lux%lu",
                        desc.Width, desc.Height/*,
                          pTex->d3d9_tex->GetLevelCount ()*/ );
        ImGui::Text ( "Format:       %hs",
                        SK_D3D9_FormatToStr (desc.Format).c_str () );

        ImGui::Separator     ( );

        ImGui::PushStyleColor  (ImGuiCol_Border, ImVec4 (0.95f, 0.95f, 0.05f, 1.0f));
        ImGui::BeginChildFrame (ImGui::GetID ("ChildFrame_ZZZ"), ImVec2 (effective_width + 8.0f, effective_height + 8.0f),
                                  ImGuiWindowFlags_NoInputs    | ImGuiWindowFlags_NoNavInputs |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize );
        ImGui::Image           ( pTex,
                                   ImVec2 (effective_width, effective_height),
                                     ImVec2  (0,0),             ImVec2  (1,1),
                                     ImColor (255,255,255,255), ImColor (255,255,255,128)
                               );
        ImGui::EndChildFrame ( );
        ImGui::PopStyleColor ( );

        if (shaders > 0)
        {
          ImGui::Columns     (2);

          for ( auto it : _tracked_rt.vertex_shaders )
            ImGui::Text ("Vertex Shader: %08x", it);

          ImGui::NextColumn  ( );

          for ( auto it : _tracked_rt.pixel_shaders )
            ImGui::Text ("Pixel Shader: %08x", it);

          ImGui::Columns     (1);
        }

        ImGui::EndChild      ( );
        ImGui::PopStyleColor ( );
      }
    }
    ImGui::EndGroup          ( );
  }

  if (ImGui::CollapsingHeader ("Live Shader View"))
  {
    ImGui::TreePush ("");

    if (ImGui::CollapsingHeader ("Pixel Shaders"))
    {
      std::scoped_lock <SK_Thread_HybridSpinlock>
            scope_lock (*lock_ps);

      SK_D3D9_LiveShaderClassView (SK::D3D9::ShaderClass::Pixel, can_scroll);
    }

    if (ImGui::CollapsingHeader ("Vertex Shaders"))
    {
      std::scoped_lock <SK_Thread_HybridSpinlock>
            scope_lock (*lock_vs);

      SK_D3D9_LiveShaderClassView (SK::D3D9::ShaderClass::Vertex, can_scroll);
    }

    ImGui::TreePop    ( );
  }

  if (ImGui::CollapsingHeader ("Live Vertex Buffer View"))
  {
    ImGui::TreePush  ("");

    if (ImGui::CollapsingHeader ("Stream 0"))
    {
      std::scoped_lock <SK_Thread_HybridSpinlock>
            scope_lock (*lock_vb);

      SK_LiveVertexStreamView (can_scroll);
    }

    ImGui::TreePop    ( );
  }

  if (ImGui::CollapsingHeader ("Misc. Settings"))
  {
    ImGui::TreePush  ("");
    //if (ImGui::Checkbox ("Dump ALL Shaders   (TBFix_Res\\dump\\shaders\\<ps|vs>_<checksum>.html)", &config.render.dump_shaders)) need_reset.graphics = true;
    ImGui::Checkbox (R"(Dump ALL Textures at Load  (<ResourceRoot>\dump\textures\<format>\*.dds))", &config.textures.dump_on_load);//) need_reset.graphics = true;

    if (ImGui::IsItemHovered ())
    {
      ImGui::BeginTooltip ();
      ImGui::Text         ("Enabling this will cause the game to run slower and waste disk space, only enable if you know what you are doing.");
      ImGui::EndTooltip   ();
    }

    ImGui::TreePop    ( );
  }

  ImGui::PopItemWidth ( );

  if (can_scroll)
    ImGui::SetScrollY (ImGui::GetScrollY () + 5.0f * ImGui::GetFont ()->FontSize * -ImGui::GetIO ().MouseWheel);

  ImGui::End          ( );

  std::scoped_lock <SK_Thread_HybridSpinlock, SK_Thread_HybridSpinlock,
                                              SK_Thread_HybridSpinlock>
        scope_lock (*lock_vs, *lock_ps, *lock_vb);

  _tracked_ps.clear (); _tracked_vs.clear (); _last_frame.clear ();
  _tracked_rt.clear (); _tracked_vb.clear (); _known_objs.clear ();

  return show_dlg;
}





