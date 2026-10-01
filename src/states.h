#pragma once

// The game's EGameState enum (UTownfallGameInstance keeps a stack of these). The mod uses it
// to tell free-roam gameplay apart from cutscenes, item examination and other scripted cameras.

#include <cstdint>
#include <vector>

#include "pdb.h"

namespace states {

struct Info {
  const char* name;      // EGameState enumerator
  const wchar_t* label;  // shown in the menu and status line
  bool applyByDefault;   // use the custom FOV in this state out of the box
  bool overlay;          // shown on top of another state (pause menu...): use the state underneath
  bool inMenu;           // has a toggle in the in-game menu
  int64_t value;         // resolved from the PDB; -1 if this game build doesn't have it
  const char* follows = nullptr;  // a transition into and out of this state: uses its setting
};

constexpr int kMax = 48;
extern Info kAll[];
extern const int kCount;

void AddRequests(std::vector<pdb::Request>& reqs);
void BuildLookup();
int IndexOfValue(int value);  // -1 if unknown
int IndexOfName(const char* name);  // -1 if unknown
// Index of the state whose setting applies in state |index|: its own, or the one it follows.
int SettingIndex(int index);

}  // namespace states
