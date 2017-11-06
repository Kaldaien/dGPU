//
// Copyright 2017  Andon  "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <SpecialK/dxgi_backend.h>
#include <SpecialK/config.h>
#include <SpecialK/command.h>
#include <SpecialK/framerate.h>
#include <SpecialK/ini.h>
#include <SpecialK/parameter.h>
#include <SpecialK/utility.h>
#include <SpecialK/log.h>
#include <SpecialK/steam_api.h>

#include <SpecialK/input/input.h>
#include <SpecialK/input/xinput.h>

#include <SpecialK/hooks.h>
#include <SpecialK/core.h>
#include <process.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_d3d11.h>

#include <atlbase.h>

#define DGPU_VERSION_NUM L"0.0.7"
#define DGPU_VERSION_STR L".hack//G.P.U. v " DGPU_VERSION_NUM

volatile LONG __DGPU_init = FALSE;


//
//
// It's hideous, don't look!
//
//


extern HMODULE
__stdcall
SK_ReShade_GetDLL (void);

extern bool
__stdcall
SK_FetchVersionInfo (const wchar_t* wszProduct);

extern HRESULT
__stdcall
SK_UpdateSoftware   (const wchar_t* wszProduct);

typedef void (__stdcall *SK_ReShade_SetResolutionScale_pfn)(float fScale);
static SK_ReShade_SetResolutionScale_pfn SK_ReShade_SetResolutionScale = nullptr;

using SK_PlugIn_ControlPanelWidget_pfn = void (__stdcall         *)(void);
static SK_PlugIn_ControlPanelWidget_pfn SK_PlugIn_ControlPanelWidget_Original = nullptr;

struct dpgu_cfg_s
{
  sk::ParameterFactory factory;

  struct antialiasing_s
  {
    sk::ParameterFloat* scale = nullptr;
  } antialiasing;
} dgpu_config;


struct
{
  float scale = 0.0f;
} aa_prefs;

extern void
__stdcall
SK_SetPluginName (std::wstring name);

unsigned int
__stdcall
SK_DGPU_CheckVersion (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  extern volatile LONG   SK_bypass_dialog_active;
  InterlockedIncrement (&SK_bypass_dialog_active);

  if (SK_FetchVersionInfo (L"dGPU"))
    SK_UpdateSoftware (L"dGPU");

  InterlockedDecrement (&SK_bypass_dialog_active);

  return 0;
}

void
__stdcall
SK_DGPU_ControlPanel (void)
{
  if (ImGui::CollapsingHeader (".hack//G.U.", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TreePush ("");

    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.90f, 0.40f, 0.40f, 0.45f));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.90f, 0.45f, 0.45f, 0.80f));
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImVec4 (0.87f, 0.53f, 0.53f, 0.80f));

    bool aa = ImGui::CollapsingHeader ("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::IsItemHovered ())
      ImGui::SetTooltip ("This does not change anything in-game, set this to match your in-game setting so that ReShade works correctly.");

    if (aa)
    {
      bool changed = false;

      ImGui::TreePush ("");

      int level = aa_prefs.scale == 1.0f ? 1 :
                  aa_prefs.scale == 2.0f ? 2 :
                  aa_prefs.scale == 0.0f ? 0 :
                                           0;

      changed |= ImGui::RadioButton ("Low##AntiAliasLevel_DotHack",    &level, 0);
      ImGui::SameLine ();
      changed |= ImGui::RadioButton ("Medium##AntiAliasLevel_DotHack", &level, 1);
      if (ImGui::IsItemHovered ()) ImGui::SetTooltip ("Not recommended, please consider Low or High");
      ImGui::SameLine ();
      changed |= ImGui::RadioButton ("High##AntiAliasLevel_DotHack",   &level, 2);

      if (changed)
      {
        aa_prefs.scale = ( level == 0 ? 0.0f :
                           level == 1 ? 1.0f :
                           level == 2 ? 2.0f :
                                        1.0f );

        dgpu_config.antialiasing.scale->store (aa_prefs.scale);

        SK_GetDLLConfig ()->write (SK_GetDLLConfig ()->get_filename ());

        if (SK_ReShade_SetResolutionScale != nullptr)
        {
          SK_ReShade_SetResolutionScale (aa_prefs.scale);
        }
      }

      ImGui::TreePop  ();
    }

    if (ImGui::CollapsingHeader ("Texture Management", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::TreePush    ("");
      ImGui::Checkbox    ("Generate Mipmaps", &config.textures.d3d11.generate_mips);

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip    ();
        ImGui::TextUnformatted ("Builds Complete Mipchains (Mipmap LODs) for all Textures");
        ImGui::Separator       ();
        ImGui::BulletText      ("SIGNIFICANLY reduces texture aliasing");
        ImGui::BulletText      ("May introduce slightly longer load-times, but should be mitigated over time by texture caching.");
        ImGui::EndTooltip      ();
      }

      if (! config.textures.d3d11.generate_mips)
      {
        ImGui::SameLine    ();
        ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.1f, 1.0f), " Enable to reduce texture aliasing");
      }

      else
      {
        ImGui::SameLine    ();
        ImGui::TextColored (ImVec4 (1.0f, 0.5f, 0.1f, 1.0f), " Disable to reduce load time");
      }

      ImGui::TreePop           ();
    }

    ImGui::PopStyleColor (3);
    ImGui::TreePop ();
  }
}


