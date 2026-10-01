#pragma once

// Gameplay-only FOV. The player camera manager's ApplyCameraModifiers runs once per view target
// every frame; the hook rescales the FOV only when that view target is the player character and
// the game is in a gameplay state. Cutscene and interaction cameras are separate view targets
// (or flagged states) and are never touched; camera blends between them interpolate naturally.

namespace fov {

enum class Reason { NoPlayer, Disabled, Applying, StateExcluded, UnknownState, InteractionCamera, CloseUpCamera };

struct Status {
  bool cameraHooked = false;
  bool hasPlayer = false;
  int effectiveState = -1;  // states::kAll index, skipping overlays like the pause menu
  int rawState = -1;        // EGameState value on top of the stack
  Reason reason = Reason::NoPlayer;
  float alpha = 0.f;        // blend weight: 0 = game FOV, 1 = your FOV
  float smoothedFov = 0.f;  // your FOV, eased so hotkey changes zoom smoothly
  float lastIn = 0.f;       // FOV the game wanted for the player camera last frame
  float lastOut = 0.f;      // FOV after the mod
  float lastBase = 0.f;     // the game's gameplay FOV that "keep effects" scales around
  float gameBase = 0.f;     // the game's gameplay FOV (ATownfallPlayerCharacter::CameraFieldOfView)
  // Zooming in (holding right-click): the game eases its FOV well below its gameplay FOV.
  bool zooming = false;
  float zoomAlpha = 0.f;    // 0 = not zoomed, 1 = zoomed in; eases at the game's own zoom speed
  float gameZoomFov = 0.f;  // the FOV the game's zoom settles at, once seen (0 = not yet)
  // First-person meshes (arms, held items), drawn with a FOV of their own in gameplay.
  float gameViewmodelFov = 0.f;  // the game's FOV for them, last seen (0 = not yet)
  float lastViewmodelOut = 0.f;  // after the mod, last frame (0 = not drawn with their own FOV)
  double lastPovTime = -1.0;
  float lockedFov = 0.f;    // APlayerCameraManager::LockedFOV (the console 'fov' command)
  // Camera controller signals (for the guards and the diagnostic log).
  bool lensOverride = false;  // borrowing a cinematic camera's lens
  int closeUpState = 0;       // beauty (close-up) camera transition state, 0 = idle
  const char* activeCamera = "unknown";
  void* character = nullptr;      // the player character this frame (nullptr outside gameplay)
  void* cameraManager = nullptr;  // the player camera manager this frame
};

extern Status St;

// The camera actor the camera controller moves between two of the player's own views (lowering
// the map, some interactions), or nullptr.
void* TransitionActor(void* character);

// Called once per frame on the game thread (from the viewport's PostRender).
void OnFrame(void* viewportClient, double now);
// The view the frame is rendered from (FMinimalViewInfo), after the game's local player has
// filled it in: sets the first-person meshes' FOV to the viewmodel FOV.
void ApplyToView(void* view);
void RequestClearLockedFov();

}  // namespace fov
