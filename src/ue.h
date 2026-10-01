#pragma once

// Minimal access to Unreal Engine objects. Offsets and addresses are filled in from the PDB
// (see AddRequests); -1 / 0 means "not found" for optional entries.

#include <cstdint>
#include <string>
#include <vector>

#include "pdb.h"

namespace ue {

struct Offsets {
  int64_t PCM_PCOwner = -1;             // APlayerCameraManager::PCOwner
  int64_t PCM_LockedFOV = -1;           // APlayerCameraManager::LockedFOV (set by the 'fov' console command)
  int64_t PCM_ViewTarget = -1;          // APlayerCameraManager::ViewTarget
  int64_t PCM_PendingViewTarget = -1;   // APlayerCameraManager::PendingViewTarget
  int64_t VT_Target = -1;               // FTViewTarget::Target
  int64_t VT_POV = -1;                  // FTViewTarget::POV
  int64_t POV_FOV = -1;                 // FMinimalViewInfo::FOV
  int64_t POV_DesiredFOV = -1;          // FMinimalViewInfo::DesiredFOV
  int64_t POV_FirstPersonFOV = -1;      // FMinimalViewInfo::FirstPersonFOV (first-person meshes' FOV)
  int64_t Ctrl_Pawn = -1;               // AController::Pawn
  int64_t Char_CameraController = -1;   // ATownfallPlayerCharacter::CameraControllerComponent (weak)
  int64_t Char_CameraFieldOfView = -1;  // ATownfallPlayerCharacter::CameraFieldOfView
  int64_t Cam_OverridingCine = -1;      // UTFCameraControllerComponent::bOverridingFirstPersonCineCameraSettings
  int64_t Cam_BeautyState = -1;         // UTFCameraControllerComponent::beautyCamTransitionState
  // UTFCameraControllerComponent cameras, used only to name the active one in the log.
  int64_t Cam_ActiveCamera = -1;
  int64_t Cam_FirstPersonCamera = -1;   // FirstPersonCameraLocal
  int64_t Cam_HeadCamera = -1;
  int64_t Cam_AnimatedCamera = -1;
  int64_t Cam_TransitionCamera = -1;
  int64_t Cam_OuterCamera = -1;
  int64_t Char_FPSCamera = -1;          // ATownfallPlayerCharacter::FPSCamera (log only)
  // UTFCameraControllerComponent::DecoupledTransitionCameraActor: the camera actor the controller
  // moves between two of the player's views (closing the map, some interactions).
  int64_t Cam_TransitionActor = -1;
  int64_t PCM_CameraCache = -1;         // APlayerCameraManager::CameraCachePrivate (log only)
  int64_t CacheEntry_POV = -1;          // FCameraCacheEntry::POV (log only)
  int64_t GI_GameStateStack = -1;       // UTownfallGameInstance::GameStateStack
  int64_t Obj_Class = -1;               // UObjectBase::ClassPrivate
  int64_t Obj_Name = -1;                // UObjectBase::NamePrivate (FName; unique per actor instance)
  int64_t Struct_Super = -1;            // UStruct::SuperStruct
  int64_t Eng_GameViewport = -1;        // UEngine::GameViewport
  int64_t Eng_SmallFont = -1;           // UEngine::SmallFont
  int64_t Eng_MediumFont = -1;          // UEngine::MediumFont
  int64_t Eng_LargeFont = -1;           // UEngine::LargeFont
  int64_t Eng_SubtitleFont = -1;        // UEngine::SubtitleFont
  int64_t Font_CacheType = -1;          // UFont::FontCacheType (uint8: 0 offline bitmap, 1 runtime)
  int64_t Font_LegacyFontSize = -1;     // UFont::LegacyFontSize (int32 points; runtime fonts rasterize at this)
  int64_t Canvas_OrgX = -1;             // UCanvas::OrgX
  int64_t Canvas_OrgY = -1;             // UCanvas::OrgY
  int64_t Canvas_ClipX = -1;            // UCanvas::ClipX
  int64_t Canvas_ClipY = -1;            // UCanvas::ClipY
  int64_t Canvas_CenterBits = -1;       // UCanvas::bCenterX (bit 0) / bCenterY (bit 1)
  int64_t Canvas_DrawColor = -1;        // UCanvas::DrawColor
  int64_t Canvas_SizeX = -1;            // UCanvas::SizeX
  int64_t Canvas_SizeY = -1;            // UCanvas::SizeY
  int64_t Canvas_DefaultTexture = -1;   // UCanvas::DefaultTexture
  int64_t Canvas_Canvas = -1;           // UCanvas::Canvas (FCanvas*)
  int64_t FCanvas_Stereo = -1;          // FCanvas::bStereoRendering
  int64_t FCanvas_DPIScale = -1;        // FCanvas::DPIScale
  int64_t GVC_World = -1;               // UGameViewportClient::World
  // Experimental third-person view (thirdperson.cpp).
  int64_t POV_Location = -1;            // FMinimalViewInfo::Location (FVector, doubles)
  int64_t POV_Rotation = -1;            // FMinimalViewInfo::Rotation (FRotator, doubles)
  int64_t POV_FirstPersonBits = -1;     // FMinimalViewInfo::bUseFirstPersonParameters (bit 1 of this byte)
  int64_t Player_PC = -1;               // UPlayer::PlayerController
  int64_t PC_Player = -1;               // APlayerController::Player (the local player)
  int64_t PC_CameraManager = -1;        // APlayerController::PlayerCameraManager
  int64_t Actor_Root = -1;              // AActor::RootComponent
  int64_t Scene_AttachChildren = -1;    // USceneComponent::AttachChildren
  int64_t Scene_VisibleBits = -1;       // USceneComponent::bVisible (bit 5)
  int64_t Scene_HiddenBits = -1;        // USceneComponent::bHiddenInGame (bit 3)
  int64_t Prim_OwnerNoSeeBits = -1;     // UPrimitiveComponent::bOwnerNoSee (bit 0) / bOnlyOwnerSee (bit 1)
  int64_t Prim_FirstPersonType = -1;    // UPrimitiveComponent::FirstPersonPrimitiveType (uint8)
  int64_t Skinned_Asset = -1;           // USkinnedMeshComponent::SkinnedAsset
  int64_t Skinned_Leader = -1;          // USkinnedMeshComponent::LeaderPoseComponent (weak)
  int64_t Comp_Owner = -1;              // UActorComponent::OwnerPrivate
  int64_t Prim_RenderBits = -1;         // UPrimitiveComponent::bRenderInMainPass (bit 4)
  int64_t Hit_Time = -1;                // FHitResult::Time (float, 0..1 along the trace)
  int64_t Hit_Bits = -1;                // FHitResult::bBlockingHit (bit 0) / bStartPenetrating (bit 1)
  int64_t Scene_RelativeRotation = -1;  // USceneComponent::RelativeRotation (FRotator, doubles)
  int64_t Scene_Velocity = -1;          // USceneComponent::ComponentVelocity (FVector, doubles)
  int64_t Skinned_TickOption = -1;      // USkinnedMeshComponent::VisibilityBasedAnimTickOption (uint8)
  int64_t SkelMesh_AnimClass = -1;      // USkeletalMeshComponent::AnimClass (log only)
  int64_t SkelMesh_AnimInstance = -1;   // USkeletalMeshComponent::AnimScriptInstance
  // Reflection, to set a variable of an animation blueprint.
  int64_t Struct_ChildProperties = -1;  // UStruct::ChildProperties (FField*)
  int64_t Field_Class = -1;             // FField::ClassPrivate (FFieldClass*)
  int64_t Field_Next = -1;              // FField::Next
  int64_t Field_Name = -1;              // FField::NamePrivate (FName)
  int64_t FieldClass_Name = -1;         // FFieldClass::Name (FName)
  int64_t Prop_Offset = -1;             // FProperty::Offset_Internal (int32)
  int64_t NamePool_Entries = -1;        // FNamePool::Entries
  int64_t NameAllocator_Blocks = -1;    // FNameEntryAllocator::Blocks (uint8* [])
};

struct Addresses {
  int64_t GEngine = 0;                     // UEngine* GEngine (address of the global)
  int64_t WeakObjectPtrGet = 0;            // UObject* FWeakObjectPtr::Get() const
  int64_t PlayerCharacterStaticClass = 0;  // UClass* ATownfallPlayerCharacter::StaticClass()
  int64_t GetPlayerCameraManager = 0;      // UTownfallBlueprintLibrary::GetTownfallPlayerCameraManager(UObject*)
  int64_t GetGameInstance = 0;            // UTownfallBlueprintLibrary::GetTownfallGameInstance(UObject*)
  int64_t ApplyCameraModifiers = 0;        // ATownfallPlayerCameraManager::ApplyCameraModifiers(float, FMinimalViewInfo&)
  int64_t PostRender = 0;                  // UGameViewportClient::PostRender(UCanvas*)
  int64_t CanvasDrawText = 0;              // UCanvas::DrawText
  int64_t CanvasTextSize = 0;              // UCanvas::TextSize
  int64_t CanvasDrawTexture = 0;           // UCanvas::K2_DrawTexture
  // Experimental third-person view (all optional).
  int64_t LocalPlayerGetViewPoint = 0;     // UTownfallLocalPlayer::GetViewPoint(FMinimalViewInfo&) const
  int64_t MarkRenderStateDirty = 0;        // UActorComponent::MarkRenderStateDirty()
  int64_t PrimitiveStaticClass = 0;        // UPrimitiveComponent::StaticClass()
  int64_t SkinnedMeshStaticClass = 0;      // USkinnedMeshComponent::StaticClass()
  int64_t GetDisplayName = 0;              // FString UKismetSystemLibrary::GetDisplayName(const UObject*)
  int64_t MemoryFree = 0;                  // FMemory::Free(void*)
  int64_t SphereTrace = 0;                 // UKismetSystemLibrary::SphereTraceSingle
  int64_t GetNumBones = 0;                 // int32 USkinnedMeshComponent::GetNumBones() const
  int64_t IsBoneHidden = 0;                // bool USkinnedMeshComponent::IsBoneHidden(int32) const
  int64_t HideBone = 0;                    // USkinnedMeshComponent::HideBone(int32, EPhysBodyOp)
  int64_t UnHideBone = 0;                  // USkinnedMeshComponent::UnHideBone(int32)
  // Borrowing the cutscene head for the full character model.
  int64_t StaticLoadObject = 0;            // UObject* StaticLoadObject(UClass*, UObject*, const TCHAR*, ...)
  int64_t AddComponentByClass = 0;         // AActor::AddComponentByClass(const TSubclassOf&, bool, const FTransform&, bool)
  int64_t SetSkeletalMesh = 0;             // USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh*, bool)
  int64_t SetLeaderPoseComponent = 0;      // USkinnedMeshComponent::SetLeaderPoseComponent(USkinnedMeshComponent*, bool, bool)
  int64_t AttachToComponent = 0;           // USceneComponent::AttachToComponent(USceneComponent*, const FAttachmentTransformRules&, FName)
  int64_t SetHiddenInGame = 0;             // USceneComponent::SetHiddenInGame(bool, bool)
  int64_t SkeletalMeshStaticClass = 0;     // USkeletalMesh::StaticClass()
  int64_t SkeletalMeshCompStaticClass = 0; // USkeletalMeshComponent::StaticClass()
  int64_t ClassStaticClass = 0;            // UClass::StaticClass()
  int64_t SetAnimInstanceClass = 0;        // USkeletalMeshComponent::SetAnimInstanceClass(UClass*)
  int64_t SetRelativeRotation = 0;         // USceneComponent::SetRelativeRotation(FRotator, bool, FHitResult*, ETeleportType)
  int64_t AddTickPrerequisite = 0;         // UActorComponent::AddTickPrerequisiteComponent(UActorComponent*)
  int64_t MaterialInterfaceStaticClass = 0;  // UMaterialInterface::StaticClass()
  int64_t SetMaterial = 0;                 // UMeshComponent::SetMaterial(int32, UMaterialInterface*)
  int64_t DestroySceneComponent = 0;       // USceneComponent::DestroyComponent(bool)
  int64_t NamePool = 0;                    // FNamePool NamePoolData (the global name table)
};

extern Offsets O;
extern Addresses A;

void AddRequests(std::vector<pdb::Request>& reqs);

// Engine value types passed to engine functions; layouts must match the engine's.
struct FVector2D {
  double X, Y;
};
struct FLinearColor {
  float R, G, B, A;
};
struct FColor {
  uint8_t B, G, R, A;  // FColor is stored BGRA on little-endian platforms
};
struct FStringView {
  const wchar_t* Data;
  int32_t Size;
};

template <class T>
inline T& At(void* base, int64_t offset) {
  return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}
inline void* Ptr(void* base, int64_t offset) { return At<void*>(base, offset); }

// Calls a UTownfallBlueprintLibrary getter that takes a world-context object.
void* CallWorldGetter(int64_t fn, void* worldContext);
// Resolves a TWeakObjectPtr member at |offset| inside |base|.
void* WeakGet(void* base, int64_t offset);
bool IsA(void* object, void* cls);
// The text of an FName, read from the engine's name table ("?" if it isn't available).
std::string NameToString(const void* name);

// Points the vtable slot of |object| that currently holds |original| at |hook|. Returns true if
// the hook is (now) installed.
bool HookVirtual(void* object, int64_t original, void* hook);

}  // namespace ue