HRESULT
STDMETHODCALLTYPE
SK_DGPU_PresentFirstFrame (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
  UNREFERENCED_PARAMETER (pSwapChain);
  UNREFERENCED_PARAMETER (SyncInterval);
  UNREFERENCED_PARAMETER (Flags);

  while (! InterlockedAdd (&__DGPU_init, 0))
    SleepEx (16, FALSE);

  if (SK_ReShade_SetResolutionScale == nullptr)
  {
    SK_ReShade_SetResolutionScale =
      (SK_ReShade_SetResolutionScale_pfn)GetProcAddress (
        GetModuleHandle (L"dxgi.dll"),
          "SK_ReShade_SetResolutionScale"
      );
  }

  if (SK_ReShade_SetResolutionScale != nullptr)
    SK_ReShade_SetResolutionScale (aa_prefs.scale);

  return S_OK;
}

void
SK_DGPU_InitPlugin (void)
{
  SK_SetPluginName (DGPU_VERSION_STR);

  dgpu_config.antialiasing.scale =
      dynamic_cast <sk::ParameterFloat *>
        (dgpu_config.factory.create_parameter <float> (L"Anti-Aliasing Scale"));

  dgpu_config.antialiasing.scale->register_to_ini ( SK_GetDLLConfig (),
                                                      L"dGPU.Antialiasing",
                                                        L"Scale" );

  dgpu_config.antialiasing.scale->load (aa_prefs.scale);

  SK_CreateFuncHook (       L"SK_PlugIn_ControlPanelWidget",
                              SK_PlugIn_ControlPanelWidget,
                                SK_DGPU_ControlPanel,
     static_cast_p2p <void> (&SK_PlugIn_ControlPanelWidget_Original) );
  MH_QueueEnableHook (        SK_PlugIn_ControlPanelWidget           );

  //SK_CreateFuncHook ( L"SK_ImGUI_DrawEULA_PlugIn",
  //                      SK_ImGui_DrawEULA_PlugIn,
   //                            SK_IT_EULA_Insert,
  //                              &dontcare     );
  //
  //MH_QueueEnableHook (SK_ImGui_DrawEULA_PlugIn);

  SK_ReShade_SetResolutionScale =
    (SK_ReShade_SetResolutionScale_pfn)GetProcAddress (
      GetModuleHandle (L"dxgi.dll"),
        "SK_ReShade_SetResolutionScale"
  );

  MH_ApplyQueued ();

  InterlockedExchange (&__DGPU_init, 1);

  if (! SK_IsInjected ())
    SK_DGPU_CheckVersion (nullptr);
};