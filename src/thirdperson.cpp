#include "thirdperson.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "fov.h"
#include "log.h"
#include "states.h"
#include "ue.h"
#include "util.h"

namespace tp {
namespace {

using ue::A;
using ue::At;
using ue::O;
using ue::Ptr;

struct FString {
  wchar_t* data;
  int32_t num;
  int32_t max;
};
struct FVector {
  double x, y, z;
};
struct FRotator {
  double pitch, yaw, roll;
};
struct PtrArray {
  void** data;
  int32_t num;
  int32_t max;
};
struct AttachRules {  // FAttachmentTransformRules
  uint8_t location, rotation, scale;
  bool weldSimulatedBodies;
};
constexpr uint8_t kSnapToTarget = 2;  // EAttachmentRule::SnapToTarget

using GetViewPointFn = void(__fastcall*)(void* self, void* viewInfo);
using ComponentFn = void(__fastcall*)(void* component);
using StaticClassFn = void*(__fastcall*)();
using FreeFn = void(__fastcall*)(void* p);
using DisplayNameFn = FString*(__fastcall*)(FString* out, const void* object);
using NumBonesFn = int32_t(__fastcall*)(const void* component);
using IsBoneHiddenFn = bool(__fastcall*)(const void* component, int32_t bone);
using HideBoneFn = void(__fastcall*)(void* component, int32_t bone, int32_t physBodyOp);
using UnHideBoneFn = void(__fastcall*)(void* component, int32_t bone);
using SphereTraceFn = bool(__fastcall*)(const void* worldContext, FVector start, FVector end, float radius, int32_t channel,
                                        bool traceComplex, const PtrArray& actorsToIgnore, int32_t drawDebug, void* outHit,
                                        bool ignoreSelf, ue::FLinearColor traceColor, ue::FLinearColor hitColor, float drawTime);
using LoadObjectFn = void*(__fastcall*)(void* cls, void* outer, const wchar_t* name, const wchar_t* filename,
                                        uint32_t loadFlags, void* sandbox, bool allowReconciliation, const void* instancing);
// TSubclassOf is passed by reference (it isn't trivially copyable).
using AddComponentFn = void*(__fastcall*)(void* actor, void* const* componentClass, bool manualAttachment,
                                          const void* relativeTransform, bool deferredFinish);
using SetMeshFn = void(__fastcall*)(void* component, void* mesh, bool reinitPose);
using SetLeaderFn = void(__fastcall*)(void* follower, void* leader, bool forceUpdate, bool followerTicksPose);
using AttachFn = bool(__fastcall*)(void* component, void* parent, const AttachRules& rules, uint64_t socketName);
using SetHiddenFn = void(__fastcall*)(void* component, bool hidden, bool propagateToChildren, bool setChildrenState);
using SetAnimClassFn = void(__fastcall*)(void* component, void* animClass);
using AddPrerequisiteFn = void(__fastcall*)(void* component, void* prerequisite);
using SetMaterialFn = void(__fastcall*)(void* component, int32_t element, void* material);
using DestroyFn = void(__fastcall*)(void* component, bool promoteChildren);
using SetRotationFn = void(__fastcall*)(void* component, FRotator rotation, bool sweep, void* sweepHit, int32_t teleport);

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr uint8_t kOwnerNoSee = 1, kOnlyOwnerSee = 2;  // UPrimitiveComponent::bOwnerNoSee's byte
constexpr uint8_t kRenderInMainPass = 1 << 4;          // UPrimitiveComponent::bRenderInMainPass's byte
constexpr uint8_t kFirstPersonView = 2;                // FMinimalViewInfo::bUseFirstPersonParameters's byte
constexpr int32_t kCameraTraceChannel = 1;             // ETraceTypeQuery::TraceTypeQuery2 = ECC_Camera
constexpr float kProbeRadius = 12.f;                   // like a spring arm's probe
constexpr double kEaseOutPerSecond = 2.5;              // back to full distance after a wall, per second

// ---- helpers --------------------------------------------------------------------------------

std::string NameOf(const void* object) {
  if (!object || !A.GetDisplayName) return "?";
  FString s{};
  reinterpret_cast<DisplayNameFn>(A.GetDisplayName)(&s, object);
  std::string out;
  for (int32_t i = 0; s.data && i < s.num && s.data[i] && i < 200; ++i) {
    out.push_back(s.data[i] < 128 ? static_cast<char>(s.data[i]) : '?');
  }
  if (s.data && A.MemoryFree) reinterpret_cast<FreeFn>(A.MemoryFree)(s.data);
  return out;
}

// An object's name (its FName, as 8 bytes): unique for each spawned actor. Remembered with an
// actor's address, because after a level load a new character can land at the old one's address.
uint64_t ObjectName(void* object) { return object && O.Obj_Name >= 0 ? At<uint64_t>(object, O.Obj_Name) : 0; }

void Collect(void* component, std::vector<void*>& out, int depth) {
  if (!component || depth > 16 || out.size() >= 256) return;
  out.push_back(component);
  const PtrArray& children = At<PtrArray>(component, O.Scene_AttachChildren);
  if (!children.data || children.num < 0 || children.num > 256) return;
  for (int32_t i = 0; i < children.num; ++i) Collect(children.data[i], out, depth + 1);
}

void MarkDirty(void* component) {
  if (A.MarkRenderStateDirty) reinterpret_cast<ComponentFn>(A.MarkRenderStateDirty)(component);
}

bool CanDoBones() { return A.GetNumBones && A.IsBoneHidden && A.HideBone && A.UnHideBone; }

bool CanBorrowHead() {
  return A.StaticLoadObject && A.AddComponentByClass && A.SetSkeletalMesh && A.SetLeaderPoseComponent &&
         A.AttachToComponent && A.SetHiddenInGame && A.SkeletalMeshStaticClass && A.SkeletalMeshCompStaticClass;
}

// ---- the borrowed head --------------------------------------------------------------------------
// The player's bodies have no head: the face and hair are separate MetaHuman meshes that gameplay
// never loads. For the full character model they're loaded and attached to its body. The face runs
// the game's own head-attachment animation blueprint: each frame it reads the body's spine, neck,
// head and shoulder bones and places the MetaHuman's on them, with that character's own offsets so
// the neck and shoulders stay under the clothes. The grooms follow the face. Without that blueprint
// everything follows the body's skeleton by bone name (leader pose), which fits the head but leaves
// the MetaHuman shoulders poking out of the clothes.
//
// Which head goes with which body is taken from the game's own character setups (the hero puppets
// in /Game/Developers/chris/HeroPoses). The player Blueprint (BP_Bill) swaps the body meshes when
// the game switches between the playable characters, so the head is rebuilt when the body changes.

struct HeadPart {
  const wchar_t* mesh;
  // Its own animation blueprint class: for the face, the head-attachment one. Without one, a part
  // follows the face bone by bone (leader pose).
  const wchar_t* anim;
  const wchar_t* material;  // replaces material 0, as the game's setup does
};
struct HeadSet {
  const char* bodyPrefix;  // the body mesh asset names it's for (the first match wins)
  HeadPart parts[6];       // the face first
};

#define TP_ASSET(dir, name) dir name L"." name
#define TP_CLASS(dir, name) dir name L"." name L"_C"
#define TP_HEAD_ANIM(name) TP_CLASS(L"/Game/Townfall/Animation/Gameplay/Shared/Blueprints/", name)
#define TP_SIMON_GROOM(name) {TP_ASSET(L"/Game/MetaHumans/Simon_Head_5_6_Optimised/Face/Groom/", name)}
const HeadSet kHeads[] = {
    {"SK_Simon_SideStep",  // the "flash sideways" Simon has a face of his own
     {{TP_ASSET(L"/Game/MetaHumans/Simon_Head_side_step_5_6/Face/", L"SKM_Simon_Head_FaceMesh"),
       TP_HEAD_ANIM(L"ABP_Metahuman_HeadAttachment_Simon")},
      TP_SIMON_GROOM(L"SK_Simon_Head_Hair_Optimised"), TP_SIMON_GROOM(L"SK_Simon_Head_Beard_Optimised"),
      TP_SIMON_GROOM(L"SK_Simon_Head_Eyebrows_Optimised"), TP_SIMON_GROOM(L"SK_Simon_Head_Eyelashes_Optimised")}},
    {"SK_Simon",
     {{TP_ASSET(L"/Game/MetaHumans/Simon_Head_5_6_Optimised/Face/", L"SKM_Simon_Head_FaceMesh"),
       TP_HEAD_ANIM(L"ABP_Metahuman_HeadAttachment_Simon")},
      TP_SIMON_GROOM(L"SK_Simon_Head_Hair_Optimised"), TP_SIMON_GROOM(L"SK_Simon_Head_Beard_Optimised"),
      TP_SIMON_GROOM(L"SK_Simon_Head_Eyebrows_Optimised"), TP_SIMON_GROOM(L"SK_Simon_Head_Eyelashes_Optimised")}},
    {"SK_Zoe",
     {{TP_ASSET(L"/Game/MetaHumans/Zoe_Head_5_6_Optimised/Face/", L"SKM_Zoe_Head_FaceMesh"),
       TP_HEAD_ANIM(L"ABP_Metahuman_HeadAttachment_Zoe")},
      // Her hair has strands of its own: its animation copies the face's pose and simulates them.
      {TP_ASSET(L"/Game/Townfall/Characters/Zoe/Groom/HairCards/", L"SK_Zoe_Hair"),
       TP_CLASS(L"/Game/Townfall/Characters/Zoe/Groom/HairCards/", L"Zoe_Hair_ABP")},
      {TP_ASSET(L"/Game/Townfall/Characters/Zoe/Groom/HairCards/Optimised/", L"SK_Zoe_Groom_Eyebrows_optimised")},
      {TP_ASSET(L"/Game/Townfall/Characters/Zoe/Groom/HairCards/Optimised/", L"SK_Zoe_Groom_Eyelashes_optimised"),
       nullptr, TP_ASSET(L"/Game/Townfall/Characters/Zoe/Materials/", L"MI_Zoe_Eyelashes")}}},
    {"SK_Richard",
     {{TP_ASSET(L"/Game/MetaHumans/Richard_Head_5_6_Optimised/Face/", L"SKM_Richard_Head_FaceMesh"),
       TP_HEAD_ANIM(L"ABP_Metahuman_HeadAttachment_Richard")},
      {TP_ASSET(L"/Game/Townfall/Characters/Richard/Grooms/Hair_Cards/Optimized/", L"SK_Richard_Head_Beard_optimised")},
      {TP_ASSET(L"/Game/Townfall/Characters/Richard/Grooms/Hair_Cards/Optimized/", L"SK_Richard_Head_Eyebrows_optimised")},
      {TP_ASSET(L"/Game/Townfall/Characters/Richard/Grooms/Hair_Cards/Optimized/",
                L"SK_Richard_Head_Eyelashes_optimised")}}},
};
#undef TP_SIMON_GROOM
#undef TP_HEAD_ANIM
#undef TP_CLASS
#undef TP_ASSET

struct Head {
  void* character = nullptr;  // the character the parts were added to (they go away with it)
  uint64_t characterName = 0;
  void* body = nullptr;       // the full character model they follow
  void* bodyAsset = nullptr;  // its mesh when they were built (the game swaps it between characters)
  std::vector<void*> parts;
  bool pending = false;       // to be built in the next camera update
  bool settling = false;      // built in the last one: shown once its animation has run
  bool shown = false;
};
Head g_head;

// Is |character| (a live one) the one the head was made for?
bool HeadIsFor(void* character) {
  return character && character == g_head.character && ObjectName(character) == g_head.characterName;
}

void SetHeadShown(bool shown) {
  g_head.shown = shown;
  for (void* part : g_head.parts) reinterpret_cast<SetHiddenFn>(A.SetHiddenInGame)(part, !shown, false, false);
}

// Removes the parts (only while their character is still there).
void DestroyHead() {
  for (auto it = g_head.parts.rbegin(); it != g_head.parts.rend(); ++it) {
    if (A.DestroySceneComponent) {
      reinterpret_cast<DestroyFn>(A.DestroySceneComponent)(*it, false);
    } else {
      reinterpret_cast<SetHiddenFn>(A.SetHiddenInGame)(*it, true, false, false);
    }
  }
  g_head.parts.clear();
  g_head.shown = g_head.settling = false;
}

bool CanReflect() {
  return O.SkelMesh_AnimInstance >= 0 && O.Struct_ChildProperties >= 0 && O.Field_Class >= 0 && O.Field_Next >= 0 &&
         O.Field_Name >= 0 && O.FieldClass_Name >= 0 && O.Prop_Offset >= 0 && A.NamePool;
}

bool IsBlueprintClass(void* cls) {
  const std::string name = NameOf(cls);
  return name.size() > 2 && name.compare(name.size() - 2, 2, "_C") == 0;
}

std::string FieldName(void* field) { return ue::NameToString(static_cast<char*>(field) + O.Field_Name); }

std::string FieldType(void* field) {
  void* fieldClass = Ptr(field, O.Field_Class);
  return fieldClass ? ue::NameToString(static_cast<char*>(fieldClass) + O.FieldClass_Name) : "?";
}

// Sets an object variable of the component's animation blueprint, found by name among the
// variables of its Blueprint classes. If it isn't there, logs what is.
bool SetAnimObject(void* component, const char* variable, void* value) {
  void* instance = Ptr(component, O.SkelMesh_AnimInstance);
  if (!instance || !CanReflect()) return false;
  std::string seen;
  for (void* cls = Ptr(instance, O.Obj_Class); cls && IsBlueprintClass(cls); cls = Ptr(cls, O.Struct_Super)) {
    int fields = 0;
    for (void* f = Ptr(cls, O.Struct_ChildProperties); f && fields < 1000; f = Ptr(f, O.Field_Next), ++fields) {
      const std::string name = FieldName(f);
      if (name == variable && FieldType(f) == "ObjectProperty") {
        At<void*>(instance, At<int32_t>(f, O.Prop_Offset)) = value;
        return true;
      }
      if (fields < 40) seen += " " + name + ":" + FieldType(f);
    }
    seen += " |";
  }
  LOG("Third person: no %s in the animation %s; it has:%s", variable, NameOf(Ptr(instance, O.Obj_Class)).c_str(),
      seen.c_str());
  return false;
}

// A part with an animation of its own: it animates after |source|, so it follows this frame's pose
// rather than the last one, and keeps animating while hidden, so it's in place the moment it's shown.
void AnimateAfter(void* part, void* source) {
  if (A.AddTickPrerequisite) reinterpret_cast<AddPrerequisiteFn>(A.AddTickPrerequisite)(part, source);
  if (O.Skinned_TickOption >= 0) At<uint8_t>(part, O.Skinned_TickOption) = 0;  // AlwaysTickPoseAndRefreshBones
}

void BuildHead() {
  const std::string asset = NameOf(g_head.bodyAsset);
  const HeadSet* set = nullptr;
  for (const HeadSet& h : kHeads) {
    if (asset.compare(0, std::strlen(h.bodyPrefix), h.bodyPrefix) == 0) {
      set = &h;
      break;
    }
  }
  if (!set) {
    LOG("Third person: no head known for the body %s", asset.c_str());
    return;
  }
  const auto load = [](void* cls, const wchar_t* path) -> void* {
    if (!cls || !path) return nullptr;
    void* object = reinterpret_cast<LoadObjectFn>(A.StaticLoadObject)(cls, nullptr, path, nullptr, 0, nullptr, true, nullptr);
    if (!object) LOG("Third person: couldn't load %ls", path);
    return object;
  };
  const auto staticClass = [](int64_t fn) { return fn ? reinterpret_cast<StaticClassFn>(fn)() : nullptr; };
  void* meshClass = staticClass(A.SkeletalMeshStaticClass);
  void* componentClass = staticClass(A.SkeletalMeshCompStaticClass);
  void* animClass = A.SetAnimInstanceClass ? staticClass(A.ClassStaticClass) : nullptr;
  void* materialClass = A.SetMaterial ? staticClass(A.MaterialInterfaceStaticClass) : nullptr;
  alignas(16) static const double kIdentity[12] = {0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};  // FTransform (doubles)
  const AttachRules snap{kSnapToTarget, kSnapToTarget, kSnapToTarget, false};
  const double start = util::Now();
  void* face = nullptr;  // the face, once its head-attachment animation follows the body
  for (const HeadPart& hp : set->parts) {
    if (!hp.mesh) break;
    void* mesh = load(meshClass, hp.mesh);
    if (!mesh) continue;
    void* part = reinterpret_cast<AddComponentFn>(A.AddComponentByClass)(g_head.character, &componentClass, true, kIdentity, false);
    if (!part) continue;
    reinterpret_cast<SetMeshFn>(A.SetSkeletalMesh)(part, mesh, true);
    if (void* material = hp.material ? load(materialClass, hp.material) : nullptr) {
      reinterpret_cast<SetMaterialFn>(A.SetMaterial)(part, 0, material);
    }
    void* anim = hp.anim ? load(animClass, hp.anim) : nullptr;
    if (&hp == &set->parts[0]) {  // the face
      reinterpret_cast<AttachFn>(A.AttachToComponent)(part, g_head.body, snap, 0);
      if (anim) reinterpret_cast<SetAnimClassFn>(A.SetAnimInstanceClass)(part, anim);
      // What the game's own head setups do (SetParentMesh in BP_MetaHuman_HeadAttachment and the
      // hero puppets). Without it the animation has no bones to read and leaves the head at the
      // body's feet.
      if (anim && SetAnimObject(part, "ParentSkeletalMesh", g_head.body)) {
        face = part;
        AnimateAfter(part, g_head.body);
      } else {
        if (anim) reinterpret_cast<SetAnimClassFn>(A.SetAnimInstanceClass)(part, nullptr);
        reinterpret_cast<SetLeaderFn>(A.SetLeaderPoseComponent)(part, g_head.body, true, false);
      }
    } else {
      void* parent = face ? face : g_head.body;
      reinterpret_cast<AttachFn>(A.AttachToComponent)(part, parent, snap, 0);
      if (anim && face) {
        // Its animation copies the face's pose from its attach parent (and moves its own bones).
        reinterpret_cast<SetAnimClassFn>(A.SetAnimInstanceClass)(part, anim);
        AnimateAfter(part, face);
      } else {
        reinterpret_cast<SetLeaderFn>(A.SetLeaderPoseComponent)(part, parent, true, false);
      }
    }
    g_head.parts.push_back(part);
  }
  LOG("Third person: attached %zu head parts to %s with %s (%.2fs)", g_head.parts.size(), asset.c_str(),
      face ? "the head-attachment animation" : "leader pose", util::Now() - start);
}

void SafeBuildHead() {
  __try {
    BuildHead();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    LOG("Third person: fault while attaching the head (0x%08lX)", GetExceptionCode());
  }
}

// ---- which meshes are shown -------------------------------------------------------------------
// The character carries two bodies: the first-person one (no head, only its owner sees it) and
// the full character model (everyone but its owner sees it; it casts the player's shadow). In
// third person the full model is shown instead of the first-person body.

struct FlagChange {
  void* component;
  int64_t offset;
  uint8_t mask;
  uint8_t before;  // the masked bits before the change
};
struct BoneChange {
  void* component;
  int32_t bone;
};

void* g_character = nullptr;  // the character the changes below were made on
uint64_t g_characterName = 0;
bool g_fullBody = false;      // which body the changes show
std::vector<FlagChange> g_flags;
std::vector<BoneChange> g_bones;
std::vector<void*> g_ignore;  // actors attached to the player: the camera trace ignores them

void SetFlags(void* component, int64_t offset, uint8_t mask, uint8_t value) {
  uint8_t& byte = At<uint8_t>(component, offset);
  if ((byte & mask) == (value & mask)) return;
  g_flags.push_back({component, offset, mask, static_cast<uint8_t>(byte & mask)});
  byte = static_cast<uint8_t>((byte & ~mask) | (value & mask));
  MarkDirty(component);
}

// ---- which way the full model faces ----------------------------------------------------------
// The character turns with the camera (the first-person setup). The full model is turned back by
// the difference, so looking around doesn't spin it; while moving it turns to face the camera
// direction again. Only the drawn mesh turns: movement, aim and collision are the game's as always.

constexpr double kMovingSpeed = 20.0;  // cm/s: slower counts as standing still
constexpr double kTurnPerSecond = 8.0;  // how quickly the model turns to face the way you move

void* g_fullModel = nullptr;  // the full character model while it's shown
struct BodyYaw {
  bool valid = false;
  double body = 0.0;         // where the model faces (world yaw)
  double base = 0.0;         // the model's own relative yaw, without the turn
  double lastWritten = 0.0;  // relative yaw we set last frame
  double lastTime = 0.0;
};
BodyYaw g_yaw;

double Wrap180(double deg) {
  deg = std::fmod(deg + 180.0, 360.0);
  return (deg < 0.0 ? deg + 360.0 : deg) - 180.0;
}

void SetModelYaw(double relativeYaw) {
  FRotator rotation = At<FRotator>(g_fullModel, O.Scene_RelativeRotation);
  rotation.yaw = relativeYaw;
  reinterpret_cast<SetRotationFn>(A.SetRelativeRotation)(g_fullModel, rotation, false, nullptr, 0);
  g_yaw.lastWritten = At<FRotator>(g_fullModel, O.Scene_RelativeRotation).yaw;
}

// From the camera update, after the character has moved this frame.
void UpdateBodyYaw() {
  if (!g_fullModel || !g_character || !A.SetRelativeRotation || O.Scene_RelativeRotation < 0 || O.Scene_Velocity < 0) {
    return;
  }
  const double now = util::Now();
  if (g_yaw.valid && now - g_yaw.lastTime < 0.001) return;  // another camera update in the same frame
  const double dt = g_yaw.valid ? util::Clamp(now - g_yaw.lastTime, 0.0, 0.1) : 0.0;
  g_yaw.lastTime = now;
  void* root = Ptr(g_character, O.Actor_Root);
  const double actorYaw = At<FRotator>(root, O.Scene_RelativeRotation).yaw;
  const FVector& velocity = At<FVector>(root, O.Scene_Velocity);
  const double relative = At<FRotator>(g_fullModel, O.Scene_RelativeRotation).yaw;
  if (!g_yaw.valid) {
    g_yaw.valid = true;
    g_yaw.body = actorYaw;
    g_yaw.base = relative;
  } else if (std::fabs(relative - g_yaw.lastWritten) > 0.01) {
    g_yaw.base = relative;  // the game placed the model itself this frame
  }
  if (std::hypot(velocity.x, velocity.y) > kMovingSpeed) {
    g_yaw.body += Wrap180(actorYaw - g_yaw.body) * (std::min)(1.0, dt * kTurnPerSecond);
  }
  SetModelYaw(g_yaw.base + Wrap180(g_yaw.body - actorYaw));
}

void RestoreBodyYaw() {
  if (g_yaw.valid && g_fullModel && A.SetRelativeRotation) SetModelYaw(g_yaw.base);
  g_yaw = {};
}

void LogComponent(void* c, bool primitive, void* skinnedClass) {
  const std::string name = NameOf(c), cls = NameOf(Ptr(c, O.Obj_Class));
  if (!primitive) {
    LOG("  %s [%s]", name.c_str(), cls.c_str());
    return;
  }
  const uint8_t owner = At<uint8_t>(c, O.Prim_OwnerNoSeeBits);
  std::string extra;
  if (skinnedClass && ue::IsA(c, skinnedClass)) {
    if (O.Skinned_Asset >= 0) extra += " mesh " + NameOf(Ptr(c, O.Skinned_Asset));
    if (O.Skinned_Leader >= 0) {
      if (void* leader = ue::WeakGet(c, O.Skinned_Leader)) extra += " | follows " + NameOf(leader);
    }
    if (CanDoBones()) {
      const int32_t bones = reinterpret_cast<NumBonesFn>(A.GetNumBones)(c);
      int hidden = 0;
      for (int32_t b = 0; b < bones && b < 2048; ++b) hidden += reinterpret_cast<IsBoneHiddenFn>(A.IsBoneHidden)(c, b);
      extra += " | " + std::to_string(bones) + " bones, " + std::to_string(hidden) + " hidden";
    }
    if (O.Skinned_TickOption >= 0) extra += " | anim tick option " + std::to_string(At<uint8_t>(c, O.Skinned_TickOption));
    if (O.SkelMesh_AnimClass >= 0) {
      if (void* anim = Ptr(c, O.SkelMesh_AnimClass)) extra += " | anim " + NameOf(anim);
    }
  }
  LOG("  %s [%s]%s | first-person type %d | owner-no-see %d | only-owner-see %d | main pass %d | visible %d | hidden %d",
      name.c_str(), cls.c_str(), extra.c_str(), At<uint8_t>(c, O.Prim_FirstPersonType), owner & 1, (owner >> 1) & 1,
      O.Prim_RenderBits >= 0 ? (At<uint8_t>(c, O.Prim_RenderBits) >> 4) & 1 : -1,
      (At<uint8_t>(c, O.Scene_VisibleBits) >> 5) & 1, (At<uint8_t>(c, O.Scene_HiddenBits) >> 3) & 1);
}

void ShowBody(void* character, bool fullBody) {
  std::vector<void*> components;
  Collect(Ptr(character, O.Actor_Root), components, 0);
  void* primitiveClass = reinterpret_cast<StaticClassFn>(A.PrimitiveStaticClass)();
  void* skinnedClass = A.SkinnedMeshStaticClass ? reinterpret_cast<StaticClassFn>(A.SkinnedMeshStaticClass)() : nullptr;
  static void* loggedFor = nullptr;
  const bool log = loggedFor != character;
  loggedFor = character;
  if (log) LOG("Third person: %zu components attached to %s", components.size(), NameOf(character).c_str());
  if (g_head.character && !HeadIsFor(character)) g_head = {};  // the old parts went with the old character

  g_ignore.clear();
  g_ignore.push_back(character);
  void* fullModel = nullptr;
  for (void* c : components) {
    void* owner = O.Comp_Owner >= 0 ? Ptr(c, O.Comp_Owner) : character;
    if (owner && std::find(g_ignore.begin(), g_ignore.end(), owner) == g_ignore.end()) g_ignore.push_back(owner);
    const bool primitive = ue::IsA(c, primitiveClass);
    if (log) LogComponent(c, primitive, skinnedClass);
    // Props on other actors (radio, torch) keep their setup; the first-person body mode draws the
    // meshes as the game does, just from behind. Our own head parts aren't the game's.
    if (!primitive || owner != character || !fullBody) continue;
    if (std::find(g_head.parts.begin(), g_head.parts.end(), c) != g_head.parts.end()) continue;
    const uint8_t bits = At<uint8_t>(c, O.Prim_OwnerNoSeeBits) & (kOwnerNoSee | kOnlyOwnerSee);
    if (bits == kOnlyOwnerSee) {
      SetFlags(c, O.Prim_OwnerNoSeeBits, kOwnerNoSee, kOwnerNoSee);  // first-person-only: hide
      // ...but keep it animating: a mesh that isn't drawn normally stops, and the game drives
      // things like the right-click zoom from the first-person animations.
      if (skinnedClass && ue::IsA(c, skinnedClass) && O.Skinned_TickOption >= 0) {
        SetFlags(c, O.Skinned_TickOption, 0xFF, 0);  // EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
      }
    } else if (bits == kOwnerNoSee) {
      SetFlags(c, O.Prim_OwnerNoSeeBits, kOwnerNoSee, 0);  // everyone-but-you: show
      if (O.Prim_RenderBits >= 0) SetFlags(c, O.Prim_RenderBits, kRenderInMainPass, kRenderInMainPass);
      if (skinnedClass && ue::IsA(c, skinnedClass)) {
        if (!fullModel) fullModel = c;
        if (CanDoBones()) {
          const int32_t bones = reinterpret_cast<NumBonesFn>(A.GetNumBones)(c);
          for (int32_t b = 0; b < bones && b < 2048; ++b) {
            if (!reinterpret_cast<IsBoneHiddenFn>(A.IsBoneHidden)(c, b)) continue;
            reinterpret_cast<UnHideBoneFn>(A.UnHideBone)(c, b);
            g_bones.push_back({c, b});
          }
        }
      }
    }
  }
  // The head for the full model: built on the next camera update, or shown again.
  if (fullBody && fullModel && CanBorrowHead()) {
    void* asset = O.Skinned_Asset >= 0 ? Ptr(fullModel, O.Skinned_Asset) : nullptr;
    if (!HeadIsFor(character) || g_head.body != fullModel || g_head.bodyAsset != asset) {
      if (HeadIsFor(character)) DestroyHead();  // another character's head on this character
      g_head = {};
      g_head.character = character;
      g_head.characterName = ObjectName(character);
      g_head.body = fullModel;
      g_head.bodyAsset = asset;
      g_head.pending = true;
    } else if (!g_head.pending && !g_head.settling) {
      SetHeadShown(true);
    }
  }
  g_character = character;
  g_characterName = ObjectName(character);
  g_fullBody = fullBody;
  g_fullModel = fullBody ? fullModel : nullptr;
}

// Is |character| (a live one, or null) the one the changes were made on?
bool IsTracked(void* character) {
  return character && character == g_character && ObjectName(character) == g_characterName;
}

// Puts everything back. After a level change the old components are gone: nothing to restore.
void RestoreBody(bool characterStillThere) {
  if (characterStillThere) {
    RestoreBodyYaw();
    for (auto it = g_bones.rbegin(); it != g_bones.rend(); ++it) {
      reinterpret_cast<HideBoneFn>(A.HideBone)(it->component, it->bone, 0);
    }
    for (auto it = g_flags.rbegin(); it != g_flags.rend(); ++it) {
      uint8_t& byte = At<uint8_t>(it->component, it->offset);
      byte = static_cast<uint8_t>((byte & ~it->mask) | it->before);
      MarkDirty(it->component);
    }
    if (HeadIsFor(g_character)) SetHeadShown(false);
  }
  g_bones.clear();
  g_flags.clear();
  g_ignore.clear();
  g_character = nullptr;
  g_characterName = 0;
  g_fullModel = nullptr;
  g_yaw = {};
}

// ---- when third person applies --------------------------------------------------------------

int g_gameplayState = -1;  // states::kAll index of InGame_FPS
bool g_active = false;     // third person is on and the player is moving freely

// Free movement on the player's own camera. Interactions have game states of their own, and use
// the game's transition camera, a borrowed lens or a close-up camera: those are first person.
bool FreeMovement(void* character, void* manager) {
  if (g_gameplayState < 0) g_gameplayState = states::IndexOfName("InGame_FPS");
  const fov::Status& st = fov::St;
  if (st.effectiveState != g_gameplayState || st.lensOverride || st.closeUpState != 0) return false;
  if (std::strcmp(st.activeCamera, "character") != 0 && std::strcmp(st.activeCamera, "unknown") != 0) return false;
  void* target = Ptr(manager, O.PCM_ViewTarget + O.VT_Target);
  void* pending = Ptr(manager, O.PCM_PendingViewTarget + O.VT_Target);
  return target == character && (!pending || pending == character);
}

// ---- the view -------------------------------------------------------------------------------

bool g_hooked = false;
void** g_checkedVtable = nullptr;
double g_fraction = 1.0;  // how far along the offset the camera is (a wall pulls it in)
double g_lastViewTime = 0.0;

// Is the player's own camera the one being rendered?
void* PlayerOnScreen(void* localPlayer) {
  void* pc = Ptr(localPlayer, O.Player_PC);
  void* manager = pc ? Ptr(pc, O.PC_CameraManager) : nullptr;
  void* pawn = pc ? Ptr(pc, O.Ctrl_Pawn) : nullptr;
  if (!manager || !pawn) return nullptr;
  void* target = Ptr(manager, O.PCM_ViewTarget + O.VT_Target);
  void* pending = Ptr(manager, O.PCM_PendingViewTarget + O.VT_Target);
  const auto players = [&](void* actor) { return actor == pawn || (actor && actor == fov::TransitionActor(pawn)); };
  return players(target) && (!pending || players(pending)) ? pawn : nullptr;
}

// Fraction of the way from |from| to |to| the camera can go before it touches something.
double FreeFraction(void* character, const FVector& from, const FVector& to) {
  if (!cfg::S.tpCollision || !A.SphereTrace || O.Hit_Time < 0 || O.Hit_Bits < 0) return 1.0;
  alignas(16) unsigned char hit[0x200] = {};  // FHitResult is 0x100 bytes in this build
  const PtrArray ignore{g_ignore.data(), static_cast<int32_t>(g_ignore.size()), static_cast<int32_t>(g_ignore.size())};
  const bool blocked = reinterpret_cast<SphereTraceFn>(A.SphereTrace)(
      character, from, to, kProbeRadius, kCameraTraceChannel, false, ignore, 0, hit, true, {1, 0, 0, 1}, {0, 1, 0, 1}, 0.f);
  const uint8_t bits = At<uint8_t>(hit, O.Hit_Bits);
  if (!blocked || !(bits & 1)) return 1.0;
  if (bits & 2) return g_fraction;  // started inside something: hold where it is
  return util::Clamp(static_cast<double>(At<float>(hit, O.Hit_Time)), 0.0, 1.0);
}

void MoveView(void* character, void* view) {
  FVector& loc = At<FVector>(view, O.POV_Location);
  const FRotator& rot = At<FRotator>(view, O.POV_Rotation);
  const cfg::Settings& s = cfg::S;
  const double pitch = rot.pitch * kDegToRad, yaw = rot.yaw * kDegToRad;
  const FVector fwd{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
  const FVector right{-std::sin(yaw), std::cos(yaw), 0.0};
  const FVector eye = loc;
  const FVector want{eye.x - fwd.x * s.tpDistance + right.x * s.tpSide, eye.y - fwd.y * s.tpDistance + right.y * s.tpSide,
                     eye.z - fwd.z * s.tpDistance + s.tpHeight};

  // Like a spring arm: straight in when something's in the way, eased back out when it's clear.
  const double free = FreeFraction(character, eye, want);
  const double now = util::Now();
  const double dt = util::Clamp(now - g_lastViewTime, 0.0, 0.1);
  g_lastViewTime = now;
  g_fraction = free < g_fraction ? free : (std::min)(free, g_fraction + dt * kEaseOutPerSecond);

  loc = {eye.x + (want.x - eye.x) * g_fraction, eye.y + (want.y - eye.y) * g_fraction, eye.z + (want.z - eye.z) * g_fraction};
  // Draw the bodies as ordinary meshes, not with the first-person projection.
  At<uint8_t>(view, O.POV_FirstPersonBits) &= ~kFirstPersonView;
}

void SafeMoveView(void* localPlayer, void* view) {
  __try {
    if (void* character = PlayerOnScreen(localPlayer)) MoveView(character, view);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void SafeApplyViewmodel(void* view) {
  __try {
    fov::ApplyToView(view);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

// The view each frame is rendered from, once the game's local player has filled it in.
void __fastcall HookGetViewPoint(void* self, void* view) {
  reinterpret_cast<GetViewPointFn>(A.LocalPlayerGetViewPoint)(self, view);
  SafeApplyViewmodel(view);
  // Also while the full model is still swapped in (switched off this frame): the swap is undone
  // at the end of the frame, and a first-person frame of it would show the inside of the head.
  if (g_active || g_character) SafeMoveView(self, view);
}

// Hooks the local player's view point once it exists. The viewmodel FOV needs it too, so it
// doesn't wait for everything third person needs.
void EnsureViewHook(void* cameraManager) {
  if (!cameraManager || !A.LocalPlayerGetViewPoint || O.PCM_PCOwner < 0 || O.PC_Player < 0) return;
  void* pc = Ptr(cameraManager, O.PCM_PCOwner);
  void* localPlayer = pc ? Ptr(pc, O.PC_Player) : nullptr;
  if (!localPlayer || *static_cast<void***>(localPlayer) == g_checkedVtable) return;
  g_checkedVtable = *static_cast<void***>(localPlayer);
  g_hooked = ue::HookVirtual(localPlayer, A.LocalPlayerGetViewPoint, reinterpret_cast<void*>(&HookGetViewPoint));
  if (!g_hooked) LOG("UTownfallLocalPlayer::GetViewPoint isn't in the local player's vtable");
}

}  // namespace

bool Available() {
  return A.LocalPlayerGetViewPoint && A.MarkRenderStateDirty && A.PrimitiveStaticClass && A.GetDisplayName &&
         O.POV_Location >= 0 && O.POV_Rotation >= 0 && O.POV_FirstPersonBits >= 0 && O.Player_PC >= 0 &&
         O.PC_Player >= 0 && O.PC_CameraManager >= 0 && O.Actor_Root >= 0 && O.Scene_AttachChildren >= 0 &&
         O.Scene_VisibleBits >= 0 && O.Scene_HiddenBits >= 0 && O.Prim_OwnerNoSeeBits >= 0 &&
         O.Prim_FirstPersonType >= 0;
}

bool IsOn() { return cfg::S.tpEnabled; }

bool ViewActive() { return g_active; }

void Toggle() {
  if (!Available()) return;
  cfg::S.tpEnabled = !cfg::S.tpEnabled;
  cfg::MarkDirty();
  // Swap the meshes now, before this frame renders (OnFrame would only get to it after).
  void* character = fov::St.character;
  void* manager = fov::St.cameraManager;
  if (!cfg::S.tpEnabled) {
    g_active = false;
    if (g_character) RestoreBody(IsTracked(character));
  } else if (character && manager && !g_character && FreeMovement(character, manager)) {
    g_active = true;
    ShowBody(character, cfg::S.tpFullBody);
    g_fraction = 1.0;
  }
}

void OnFrame() {
  void* character = fov::St.character;
  void* manager = fov::St.cameraManager;
  EnsureViewHook(manager);
  if (!Available()) return;
  // Third person while moving freely; interactions switch to first person until they're done.
  const bool wasActive = g_active;
  g_active = cfg::S.tpEnabled && character && manager && FreeMovement(character, manager);
  if (g_active != wasActive && cfg::S.tpEnabled) LOG("Third person: %s", g_active ? "back to third person" : "first person for now");
  // Keep the meshes in step with that, the chosen body and the current character.
  if (g_character && (!g_active || !IsTracked(character) || cfg::S.tpFullBody != g_fullBody)) {
    RestoreBody(IsTracked(character));
  }
  if (g_active && !g_character) {
    ShowBody(character, cfg::S.tpFullBody);
    g_fraction = 1.0;
  }
}

namespace {

// The game switches playable characters by giving the same body component another mesh (Simon to
// Zoe, say): the head goes with the old one.
void CheckHeadBody() {
  if (!g_head.body || g_head.pending || O.Skinned_Asset < 0) return;
  void* asset = Ptr(g_head.body, O.Skinned_Asset);
  if (asset == g_head.bodyAsset) return;
  LOG("Third person: the body is now %s; replacing the head", NameOf(asset).c_str());
  DestroyHead();
  if (IsTracked(g_head.character) && g_fullBody) {  // on screen: rebuilt right away
    g_head.bodyAsset = asset;
    g_head.pending = true;
  } else {  // set up again when it's next shown
    g_head = {};
  }
}

void CameraUpdate(void* cameraManager) {
  if (g_active) UpdateBodyYaw();
  if (!g_head.character) return;
  // Only while the head's character is the player: then its parts are still there. After a level
  // load a queued head is for a character that's gone; with no player (loading), wait.
  void* pc = cameraManager ? Ptr(cameraManager, O.PCM_PCOwner) : nullptr;
  void* pawn = pc ? Ptr(pc, O.Ctrl_Pawn) : nullptr;
  if (!pawn || !HeadIsFor(pawn)) {
    if (pawn && g_head.pending) g_head = {};
    return;
  }
  CheckHeadBody();
  if (g_head.settling) {  // its animation has run with the body since it was built
    g_head.settling = false;
    SetHeadShown(IsTracked(g_head.character) && g_fullBody);
  }
  if (!g_head.pending) return;
  g_head.pending = false;
  SafeBuildHead();
  // The new parts haven't been animated with the body yet (the face would show at its feet):
  // hidden until the next update, by when they have.
  SetHeadShown(false);
  g_head.settling = true;
}

}  // namespace

void OnCameraUpdate(void* cameraManager) {
  __try {
    CameraUpdate(cameraManager);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

}  // namespace tp
