#include "fov.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "config.h"
#include "log.h"
#include "states.h"
#include "thirdperson.h"
#include "ue.h"
#include "util.h"

namespace fov {

Status St;

namespace {

using ApplyCameraModifiersFn = void(__fastcall*)(void* self, float deltaTime, void* pov);
using StaticClassFn = void*(__fastcall*)();

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr float kMinOut = 5.f;
constexpr float kMaxOut = 170.f;
constexpr uint8_t kFirstPersonParams = 2;  // FMinimalViewInfo::bUseFirstPersonParameters's byte
// If the player camera hasn't been rendered for this long, it was off screen (cutscene camera,
// loading...) and there's nothing on screen to blend from when it comes back.
constexpr double kOffScreenSeconds = 0.2;
// Camera trace in the log: every frame (at most 60 a second) for this long after each change.
constexpr double kTraceSeconds = 2.5;
constexpr int kMaxTraceLines = 3000;
// The game's zoom (holding right-click) eases its FOV about 30 degrees below its gameplay FOV;
// anything this far below counts as zooming. A per-frame rise this big means it's easing back.
constexpr float kZoomMinDepth = 3.f;
constexpr float kZoomStep = 0.1f;
// The game eases its zoom like FInterpTo at this speed (measured); the zoom FOV eases the same way.
constexpr float kZoomSpeed = 15.f;

struct TArrayBytes {
  uint8_t* data;
  int32_t num;
  int32_t max;
};

void* g_playerClass = nullptr;
void** g_checkedVtable = nullptr;
double g_lastFrame = 0;
bool g_clearLockedRequested = false;
long g_faults = 0;
uint32_t g_frame = 0;     // PostRender frames so far
uint32_t g_povFrame = 0;  // g_frame when the player camera was last updated

// Your FOV: the third-person one while that view is in use.
float UserFov() { return tp::ViewActive() ? cfg::S.tpFov : cfg::S.fov; }

double TanHalf(double deg) { return std::tan(deg * 0.5 * kDegToRad); }
double FromTanHalf(double t) { return 2.0 * std::atan(t) / kDegToRad; }
double Ease(double a) { return a * a * (3.0 - 2.0 * a); }  // smoothstep
bool Plausible(float fov) { return fov > 1.f && fov < 179.f; }

// Reads the top of the game state stack; |effective| skips overlays such as the pause menu so
// the background camera doesn't change when you pause.
void ReadGameState(void* gameInstance, int& raw, int& effective) {
  raw = effective = -1;
  if (!gameInstance) return;
  const auto& stack = ue::At<TArrayBytes>(gameInstance, ue::O.GI_GameStateStack);
  if (!stack.data || stack.num <= 0 || stack.num > 256) return;
  raw = stack.data[stack.num - 1];
  for (int i = stack.num - 1; i >= 0; --i) {
    const int index = states::IndexOfValue(stack.data[i]);
    if (index >= 0 && states::kAll[index].overlay) continue;
    effective = index;
    return;
  }
}

const char* ActiveCameraName(void* character, void* controller) {
  if (!controller || ue::O.Cam_ActiveCamera < 0) return "unknown";
  void* active = ue::Ptr(controller, ue::O.Cam_ActiveCamera);
  if (!active) return "none";
  if (ue::O.Char_FPSCamera >= 0 && ue::Ptr(character, ue::O.Char_FPSCamera) == active) return "character";
  const struct {
    int64_t offset;
    const char* name;
  } cameras[] = {{ue::O.Cam_FirstPersonCamera, "first-person"},
                 {ue::O.Cam_HeadCamera, "head"},
                 {ue::O.Cam_AnimatedCamera, "animated"},
                 {ue::O.Cam_TransitionCamera, "transition"},
                 {ue::O.Cam_OuterCamera, "outer"}};
  for (const auto& camera : cameras) {
    if (camera.offset >= 0 && ue::Ptr(controller, camera.offset) == active) return camera.name;
  }
  return "other";
}

// Reads everything the decision depends on into St.
void ReadSituation(void* character, void* gameInstance) {
  ReadGameState(gameInstance, St.rawState, St.effectiveState);
  void* controller = character ? ue::WeakGet(character, ue::O.Char_CameraController) : nullptr;
  St.lensOverride = controller && ue::O.Cam_OverridingCine >= 0 && ue::At<bool>(controller, ue::O.Cam_OverridingCine);
  St.closeUpState = controller && ue::O.Cam_BeautyState >= 0 ? ue::At<uint8_t>(controller, ue::O.Cam_BeautyState) : 0;
  St.activeCamera = ActiveCameraName(character, controller);
  St.gameBase = character && ue::O.Char_CameraFieldOfView >= 0 ? ue::At<float>(character, ue::O.Char_CameraFieldOfView) : 0.f;
}

// Holding right-click, the game eases its FOV well below its gameplay FOV, and eases it back when
// you let go; zoomAlpha follows at the same speed. Called from the camera update, once per frame,
// so the zoom FOV reacts in the same frame as the game's zoom. |continuous|: it also ran last
// frame. |restart|: the camera was off screen, so there's no zoom to carry on.
void TrackZoom(float in, float base, float dt, bool continuous, bool restart) {
  enum class Phase { Idle, In, Out };
  static Phase phase = Phase::Idle;
  static float prevIn = 0.f;
  static int steadyFrames = 0;
  if (restart) {
    phase = Phase::Idle;
    St.zoomAlpha = 0.f;
  }
  const bool prevValid = continuous;
  if (!Plausible(in) || !Plausible(base) || in >= base - kZoomMinDepth) {
    phase = Phase::Idle;
  } else {
    const float step = prevValid ? in - prevIn : 0.f;
    if (phase == Phase::Idle) {
      // Only while it's easing down: a cut to a camera that's already narrow isn't a zoom.
      if (step < -kZoomStep) phase = Phase::In;
    } else if (phase == Phase::In && step > kZoomStep) {
      phase = Phase::Out;  // let go: the game eases back
    } else if (phase == Phase::Out && step < -kZoomStep) {
      phase = Phase::In;  // zoomed again on the way out
    }
    // Where the game's zoom settles, to show in the menu.
    if (phase == Phase::In && prevValid && std::fabs(step) < 0.02f) {
      if (++steadyFrames >= 3) St.gameZoomFov = in;
    } else {
      steadyFrames = 0;
    }
  }
  prevIn = in;
  St.zooming = phase == Phase::In;
  const float target = St.zooming ? 1.f : 0.f;
  St.zoomAlpha += (target - St.zoomAlpha) * (std::min)(1.f, dt * kZoomSpeed);
  if (std::fabs(target - St.zoomAlpha) < 0.001f) St.zoomAlpha = target;
}

// Decides whether the player's camera should use the custom FOV right now (after ReadSituation).
Reason Decide(bool hasPlayer) {
  if (!hasPlayer) return Reason::NoPlayer;
  if (!cfg::S.enabled) return Reason::Disabled;
  if (St.effectiveState < 0) return Reason::UnknownState;
  // Raising and lowering the map use the map's setting, so the FOV doesn't dip on the way in or out.
  const int setting = states::SettingIndex(St.effectiveState);
  if (!cfg::S.applyInState[setting]) return Reason::StateExcluded;
  // In the gameplay states the camera controller sometimes borrows a cinematic camera's lens for an
  // interaction, or runs a separate close-up ("beauty") camera; leave those alone. States that are
  // off by default (using the map, examining an item) are scripted camera views to begin with:
  // when the player switches one on, they want their FOV there regardless.
  if (states::kAll[setting].applyByDefault) {
    if (St.lensOverride) return Reason::InteractionCamera;
    if (St.closeUpState != 0) return Reason::CloseUpCamera;
  }
  return Reason::Applying;
}

const char* ReasonName(Reason reason) {
  switch (reason) {
    case Reason::NoPlayer: return "no player";
    case Reason::Disabled: return "mod off";
    case Reason::Applying: return "applying";
    case Reason::StateExcluded: return "state switched off";
    case Reason::UnknownState: return "unknown state";
    case Reason::InteractionCamera: return "interaction camera (lens override)";
    case Reason::CloseUpCamera: return "close-up camera";
  }
  return "?";
}

const char* StateName(int index) { return index >= 0 ? states::kAll[index].name : "?"; }

// One log line whenever the camera situation changes, so "why isn't my FOV applied here?" can be
// answered from TownfallFOV.log. Returns true if it logged.
bool LogSituation(const char* viewTarget) {
  static int lastState = -2, lastRaw = -2, lastCloseUp = -1;
  static Reason lastReason = Reason::NoPlayer;
  static bool lastLens = false, lastZoom = false, logged = false;
  static const char* lastCamera = nullptr;
  static const char* lastViewTarget = nullptr;
  if (logged && lastState == St.effectiveState && lastRaw == St.rawState && lastReason == St.reason &&
      lastLens == St.lensOverride && lastCloseUp == St.closeUpState && lastZoom == St.zooming &&
      lastCamera == St.activeCamera && lastViewTarget == viewTarget) {
    return false;
  }
  logged = true;
  lastState = St.effectiveState;
  lastRaw = St.rawState;
  lastReason = St.reason;
  lastLens = St.lensOverride;
  lastCloseUp = St.closeUpState;
  lastZoom = St.zooming;
  lastCamera = St.activeCamera;
  lastViewTarget = viewTarget;
  LOG("Situation: state %s (top %d) | %s | view target: %s | camera: %s | lens override: %s | close-up: %d | "
      "zoom: %s | FOV %.1f -> %.1f",
      StateName(St.effectiveState), St.rawState, ReasonName(St.reason), viewTarget, St.activeCamera,
      St.lensOverride ? "yes" : "no", St.closeUpState, St.zooming ? "yes" : "no", St.lastIn, St.lastOut);
  return true;
}

// Frame-by-frame camera values for a few seconds after each situation change, so a transition
// (opening the map, a cutscene ending...) can be replayed from the log. |screenFov| is the FOV
// actually rendered; the player camera values are stale while another camera is on screen.
void Trace(double now, bool changed, bool povFresh, float screenFov) {
  static double hotUntil = -1.0, start = 0.0, last = -1.0;
  static int lines = 0;
  if (changed) {
    if (now > hotUntil) start = now;
    hotUntil = now + kTraceSeconds;
  }
  if (now > hotUntil || lines >= kMaxTraceLines || (!changed && now - last < 1.0 / 60.0)) return;
  last = now;
  if (++lines == kMaxTraceLines) {
    LOG("Camera trace: line limit reached, no more this session");
    return;
  }
  LOG("  %+.3fs %-22s %-18s alpha %.2f zoom %.2f | on screen %5.1f | player camera: game %5.1f base %5.1f -> "
      "%5.1f%s | viewmodel %5.1f -> %5.1f | lens %d | close-up %d | %s",
      now - start, StateName(St.effectiveState), ReasonName(St.reason), St.alpha, St.zoomAlpha, screenFov, St.lastIn,
      St.lastBase, St.lastOut, povFresh ? "" : " (not on screen)", St.lastViewmodelOut > 0.f ? St.gameViewmodelFov : 0.f,
      St.lastViewmodelOut, St.lensOverride ? 1 : 0, St.closeUpState, St.activeCamera);
}

void ApplyToPov(void* cameraManager, void* pov) {
  // Which view target is being updated? Only the current and pending ones reach the screen.
  char* vt = static_cast<char*>(pov) - ue::O.VT_POV;
  char* mgr = static_cast<char*>(cameraManager);
  if (vt != mgr + ue::O.PCM_ViewTarget && vt != mgr + ue::O.PCM_PendingViewTarget) return;

  // The player character's camera, or the transition camera standing in for it.
  void* target = ue::Ptr(vt, ue::O.VT_Target);
  void* pc = ue::Ptr(cameraManager, ue::O.PCM_PCOwner);
  void* character = pc ? ue::Ptr(pc, ue::O.Ctrl_Pawn) : nullptr;
  if (!target || !character || !g_playerClass || !ue::IsA(character, g_playerClass)) return;
  if (target != character && target != TransitionActor(character)) return;

  float& fov = ue::At<float>(pov, ue::O.POV_FOV);
  const float in = fov;
  St.lastIn = in;
  St.lastOut = in;
  St.lastBase = in;
  g_povFrame = g_frame;

  // Coming back on screen (e.g. a cutscene cut back to the player): start at the right FOV
  // instead of blending from whatever the blend weight decayed to while off screen.
  const double now = util::Now();
  const bool offScreen = now - St.lastPovTime > kOffScreenSeconds;
  if (offScreen) {
    ReadSituation(character, ue::CallWorldGetter(ue::A.GetGameInstance, character));
    St.reason = Decide(true);
    St.alpha = St.reason == Reason::Applying ? 1.f : 0.f;
  }
  St.lastPovTime = now;

  // The game's gameplay FOV (CameraParameters / NC.CameraManager.FOVOverrideValue).
  const float gameBase = ue::O.Char_CameraFieldOfView >= 0 ? ue::At<float>(character, ue::O.Char_CameraFieldOfView) : 0.f;
  static uint32_t zoomFrame = 0;
  static double zoomTime = 0.0;
  static bool zoomTracked = false;
  if (!zoomTracked || zoomFrame != g_frame) {  // the first player camera update this frame
    const bool continuous = zoomTracked && zoomFrame + 1 == g_frame;
    const float dt = continuous ? static_cast<float>(util::Clamp(now - zoomTime, 0.0, 0.1)) : 0.f;
    TrackZoom(in, gameBase, dt, continuous, offScreen);
    zoomTracked = true;
    zoomFrame = g_frame;
    zoomTime = now;
  }

  const double a = Ease(St.alpha);
  if (a <= 0.0 || !Plausible(in)) return;

  // Scaling around the game's gameplay FOV keeps any FOV animation the game plays on top of it.
  const double base = cfg::S.keepGameFovEffects && Plausible(gameBase) ? gameBase : in;
  St.lastBase = static_cast<float>(base);

  // Work in tan(fov/2) space so the scaling is proportional on screen.
  const double tIn = TanHalf(in);
  const double tUser = TanHalf(St.smoothedFov > 0.f ? St.smoothedFov : UserFov());
  double tTarget = cfg::S.keepGameFovEffects ? tIn * tUser / TanHalf(base) : tUser;
  // Zoomed in: towards the chosen zoom FOV, or by default the game's own zoomed FOV (not scaled by
  // your gameplay FOV, so the zoom looks the way the game made it).
  if (St.zoomAlpha > 0.f) {
    const double tZoom = cfg::S.zoomFov > 0.f ? TanHalf(cfg::S.zoomFov) : tIn;
    tTarget += (tZoom - tTarget) * St.zoomAlpha;
  }
  const double tOut = tIn + (tTarget - tIn) * a;
  const float out = util::Clamp(static_cast<float>(FromTanHalf(tOut)), kMinOut, kMaxOut);
  fov = out;
  St.lastOut = out;

  if (ue::O.POV_DesiredFOV >= 0) {
    float& desired = ue::At<float>(pov, ue::O.POV_DesiredFOV);
    if (Plausible(desired)) {
      desired = util::Clamp(static_cast<float>(FromTanHalf(TanHalf(desired) * tOut / tIn)), kMinOut, kMaxOut);
    }
  }
}

void SafeApplyToPov(void* cameraManager, void* pov) {
  __try {
    ApplyToPov(cameraManager, pov);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedIncrement(&g_faults);
  }
}

void __fastcall HookApplyCameraModifiers(void* self, float deltaTime, void* pov) {
  reinterpret_cast<ApplyCameraModifiersFn>(ue::A.ApplyCameraModifiers)(self, deltaTime, pov);
  SafeApplyToPov(self, pov);
  tp::OnCameraUpdate(self);
}

void EnsureCameraHook(void* cameraManager) {
  void** vtable = *static_cast<void***>(cameraManager);
  if (vtable == g_checkedVtable) return;
  g_checkedVtable = vtable;
  St.cameraHooked = ue::HookVirtual(cameraManager, ue::A.ApplyCameraModifiers,
                                    reinterpret_cast<void*>(&HookApplyCameraModifiers));
  if (!St.cameraHooked) LOG("Couldn't find ApplyCameraModifiers in camera manager vtable %p", static_cast<void*>(vtable));
}

}  // namespace

// While the player character stops being the view target, this actor stands in for its camera. It
// clones the first-person camera, lens and FOV included, so it gets the player camera's FOV too:
// otherwise the view drops to the game's FOV for the move and jumps back when the player camera
// returns.
void* TransitionActor(void* character) {
  if (!character || ue::O.Cam_TransitionActor < 0) return nullptr;
  void* controller = ue::WeakGet(character, ue::O.Char_CameraController);
  return controller ? ue::Ptr(controller, ue::O.Cam_TransitionActor) : nullptr;
}

void OnFrame(void* viewportClient, double now) {
  const float dt = g_lastFrame > 0 ? util::Clamp(static_cast<float>(now - g_lastFrame), 0.f, 0.1f) : 0.f;
  g_lastFrame = now;
  const bool povFresh = g_povFrame == g_frame;  // the camera hook ran for the player this frame
  ++g_frame;
  if (!g_playerClass) g_playerClass = reinterpret_cast<StaticClassFn>(ue::A.PlayerCharacterStaticClass)();

  void* world = ue::Ptr(viewportClient, ue::O.GVC_World);
  void* cameraManager = ue::CallWorldGetter(ue::A.GetPlayerCameraManager, world);
  // Same lookup as the camera hook: the pawn possessed by the camera manager's controller.
  void* character = nullptr;
  if (void* pc = cameraManager ? ue::Ptr(cameraManager, ue::O.PCM_PCOwner) : nullptr) {
    void* pawn = ue::Ptr(pc, ue::O.Ctrl_Pawn);
    if (pawn && ue::IsA(pawn, g_playerClass)) character = pawn;
  }

  St.lockedFov = 0.f;
  if (cameraManager) {
    EnsureCameraHook(cameraManager);
    if (ue::O.PCM_LockedFOV >= 0) {
      float& locked = ue::At<float>(cameraManager, ue::O.PCM_LockedFOV);
      if (g_clearLockedRequested) {
        LOG("Cleared console FOV override (was %.1f)", locked);
        locked = 0.f;
      }
      St.lockedFov = locked;
    }
  }
  g_clearLockedRequested = false;
  St.hasPlayer = character != nullptr;
  St.character = character;
  St.cameraManager = cameraManager;

  ReadSituation(character, ue::CallWorldGetter(ue::A.GetGameInstance, world));
  St.reason = Decide(character != nullptr);
  const float target = St.reason == Reason::Applying ? 1.f : 0.f;
  const float step = cfg::S.transitionSeconds > 0.001f ? dt / cfg::S.transitionSeconds : 1.f;
  St.alpha = target > St.alpha ? (std::min)(target, St.alpha + step) : (std::max)(target, St.alpha - step);
  if (character) {
    // What the screen shows: the player's camera (or its transition camera), a blend, or another
    // actor's camera.
    void* transition = TransitionActor(character);
    const auto players = [&](void* actor) { return actor && (actor == character || actor == transition); };
    void* viewTarget = ue::Ptr(cameraManager, ue::O.PCM_ViewTarget + ue::O.VT_Target);
    void* pending = ue::Ptr(cameraManager, ue::O.PCM_PendingViewTarget + ue::O.VT_Target);
    const char* shown = "other actor";
    if (players(viewTarget)) {
      shown = pending && !players(pending) ? "player, blending away"
              : viewTarget == character    ? "player"
                                           : "player's transition camera";
    } else if (players(pending)) {
      shown = "other actor, blending to player";
    }
    float screenFov = 0.f;
    if (ue::O.PCM_CameraCache >= 0 && ue::O.CacheEntry_POV >= 0) {
      screenFov = ue::At<float>(cameraManager, ue::O.PCM_CameraCache + ue::O.CacheEntry_POV + ue::O.POV_FOV);
    }
    Trace(now, LogSituation(shown), povFresh, screenFov);
  }

  // Ease hotkey changes over ~0.1s instead of snapping.
  // Switching between first and third person is a camera cut: take the other FOV at once.
  static bool wasThirdPerson = false;
  const bool thirdPerson = tp::ViewActive();
  const float userFov = UserFov();
  if (St.smoothedFov <= 0.f || thirdPerson != wasThirdPerson) St.smoothedFov = userFov;
  wasThirdPerson = thirdPerson;
  St.smoothedFov += (userFov - St.smoothedFov) * (1.f - std::exp(-dt / 0.05f));

  static long reportedFaults = 0;
  if (g_faults != reportedFaults) {
    reportedFaults = g_faults;
    if (reportedFaults <= 5) LOG("Recovered from a fault in the camera hook (%ld so far)", reportedFaults);
  }
}

void RequestClearLockedFov() { g_clearLockedRequested = true; }

// While the game's first-person rendering is on, the first-person meshes (arms, held items) are
// drawn with a FOV of their own: normally the game's gameplay FOV, whatever your FOV is. The
// game's local player fills it in last (UTownfallLocalPlayer::GetViewPoint), so this runs after
// that. With a viewmodel FOV set, they follow it instead, blended in and out like the main FOV.
// Changes the game makes to its own value (such as the right-click zoom) are kept, scaled around
// its usual value.
void ApplyToView(void* view) {
  St.lastViewmodelOut = 0.f;
  if (ue::O.POV_FirstPersonFOV < 0 || ue::O.POV_FirstPersonBits < 0) return;
  if (!(ue::At<uint8_t>(view, ue::O.POV_FirstPersonBits) & kFirstPersonParams)) return;
  float& fp = ue::At<float>(view, ue::O.POV_FirstPersonFOV);
  const float game = fp;
  if (!Plausible(game)) return;
  St.gameViewmodelFov = game;
  St.lastViewmodelOut = game;
  static float usual = 0.f;  // the game's value outside the zoom
  if (usual <= 0.f || (!St.zooming && St.zoomAlpha <= 0.f)) usual = game;
  const double a = Ease(St.alpha);
  if (cfg::S.viewmodelFov <= 0.f || a <= 0.0) return;
  const double tGame = TanHalf(game);
  const double tTarget = TanHalf(cfg::S.viewmodelFov) * tGame / TanHalf(usual);
  const float out = util::Clamp(static_cast<float>(FromTanHalf(tGame + (tTarget - tGame) * a)), kMinOut, kMaxOut);
  fp = out;
  St.lastViewmodelOut = out;
}

}  // namespace fov
