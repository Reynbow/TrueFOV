#pragma once

#include <string>

#include "states.h"

namespace cfg {

constexpr float kMinFov = 30.f;
constexpr float kMaxFov = 150.f;
constexpr float kMinMenuScale = 0.75f;
constexpr float kMaxMenuScale = 1.5f;  // the UI further caps it to what fits on screen

struct Settings {
  bool enabled = true;
  float fov = 90.f;  // the game's own gameplay FOV is 80
  float zoomFov = 0.f;  // FOV while zoomed in (holding right-click); 0 = the game's own zoom
  float viewmodelFov = 0.f;  // FOV of the first-person arms and held items; 0 = the game's own
  float transitionSeconds = 0.25f;
  bool keepGameFovEffects = true;
  bool popup = true;
  float menuScale = 1.f;
  int keyMenu = 0x76;     // F7
  int keyFovUp = 0x21;    // Page Up
  int keyFovDown = 0x22;  // Page Down
  int keyToggle = 0x23;   // End
  int keyThirdPerson = 0x77;  // F8
  bool applyInState[states::kMax] = {};  // indexed like states::kAll
  // Third-person view (experimental): camera offset from the eyes, in centimetres.
  bool tpEnabled = false;
  float tpFov = 95.f;        // FOV in third person (instead of the gameplay FOV)
  float tpDistance = 130.f;  // behind
  float tpHeight = -15.f;    // above
  float tpSide = 35.f;       // to the right
  bool tpCollision = true;   // keep the camera out of walls
  bool tpFullBody = true;    // show the full character model instead of the first-person body
};

constexpr float kMinTpDistance = 40.f, kMaxTpDistance = 400.f;
constexpr float kMinTpHeight = -60.f, kMaxTpHeight = 100.f;
constexpr float kMinTpSide = -100.f, kMaxTpSide = 100.f;

extern Settings S;

Settings Defaults();
void Load(const std::wstring& path);
void Save();
void MarkDirty();
void SaveIfDirty(double now);
void ResetToDefaults();  // keeps key bindings

}  // namespace cfg
