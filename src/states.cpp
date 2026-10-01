#include "states.h"

#include <cstring>

namespace states {

// clang-format off
Info kAll[] = {
    // name                                  label                        default overlay menu
    {"Unspecified",                          L"Unspecified",               false, false, false, -1},
    {"Loading_Level",                        L"Loading",                   false, false, false, -1},
    {"FrontEnd_Splash",                      L"Splash screen",             false, false, false, -1},
    {"FrontEnd_MainMenu",                    L"Main menu",                 false, false, false, -1},
    {"InGame_FPS",                           L"Gameplay",                  true,  false, false, -1},
    {"InGame_Cutscene",                      L"Cutscene",                  false, false, false, -1},
    {"InGame_CombatQTE",                     L"Combat QTE",                true,  false, true,  -1},
    {"InGame_Hiding",                        L"Hiding",                    true,  false, true,  -1},
    {"InGame_Examine",                       L"Examining an item",         false, false, true,  -1},
    {"InGame_UsingMap",                      L"Using the map",             false, false, true,  -1},
    {"InGame_TestCutscene",                  L"Cutscene",                  false, false, false, -1},
    {"InGame_Healing",                       L"Healing",                   true,  false, true,  -1},
    {"InGame_DeviceInteract",                L"Using a device",            false, false, false, -1},
    {"InGame_InteractionZone",               L"Interaction zone",          true,  false, true,  -1},
    {"InGame_Cinematic",                     L"Cinematic",                 false, false, false, -1},
    {"InGame_RadioWalkAndTalk",              L"Radio call",                true,  false, true,  -1},
    {"InGame_UIInteract",                    L"UI interaction",            false, false, false, -1},
    {"InGame_QuickInventory",                L"Quick inventory",           true,  false, true,  -1},
    {"InGame_PCInterfacePasscode",           L"PC passcode",               false, false, false, -1},
    {"InGame_SimonOS",                       L"SimonOS",                   false, false, false, -1},
    {"InGame_CombinationLock",               L"Combination lock",          false, false, false, -1},
    {"InGame_UsingLadder",                   L"Ladder",                    true,  false, true,  -1},
    {"InGame_PauseMenu",                     L"Pause menu",                false, true,  false, -1},
    {"InGame_TutorialCard",                  L"Tutorial card",             false, true,  false, -1},
    {"InGame_CCTVMiniGame",                  L"CCTV",                      false, false, false, -1},
    {"InGame_InteractionQTE",                L"Interaction QTE",           false, false, false, -1},
    {"InGame_FinalResults",                  L"Final results",             false, false, false, -1},
    {"InGame_SignalObjectTuning",            L"Signal tuning",             false, false, false, -1},
    {"InGame_InteractionTransition",         L"Interaction transition",    false, false, false, -1},
    {"InGame_PlayerBinaryChoice",            L"Choice",                    false, false, false, -1},
    {"InGame_BoardroomSlideProjectorRack",   L"Slide projector",           false, false, false, -1},
    {"InGame_IntubationMiniGame",            L"Intubation",                false, false, false, -1},
    // Raising and lowering the map (it comes before and after InGame_UsingMap).
    {"InGame_MapTransition",                 L"Map transition",            false, false, false, -1, "InGame_UsingMap"},
    {"Credits",                              L"Credits",                   false, false, false, -1},
};
// clang-format on
const int kCount = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));
static_assert(sizeof(kAll) / sizeof(kAll[0]) <= kMax, "raise states::kMax");

namespace {
int g_lookup[256];
}

void AddRequests(std::vector<pdb::Request>& reqs) {
  for (int i = 0; i < kCount; ++i) {
    const bool required = std::strcmp(kAll[i].name, "InGame_FPS") == 0;
    reqs.push_back({pdb::Kind::EnumValue, "EGameState", kAll[i].name, &kAll[i].value, !required});
  }
}

void BuildLookup() {
  for (int& v : g_lookup) v = -1;
  for (int i = 0; i < kCount; ++i) {
    if (kAll[i].value >= 0 && kAll[i].value < 256) g_lookup[kAll[i].value] = i;
  }
}

int IndexOfValue(int value) { return value >= 0 && value < 256 ? g_lookup[value] : -1; }

int IndexOfName(const char* name) {
  for (int i = 0; i < kCount; ++i) {
    if (std::strcmp(kAll[i].name, name) == 0) return i;
  }
  return -1;
}

int SettingIndex(int index) {
  if (index < 0 || index >= kCount || !kAll[index].follows) return index;
  for (int i = 0; i < kCount; ++i) {
    if (std::strcmp(kAll[i].name, kAll[index].follows) == 0) return i;
  }
  return index;
}

}  // namespace states
