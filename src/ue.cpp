#include "ue.h"

#include <windows.h>

#include "log.h"

namespace ue {

Offsets O;
Addresses A;

void AddRequests(std::vector<pdb::Request>& reqs) {
  const auto field = [&](const char* type, const char* member, int64_t* out, bool optional = false) {
    reqs.push_back({pdb::Kind::Field, type, member, out, optional});
  };
  const auto symbol = [&](const char* name, int64_t* out, bool optional = false) {
    reqs.push_back({pdb::Kind::Symbol, name, nullptr, out, optional});
  };

  field("APlayerCameraManager", "PCOwner", &O.PCM_PCOwner);
  field("APlayerCameraManager", "LockedFOV", &O.PCM_LockedFOV, true);
  field("APlayerCameraManager", "ViewTarget", &O.PCM_ViewTarget);
  field("APlayerCameraManager", "PendingViewTarget", &O.PCM_PendingViewTarget);
  field("FTViewTarget", "Target", &O.VT_Target);
  field("FTViewTarget", "POV", &O.VT_POV);
  field("FMinimalViewInfo", "FOV", &O.POV_FOV);
  field("FMinimalViewInfo", "DesiredFOV", &O.POV_DesiredFOV, true);
  field("FMinimalViewInfo", "FirstPersonFOV", &O.POV_FirstPersonFOV, true);
  field("AController", "Pawn", &O.Ctrl_Pawn);
  field("ATownfallPlayerCharacter", "CameraControllerComponent", &O.Char_CameraController, true);
  field("ATownfallPlayerCharacter", "CameraFieldOfView", &O.Char_CameraFieldOfView, true);
  field("UTFCameraControllerComponent", "bOverridingFirstPersonCineCameraSettings", &O.Cam_OverridingCine, true);
  field("UTFCameraControllerComponent", "beautyCamTransitionState", &O.Cam_BeautyState, true);
  field("UTFCameraControllerComponent", "ActiveCamera", &O.Cam_ActiveCamera, true);
  field("UTFCameraControllerComponent", "FirstPersonCameraLocal", &O.Cam_FirstPersonCamera, true);
  field("UTFCameraControllerComponent", "HeadCamera", &O.Cam_HeadCamera, true);
  field("UTFCameraControllerComponent", "AnimatedCamera", &O.Cam_AnimatedCamera, true);
  field("UTFCameraControllerComponent", "TransitionCamera", &O.Cam_TransitionCamera, true);
  field("UTFCameraControllerComponent", "OuterCamera", &O.Cam_OuterCamera, true);
  field("ATownfallPlayerCharacter", "FPSCamera", &O.Char_FPSCamera, true);
  field("UTFCameraControllerComponent", "DecoupledTransitionCameraActor", &O.Cam_TransitionActor, true);
  field("APlayerCameraManager", "CameraCachePrivate", &O.PCM_CameraCache, true);
  field("FCameraCacheEntry", "POV", &O.CacheEntry_POV, true);
  field("UTownfallGameInstance", "GameStateStack", &O.GI_GameStateStack);
  field("UObjectBase", "ClassPrivate", &O.Obj_Class);
  field("UObjectBase", "NamePrivate", &O.Obj_Name, true);
  field("UStruct", "SuperStruct", &O.Struct_Super);
  field("UEngine", "GameViewport", &O.Eng_GameViewport);
  field("UEngine", "SmallFont", &O.Eng_SmallFont);
  field("UEngine", "MediumFont", &O.Eng_MediumFont, true);
  field("UEngine", "LargeFont", &O.Eng_LargeFont, true);
  field("UEngine", "SubtitleFont", &O.Eng_SubtitleFont, true);
  field("UFont", "FontCacheType", &O.Font_CacheType, true);
  field("UFont", "LegacyFontSize", &O.Font_LegacyFontSize, true);
  field("UCanvas", "OrgX", &O.Canvas_OrgX, true);
  field("UCanvas", "OrgY", &O.Canvas_OrgY, true);
  field("UCanvas", "ClipX", &O.Canvas_ClipX, true);
  field("UCanvas", "ClipY", &O.Canvas_ClipY, true);
  field("UCanvas", "bCenterX", &O.Canvas_CenterBits, true);
  field("UCanvas", "DrawColor", &O.Canvas_DrawColor);
  field("UCanvas", "SizeX", &O.Canvas_SizeX);
  field("UCanvas", "SizeY", &O.Canvas_SizeY);
  field("UCanvas", "DefaultTexture", &O.Canvas_DefaultTexture, true);
  field("UCanvas", "Canvas", &O.Canvas_Canvas, true);
  field("FCanvas", "bStereoRendering", &O.FCanvas_Stereo, true);
  field("FCanvas", "DPIScale", &O.FCanvas_DPIScale, true);
  field("UGameViewportClient", "World", &O.GVC_World);
  // Experimental third-person view.
  field("FMinimalViewInfo", "Location", &O.POV_Location, true);
  field("FMinimalViewInfo", "Rotation", &O.POV_Rotation, true);
  field("FMinimalViewInfo", "bUseFirstPersonParameters", &O.POV_FirstPersonBits, true);
  field("UPlayer", "PlayerController", &O.Player_PC, true);
  field("APlayerController", "Player", &O.PC_Player, true);
  field("APlayerController", "PlayerCameraManager", &O.PC_CameraManager, true);
  field("AActor", "RootComponent", &O.Actor_Root, true);
  field("USceneComponent", "AttachChildren", &O.Scene_AttachChildren, true);
  field("USceneComponent", "bVisible", &O.Scene_VisibleBits, true);
  field("USceneComponent", "bHiddenInGame", &O.Scene_HiddenBits, true);
  field("UPrimitiveComponent", "bOwnerNoSee", &O.Prim_OwnerNoSeeBits, true);
  field("UPrimitiveComponent", "FirstPersonPrimitiveType", &O.Prim_FirstPersonType, true);
  field("USkinnedMeshComponent", "SkinnedAsset", &O.Skinned_Asset, true);
  field("USkinnedMeshComponent", "LeaderPoseComponent", &O.Skinned_Leader, true);
  field("UActorComponent", "OwnerPrivate", &O.Comp_Owner, true);
  field("UPrimitiveComponent", "bRenderInMainPass", &O.Prim_RenderBits, true);
  field("FHitResult", "Time", &O.Hit_Time, true);
  field("FHitResult", "bBlockingHit", &O.Hit_Bits, true);
  field("USceneComponent", "RelativeRotation", &O.Scene_RelativeRotation, true);
  field("USceneComponent", "ComponentVelocity", &O.Scene_Velocity, true);
  field("USkinnedMeshComponent", "VisibilityBasedAnimTickOption", &O.Skinned_TickOption, true);
  field("USkeletalMeshComponent", "AnimClass", &O.SkelMesh_AnimClass, true);
  field("USkeletalMeshComponent", "AnimScriptInstance", &O.SkelMesh_AnimInstance, true);
  field("UStruct", "ChildProperties", &O.Struct_ChildProperties, true);
  field("FField", "ClassPrivate", &O.Field_Class, true);
  field("FField", "Next", &O.Field_Next, true);
  field("FField", "NamePrivate", &O.Field_Name, true);
  field("FFieldClass", "Name", &O.FieldClass_Name, true);
  field("FProperty", "Offset_Internal", &O.Prop_Offset, true);
  field("FNamePool", "Entries", &O.NamePool_Entries, true);
  field("FNameEntryAllocator", "Blocks", &O.NameAllocator_Blocks, true);

  symbol("GEngine", &A.GEngine);
  symbol("FWeakObjectPtr::Get", &A.WeakObjectPtrGet);
  symbol("ATownfallPlayerCharacter::StaticClass", &A.PlayerCharacterStaticClass);
  symbol("UTownfallBlueprintLibrary::GetTownfallPlayerCameraManager", &A.GetPlayerCameraManager);
  symbol("UTownfallBlueprintLibrary::GetTownfallGameInstance", &A.GetGameInstance);
  symbol("ATownfallPlayerCameraManager::ApplyCameraModifiers", &A.ApplyCameraModifiers);
  symbol("UGameViewportClient::PostRender", &A.PostRender);
  symbol("UCanvas::DrawText", &A.CanvasDrawText);
  symbol("UCanvas::TextSize", &A.CanvasTextSize);
  symbol("UCanvas::K2_DrawTexture", &A.CanvasDrawTexture);
  // Experimental third-person view.
  symbol("UTownfallLocalPlayer::GetViewPoint", &A.LocalPlayerGetViewPoint, true);
  symbol("UActorComponent::MarkRenderStateDirty", &A.MarkRenderStateDirty, true);
  symbol("UPrimitiveComponent::StaticClass", &A.PrimitiveStaticClass, true);
  symbol("USkinnedMeshComponent::StaticClass", &A.SkinnedMeshStaticClass, true);
  symbol("UKismetSystemLibrary::GetDisplayName", &A.GetDisplayName, true);
  symbol("FMemory::Free", &A.MemoryFree, true);
  symbol("UKismetSystemLibrary::SphereTraceSingle", &A.SphereTrace, true);
  symbol("USkinnedMeshComponent::GetNumBones", &A.GetNumBones, true);
  symbol("USkinnedMeshComponent::IsBoneHidden", &A.IsBoneHidden, true);
  symbol("USkinnedMeshComponent::HideBone", &A.HideBone, true);
  symbol("USkinnedMeshComponent::UnHideBone", &A.UnHideBone, true);
  symbol("StaticLoadObject", &A.StaticLoadObject, true);
  symbol("AActor::AddComponentByClass", &A.AddComponentByClass, true);
  symbol("USkeletalMeshComponent::SetSkeletalMesh", &A.SetSkeletalMesh, true);
  symbol("USkinnedMeshComponent::SetLeaderPoseComponent", &A.SetLeaderPoseComponent, true);
  symbol("USceneComponent::AttachToComponent", &A.AttachToComponent, true);
  symbol("USceneComponent::SetHiddenInGame", &A.SetHiddenInGame, true);
  symbol("USkeletalMesh::StaticClass", &A.SkeletalMeshStaticClass, true);
  symbol("USkeletalMeshComponent::StaticClass", &A.SkeletalMeshCompStaticClass, true);
  symbol("UClass::StaticClass", &A.ClassStaticClass, true);
  symbol("USkeletalMeshComponent::SetAnimInstanceClass", &A.SetAnimInstanceClass, true);
  symbol("USceneComponent::SetRelativeRotation", &A.SetRelativeRotation, true);
  symbol("UActorComponent::AddTickPrerequisiteComponent", &A.AddTickPrerequisite, true);
  symbol("UMaterialInterface::StaticClass", &A.MaterialInterfaceStaticClass, true);
  symbol("UMeshComponent::SetMaterial", &A.SetMaterial, true);
  symbol("USceneComponent::DestroyComponent", &A.DestroySceneComponent, true);
  symbol("NamePoolData", &A.NamePool, true);
}

std::string NameToString(const void* name) {
  if (!name || !A.NamePool || O.NamePool_Entries < 0 || O.NameAllocator_Blocks < 0) return "?";
  // FName = { FNameEntryId ComparisonIndex; uint32 Number }. The id picks a block and a 2-byte
  // aligned slot; the entry is a 16-bit header (bit 0: wide, bits 6-15: length) and the characters.
  const uint32_t id = At<uint32_t>(const_cast<void*>(name), 0);
  const uint32_t number = At<uint32_t>(const_cast<void*>(name), 4);
  constexpr uint32_t kMaxBlocks = 8192;
  if ((id >> 16) >= kMaxBlocks) return "?";
  const auto* blocks = reinterpret_cast<const uint8_t* const*>(A.NamePool + O.NamePool_Entries + O.NameAllocator_Blocks);
  const uint8_t* entry = blocks[id >> 16];
  if (!entry) return "?";
  entry += (id & 0xFFFF) * 2;
  const uint16_t header = *reinterpret_cast<const uint16_t*>(entry);
  const uint32_t length = header >> 6;
  std::string out;
  if (header & 1) {
    const auto* chars = reinterpret_cast<const wchar_t*>(entry + 2);
    for (uint32_t i = 0; i < length; ++i) out.push_back(chars[i] < 128 ? static_cast<char>(chars[i]) : '?');
  } else {
    out.assign(reinterpret_cast<const char*>(entry + 2), length);
  }
  if (number) out += "_" + std::to_string(number - 1);
  return out;
}

void* CallWorldGetter(int64_t fn, void* worldContext) {
  using Fn = void*(__fastcall*)(void*);
  return fn && worldContext ? reinterpret_cast<Fn>(fn)(worldContext) : nullptr;
}

void* WeakGet(void* base, int64_t offset) {
  using Fn = void*(__fastcall*)(const void*);
  if (!base || offset < 0 || !A.WeakObjectPtrGet) return nullptr;
  return reinterpret_cast<Fn>(A.WeakObjectPtrGet)(static_cast<char*>(base) + offset);
}

bool IsA(void* object, void* cls) {
  if (!object || !cls) return false;
  void* c = Ptr(object, O.Obj_Class);
  for (int depth = 0; c && depth < 64; ++depth) {
    if (c == cls) return true;
    c = Ptr(c, O.Struct_Super);
  }
  return false;
}

bool HookVirtual(void* object, int64_t original, void* hook) {
  void** vtable = *static_cast<void***>(object);
  // The slot is well inside the vtable (slot 111 for PostRender, 253 for ApplyCameraModifiers
  // in the build this was written against), so the scan finds it before running off the end.
  for (int i = 0; i < 1024; ++i) {
    if (vtable[i] == hook) return true;
    if (reinterpret_cast<int64_t>(vtable[i]) != original) continue;
    DWORD old = 0;
    if (!VirtualProtect(&vtable[i], sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(&vtable[i], hook);
    VirtualProtect(&vtable[i], sizeof(void*), old, &old);
    LOG("Hooked vtable %p slot %d", static_cast<void*>(vtable), i);
    return true;
  }
  return false;
}

}  // namespace ue
