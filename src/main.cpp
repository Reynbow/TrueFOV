// Townfall FOV: gameplay-only field of view for Silent Hill: Townfall.
//
// Built as winmm.dll: the game imports winmm, so Windows loads this file from the game's
// Binaries\Win64 folder. Every winmm export forwards to the real system DLL (proxy_exports.h).

#include <windows.h>

#include <cwchar>
#include <string>
#include <vector>

#include "canvas.h"
#include "config.h"
#include "fov.h"
#include "log.h"
#include "pdb.h"
#include "proxy_exports.h"
#include "states.h"
#include "thirdperson.h"
#include "ue.h"
#include "ui.h"
#include "util.h"
#include "version.h"

namespace {

constexpr char kVersion[] = TFOV_VERSION_STRING;

HMODULE g_self = nullptr;
long g_frameFaults = 0;

using PostRenderFn = void(__fastcall*)(void* self, void* canvas);

void Frame(void* viewportClient, void* canvas) {
  const double now = util::Now();
  fov::OnFrame(viewportClient, now);
  tp::OnFrame();
  ui::OnFrame(viewportClient, canvas, now);
  cfg::SaveIfDirty(now);
}

void SafeFrame(void* viewportClient, void* canvas) {
  __try {
    Frame(viewportClient, canvas);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    canvas::EmergencyRestore();  // put back a borrowed font size / canvas state
    if (InterlockedIncrement(&g_frameFaults) <= 5) LOG("Recovered from a fault in the frame hook (0x%08lX)", GetExceptionCode());
  }
}

void __fastcall HookPostRender(void* self, void* canvas) {
  reinterpret_cast<PostRenderFn>(ue::A.PostRender)(self, canvas);
  if (canvas) SafeFrame(self, canvas);
}

bool SafeHookVirtual(void* object, int64_t original, void* hook) {
  __try {
    return ue::HookVirtual(object, original, hook);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void* ReadGameViewport() {
  __try {
    void* engine = *reinterpret_cast<void**>(ue::A.GEngine);
    return engine ? ue::Ptr(engine, ue::O.Eng_GameViewport) : nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

std::wstring ModuleDirectory(HMODULE module) {
  wchar_t path[MAX_PATH * 2] = {};
  GetModuleFileNameW(module, path, MAX_PATH * 2);
  std::wstring dir = path;
  return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

DWORD WINAPI InitThread(void*) {
  const std::wstring dir = ModuleDirectory(g_self);
  // One log per session: each launch starts it fresh. (1.1.2 also kept the previous session's.)
  DeleteFileW((dir + L"TownfallFOV.previous.log").c_str());
  logx::Open((dir + L"TownfallFOV.log").c_str());
  LOG("Townfall FOV %s", kVersion);
  cfg::Load(dir + L"TownfallFOV.ini");

  std::vector<pdb::Request> reqs;
  ue::AddRequests(reqs);
  states::AddRequests(reqs);
  std::string error;
  if (!pdb::Resolve(pdb::DescribeModule(GetModuleHandleW(nullptr)), reqs, dir + L"TownfallFOV.cache", error)) {
    LOG("Can't start: %s. The game runs normally without the mod.", error.c_str());
    return 0;
  }
  states::BuildLookup();

  // The engine creates its viewport a few seconds into startup; hook its PostRender to get a
  // per-frame callback on the game thread with a canvas to draw on.
  for (;;) {
    if (void* viewport = ReadGameViewport()) {
      if (SafeHookVirtual(viewport, ue::A.PostRender, reinterpret_cast<void*>(&HookPostRender))) {
        LOG("Ready");
      } else {
        LOG("Can't start: UGameViewportClient::PostRender isn't in the viewport's vtable");
      }
      return 0;
    }
    Sleep(100);
  }
}

bool IsGameProcess() {
  wchar_t path[MAX_PATH * 2] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
  const wchar_t* name = wcsrchr(path, L'\\');
  return _wcsicmp(name ? name + 1 : path, L"Townfall-Win64-Shipping.exe") == 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    g_self = module;
    if (IsGameProcess()) {
      if (HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
  }
  return TRUE;
}
