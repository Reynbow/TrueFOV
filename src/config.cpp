#include "config.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cwchar>

#include "log.h"
#include "util.h"

namespace cfg {

Settings S;

namespace {

std::wstring g_path;
bool g_dirty = false;
double g_dirtySince = 0;

std::wstring Widen(const char* s) {
  std::wstring w;
  while (*s) w.push_back(static_cast<wchar_t>(*s++));
  return w;
}

bool ReadRaw(const wchar_t* section, const wchar_t* key, wchar_t (&buf)[64]) {
  GetPrivateProfileStringW(section, key, L"", buf, 64, g_path.c_str());
  return buf[0] != 0;
}

float ReadFloat(const wchar_t* section, const wchar_t* key, float def) {
  wchar_t buf[64];
  if (!ReadRaw(section, key, buf)) return def;
  wchar_t* end = nullptr;
  const float v = wcstof(buf, &end);
  return end != buf ? v : def;
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def) {
  wchar_t buf[64];
  if (!ReadRaw(section, key, buf)) return def;
  wchar_t* end = nullptr;
  const long v = wcstol(buf, &end, 0);  // accepts decimal or 0x hex
  return end != buf ? static_cast<int>(v) : def;
}

// Clamps a setting into range; NaN or infinity (e.g. "nan" typed into the ini) falls back to |def|.
float Sanitized(float v, float lo, float hi, float def) { return std::isfinite(v) ? util::Clamp(v, lo, hi) : def; }

void Sanitize(Settings& s) {
  const Settings d;
  s.fov = Sanitized(s.fov, kMinFov, kMaxFov, d.fov);
  if (s.zoomFov != 0.f) s.zoomFov = Sanitized(s.zoomFov, kMinFov, kMaxFov, d.zoomFov);
  if (s.viewmodelFov != 0.f) s.viewmodelFov = Sanitized(s.viewmodelFov, kMinFov, kMaxFov, d.viewmodelFov);
  s.transitionSeconds = Sanitized(s.transitionSeconds, 0.f, 2.f, d.transitionSeconds);
  s.menuScale = Sanitized(s.menuScale, kMinMenuScale, kMaxMenuScale, d.menuScale);
  s.tpFov = Sanitized(s.tpFov, kMinFov, kMaxFov, d.tpFov);
  s.tpDistance = Sanitized(s.tpDistance, kMinTpDistance, kMaxTpDistance, d.tpDistance);
  s.tpHeight = Sanitized(s.tpHeight, kMinTpHeight, kMaxTpHeight, d.tpHeight);
  s.tpSide = Sanitized(s.tpSide, kMinTpSide, kMaxTpSide, d.tpSide);
}

}  // namespace

Settings Defaults() {
  Settings d;
  for (int i = 0; i < states::kCount; ++i) d.applyInState[i] = states::kAll[i].applyByDefault;
  return d;
}

void Load(const std::wstring& path) {
  g_path = path;
  S = Defaults();
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    S.enabled = ReadInt(L"General", L"Enabled", S.enabled) != 0;
    S.fov = ReadFloat(L"General", L"GameplayFOV", S.fov);
    S.zoomFov = ReadFloat(L"General", L"ZoomFOV", S.zoomFov);
    S.viewmodelFov = ReadFloat(L"General", L"ViewmodelFOV", S.viewmodelFov);
    S.transitionSeconds = ReadFloat(L"General", L"TransitionSeconds", S.transitionSeconds);
    S.keepGameFovEffects = ReadInt(L"General", L"KeepGameFovEffects", S.keepGameFovEffects) != 0;
    S.popup = ReadInt(L"General", L"ShowPopup", S.popup) != 0;
    S.menuScale = ReadFloat(L"General", L"MenuScale", S.menuScale);
    S.keyMenu = ReadInt(L"Keys", L"ToggleMenu", S.keyMenu);
    S.keyFovUp = ReadInt(L"Keys", L"FovUp", S.keyFovUp);
    S.keyFovDown = ReadInt(L"Keys", L"FovDown", S.keyFovDown);
    S.keyToggle = ReadInt(L"Keys", L"ToggleMod", S.keyToggle);
    S.keyThirdPerson = ReadInt(L"Keys", L"ToggleThirdPerson", S.keyThirdPerson);
    S.tpEnabled = ReadInt(L"ThirdPerson", L"Enabled", S.tpEnabled) != 0;
    S.tpFov = ReadFloat(L"ThirdPerson", L"FOV", S.tpFov);
    S.tpDistance = ReadFloat(L"ThirdPerson", L"Distance", S.tpDistance);
    S.tpHeight = ReadFloat(L"ThirdPerson", L"Height", S.tpHeight);
    S.tpSide = ReadFloat(L"ThirdPerson", L"Side", S.tpSide);
    S.tpCollision = ReadInt(L"ThirdPerson", L"CameraCollision", S.tpCollision) != 0;
    S.tpFullBody = ReadInt(L"ThirdPerson", L"FullBody", S.tpFullBody) != 0;
    for (int i = 0; i < states::kCount; ++i) {
      if (states::kAll[i].overlay || states::kAll[i].follows) continue;
      S.applyInState[i] = ReadInt(L"ApplyInStates", Widen(states::kAll[i].name).c_str(), S.applyInState[i]) != 0;
    }
    Sanitize(S);
  }
  Save();  // creates the file on first run and adds any options introduced by updates
  LOG("Settings: enabled=%d fov=%.1f zoom=%.1f viewmodel=%.1f transition=%.2f keepEffects=%d", S.enabled, S.fov,
      S.zoomFov, S.viewmodelFov, S.transitionSeconds, S.keepGameFovEffects);
}

void Save() {
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_path.c_str(), L"w") != 0 || !f) {
    LOG("Couldn't write %ls", g_path.c_str());
    return;
  }
  fprintf(f,
          "; Townfall FOV settings. Change them in game with the menu (F7 by default), or edit this file while\n"
          "; the game is closed.\n\n[General]\n");
  fprintf(f, "Enabled=%d\n", S.enabled ? 1 : 0);
  fprintf(f, "; Gameplay field of view in degrees, same scale as the console 'fov' command. The game uses 80.\n");
  fprintf(f, "GameplayFOV=%.1f\n", S.fov);
  fprintf(f, "; FOV while zoomed in (holding right-click). 0 = the game's own zoom, whatever GameplayFOV is.\n");
  fprintf(f, "ZoomFOV=%.1f\n", S.zoomFov);
  fprintf(f, "; FOV of your first-person arms and held items. 0 = the game's own.\n");
  fprintf(f, "ViewmodelFOV=%.1f\n", S.viewmodelFov);
  fprintf(f, "; Seconds to blend between your FOV and the game's when gameplay starts or stops.\n");
  fprintf(f, "TransitionSeconds=%.2f\n", S.transitionSeconds);
  fprintf(f, "; 1 = keep the game's own gameplay FOV changes, scaled around your FOV. 0 = always exactly GameplayFOV.\n");
  fprintf(f, "KeepGameFovEffects=%d\n", S.keepGameFovEffects ? 1 : 0);
  fprintf(f, "; Show the FOV popup when using the FOV hotkeys.\n");
  fprintf(f, "ShowPopup=%d\n", S.popup ? 1 : 0);
  fprintf(f, "MenuScale=%.2f\n", S.menuScale);
  fprintf(f,
          "\n[Keys]\n; Windows virtual-key codes: "
          "https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes\n");
  fprintf(f, "ToggleMenu=0x%02X\nFovUp=0x%02X\nFovDown=0x%02X\nToggleMod=0x%02X\nToggleThirdPerson=0x%02X\n",
          S.keyMenu, S.keyFovUp, S.keyFovDown, S.keyToggle, S.keyThirdPerson);
  fprintf(f,
          "\n[ThirdPerson]\n"
          "; Experimental third-person view. Camera offset from the eyes in centimetres: Distance behind,\n"
          "; Height above, Side to the right (negative = left).\n");
  fprintf(f, "Enabled=%d\n", S.tpEnabled ? 1 : 0);
  fprintf(f, "; Field of view in third person, used instead of GameplayFOV.\nFOV=%.1f\n", S.tpFov);
  fprintf(f, "Distance=%.0f\nHeight=%.0f\nSide=%.0f\n", S.tpDistance, S.tpHeight, S.tpSide);
  fprintf(f, "; 1 = keep the camera out of walls.\nCameraCollision=%d\n", S.tpCollision ? 1 : 0);
  fprintf(f, "; 1 = show the full character model (with head) instead of the first-person body.\nFullBody=%d\n",
          S.tpFullBody ? 1 : 0);
  fprintf(f,
          "\n[ApplyInStates]\n"
          "; 1 = use GameplayFOV in this game state, 0 = leave the game's camera alone.\n"
          "; The pause menu and tutorial cards aren't listed: they follow the state underneath them. Nor is\n"
          "; the map transition (raising and lowering the map): it follows InGame_UsingMap.\n");
  for (int i = 0; i < states::kCount; ++i) {
    if (states::kAll[i].overlay || states::kAll[i].follows) continue;
    fprintf(f, "%s=%d\n", states::kAll[i].name, S.applyInState[i] ? 1 : 0);
  }
  fclose(f);
}

void MarkDirty() {
  g_dirty = true;
  g_dirtySince = util::Now();
}

void SaveIfDirty(double now) {
  if (g_dirty && now - g_dirtySince > 1.0) {
    g_dirty = false;
    Save();
  }
}

void ResetToDefaults() {
  Settings d = Defaults();
  d.keyMenu = S.keyMenu;
  d.keyFovUp = S.keyFovUp;
  d.keyFovDown = S.keyFovDown;
  d.keyToggle = S.keyToggle;
  d.keyThirdPerson = S.keyThirdPerson;
  S = d;
  MarkDirty();
}

}  // namespace cfg
