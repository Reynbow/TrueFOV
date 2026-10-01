// Offline self-test: maps the game exe into this process (without running it) and checks that
// symbol resolution, the cache, vtable hooking and the winmm proxy all behave.
//
// Usage: offline_check.exe <path to Townfall-Win64-Shipping.exe> <path to built winmm.dll>

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/log.h"
#include "../src/pdb.h"
#include "../src/states.h"
#include "../src/ue.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
  printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
  if (!ok) ++g_failures;
}

void ExpectOffset(const char* name, int64_t got, int64_t want) {
  char buf[160];
  snprintf(buf, sizeof buf, "%-34s = 0x%llx (expected 0x%llx)", name, static_cast<long long>(got), static_cast<long long>(want));
  Check(got == want, buf);
}

void __fastcall DummyHook() {}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    printf("usage: offline_check <Townfall-Win64-Shipping.exe> <winmm.dll>\n");
    return 2;
  }
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  const std::wstring cache = std::wstring(temp) + L"TownfallFOV_offline_check.cache";
  logx::Open((std::wstring(temp) + L"TownfallFOV_offline_check.log").c_str());
  DeleteFileW(cache.c_str());

  HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!game) {
    printf("couldn't map %ls (error %lu)\n", argv[1], GetLastError());
    return 1;
  }
  const pdb::Module mod = pdb::DescribeModule(game);
  printf("Mapped game at %p, image size 0x%x, timestamp %08X\n", static_cast<void*>(game), mod.imageSize, mod.timeStamp);

  int64_t vtCameraManager = 0;
  std::vector<pdb::Request> reqs;
  ue::AddRequests(reqs);
  states::AddRequests(reqs);
  reqs.push_back({pdb::Kind::Symbol, "ATownfallPlayerCameraManager::`vftable'", nullptr, &vtCameraManager, true});
  int64_t bogus = -1;  // an optional field that doesn't exist: must be cached as "not found"
  reqs.push_back({pdb::Kind::Field, "UCanvas", "DoesNotExistForTest", &bogus, true});

  printf("\nResolving from the PDB:\n");
  std::string error;
  const ULONGLONG t0 = GetTickCount64();
  const bool ok = pdb::Resolve(mod, reqs, cache, error);
  printf("  took %llu ms\n", GetTickCount64() - t0);
  Check(ok, error.empty() ? "all required symbols resolved" : error.c_str());
  if (!ok) return 1;

  // Values found by hand while researching this build (EXE timestamp 4759946D).
  const ue::Offsets& o = ue::O;
  ExpectOffset("APlayerCameraManager::PCOwner", o.PCM_PCOwner, 0x2a8);
  ExpectOffset("APlayerCameraManager::LockedFOV", o.PCM_LockedFOV, 0x2c4);
  ExpectOffset("APlayerCameraManager::ViewTarget", o.PCM_ViewTarget, 0x340);
  ExpectOffset("APlayerCameraManager::PendingViewTarget", o.PCM_PendingViewTarget, 0xc20);
  ExpectOffset("FTViewTarget::POV", o.VT_POV, 0x10);
  ExpectOffset("FMinimalViewInfo::FOV", o.POV_FOV, 0x30);
  ExpectOffset("FMinimalViewInfo::DesiredFOV", o.POV_DesiredFOV, 0x34);
  ExpectOffset("FMinimalViewInfo::FirstPersonFOV", o.POV_FirstPersonFOV, 0x38);
  ExpectOffset("AController::Pawn", o.Ctrl_Pawn, 0x2e8);
  ExpectOffset("ATownfallPlayerCharacter::CameraController", o.Char_CameraController, 0x948);
  ExpectOffset("ATownfallPlayerCharacter::CameraFieldOfView", o.Char_CameraFieldOfView, 0x7d0);
  ExpectOffset("UTFCameraController::bOverridingCine", o.Cam_OverridingCine, 0x804);
  ExpectOffset("UTFCameraController::beautyCamState", o.Cam_BeautyState, 0x678);
  ExpectOffset("UTownfallGameInstance::GameStateStack", o.GI_GameStateStack, 0x298);
  ExpectOffset("UObjectBase::ClassPrivate", o.Obj_Class, 0x10);
  ExpectOffset("UObjectBase::NamePrivate", o.Obj_Name, 0x18);
  ExpectOffset("UStruct::SuperStruct", o.Struct_Super, 0x40);
  ExpectOffset("UEngine::GameViewport", o.Eng_GameViewport, 0xc10);
  ExpectOffset("UEngine::MediumFont", o.Eng_MediumFont, 0x80);
  ExpectOffset("UCanvas::DrawColor", o.Canvas_DrawColor, 0x38);
  ExpectOffset("UCanvas::SizeY", o.Canvas_SizeY, 0x44);
  ExpectOffset("UCanvas::DefaultTexture", o.Canvas_DefaultTexture, 0x70);
  ExpectOffset("UGameViewportClient::World", o.GVC_World, 0x78);
  ExpectOffset("UEngine::LargeFont", o.Eng_LargeFont, 0xa8);
  ExpectOffset("UEngine::SubtitleFont", o.Eng_SubtitleFont, 0xd0);
  ExpectOffset("UFont::FontCacheType", o.Font_CacheType, 0x30);
  ExpectOffset("UFont::LegacyFontSize", o.Font_LegacyFontSize, 0x13c);
  ExpectOffset("UCanvas::OrgX", o.Canvas_OrgX, 0x28);
  ExpectOffset("UCanvas::ClipX", o.Canvas_ClipX, 0x30);
  ExpectOffset("UCanvas::ClipY", o.Canvas_ClipY, 0x34);
  ExpectOffset("UCanvas::bCenterX (bitfield word)", o.Canvas_CenterBits, 0x3c);
  ExpectOffset("UCanvas::Canvas", o.Canvas_Canvas, 0x2e0);
  ExpectOffset("FCanvas::bStereoRendering", o.FCanvas_Stereo, 0xa0);
  ExpectOffset("FCanvas::DPIScale", o.FCanvas_DPIScale, 0xb0);
  ExpectOffset("UTFCameraController::ActiveCamera", o.Cam_ActiveCamera, 0x1c0);
  ExpectOffset("UTFCameraController::FirstPersonCameraLocal", o.Cam_FirstPersonCamera, 0x1a0);
  ExpectOffset("UTFCameraController::HeadCamera", o.Cam_HeadCamera, 0x190);
  ExpectOffset("UTFCameraController::AnimatedCamera", o.Cam_AnimatedCamera, 0x198);
  ExpectOffset("UTFCameraController::TransitionCamera", o.Cam_TransitionCamera, 0x1a8);
  ExpectOffset("UTFCameraController::OuterCamera", o.Cam_OuterCamera, 0x1b0);
  ExpectOffset("ATownfallPlayerCharacter::FPSCamera", o.Char_FPSCamera, 0x900);
  ExpectOffset("UTFCameraController::DecoupledTransitionCameraActor", o.Cam_TransitionActor, 0x3d0);
  ExpectOffset("APlayerCameraManager::CameraCachePrivate", o.PCM_CameraCache, 0x1530);
  ExpectOffset("FCameraCacheEntry::POV", o.CacheEntry_POV, 0x10);
  const int64_t base = static_cast<int64_t>(mod.base);
  // Experimental third-person view.
  ExpectOffset("FMinimalViewInfo::Rotation", o.POV_Rotation, 0x18);
  ExpectOffset("FMinimalViewInfo::bUseFirstPersonParams", o.POV_FirstPersonBits, 0x68);
  ExpectOffset("UPlayer::PlayerController", o.Player_PC, 0x30);
  ExpectOffset("APlayerController::Player", o.PC_Player, 0x348);
  ExpectOffset("APlayerController::PlayerCameraManager", o.PC_CameraManager, 0x360);
  ExpectOffset("AActor::RootComponent", o.Actor_Root, 0x1b8);
  ExpectOffset("USceneComponent::AttachChildren", o.Scene_AttachChildren, 0xe0);
  ExpectOffset("UPrimitiveComponent::bOwnerNoSee", o.Prim_OwnerNoSeeBits, 0x26d);
  ExpectOffset("UPrimitiveComponent::FirstPersonPrimitiveType", o.Prim_FirstPersonType, 0x273);
  ExpectOffset("USkinnedMeshComponent::VisibilityBasedAnimTickOption", o.Skinned_TickOption, 0x7cc);
  ExpectOffset("USkeletalMeshComponent::AnimScriptInstance", o.SkelMesh_AnimInstance, 0x908);
  ExpectOffset("UStruct::ChildProperties", o.Struct_ChildProperties, 0x50);
  ExpectOffset("FField::ClassPrivate", o.Field_Class, 0x8);
  ExpectOffset("FField::Next", o.Field_Next, 0x18);
  ExpectOffset("FField::NamePrivate", o.Field_Name, 0x20);
  ExpectOffset("FFieldClass::Name", o.FieldClass_Name, 0x0);
  ExpectOffset("FProperty::Offset_Internal", o.Prop_Offset, 0x44);
  ExpectOffset("FNameEntryAllocator::Blocks", o.NameAllocator_Blocks, 0x10);
  ExpectOffset("UTownfallLocalPlayer::GetViewPoint (RVA)", ue::A.LocalPlayerGetViewPoint - base, 0x138d2c0);
  ExpectOffset("UActorComponent::AddTickPrerequisiteComponent (RVA)", ue::A.AddTickPrerequisite - base, 0x25218ec);
  ExpectOffset("UMeshComponent::SetMaterial (RVA)", ue::A.SetMaterial - base, 0x146c90c);
  ExpectOffset("USceneComponent::DestroyComponent (RVA)", ue::A.DestroySceneComponent - base, 0x1201410);
  ExpectOffset("UKismetSystemLibrary::GetDisplayName (RVA)", ue::A.GetDisplayName - base, 0x55c0110);
  ExpectOffset("ApplyCameraModifiers (RVA)", ue::A.ApplyCameraModifiers - base, 0x16a2084);
  ExpectOffset("PostRender (RVA)", ue::A.PostRender - base, 0x201ef10);
  ExpectOffset("FWeakObjectPtr::Get (RVA)", ue::A.WeakObjectPtrGet - base, 0x292db14);
  ExpectOffset("GEngine (RVA)", ue::A.GEngine - base, 0x9e0d180);

  const auto stateValue = [](const char* name) {
    for (int i = 0; i < states::kCount; ++i)
      if (strcmp(states::kAll[i].name, name) == 0) return states::kAll[i].value;
    return int64_t{-1};
  };
  ExpectOffset("EGameState::InGame_FPS", stateValue("InGame_FPS"), 4);
  ExpectOffset("EGameState::InGame_Examine", stateValue("InGame_Examine"), 8);
  ExpectOffset("EGameState::InGame_Cinematic", stateValue("InGame_Cinematic"), 14);
  ExpectOffset("EGameState::InGame_PauseMenu", stateValue("InGame_PauseMenu"), 22);
  int resolvedStates = 0;
  for (int i = 0; i < states::kCount; ++i) resolvedStates += states::kAll[i].value >= 0;
  char buf[96];
  snprintf(buf, sizeof buf, "%d of %d game states resolved", resolvedStates, states::kCount);
  Check(resolvedStates == states::kCount, buf);

  printf("\nVtable hooking (on this process's private copy of the image):\n");
  Check(vtCameraManager != 0, "found ATownfallPlayerCameraManager vtable");
  if (vtCameraManager) {
    void* fakeObject = reinterpret_cast<void*>(vtCameraManager);  // an "object" whose vptr is the vtable
    auto** vt = reinterpret_cast<void**>(vtCameraManager);
    Check(vt[253] == reinterpret_cast<void*>(ue::A.ApplyCameraModifiers), "slot 253 holds ApplyCameraModifiers (relocated)");
    Check(ue::HookVirtual(&fakeObject, ue::A.ApplyCameraModifiers, reinterpret_cast<void*>(&DummyHook)), "HookVirtual found the slot");
    Check(vt[253] == reinterpret_cast<void*>(&DummyHook), "slot 253 now points at the hook");
    Check(ue::HookVirtual(&fakeObject, ue::A.ApplyCameraModifiers, reinterpret_cast<void*>(&DummyHook)), "second HookVirtual is a no-op");
  }

  printf("\nSymbol cache:\n");
  ue::Offsets before = ue::O;
  ue::Addresses beforeA = ue::A;
  ue::O = ue::Offsets{};
  ue::A = ue::Addresses{};
  std::vector<pdb::Request> again;
  ue::AddRequests(again);
  states::AddRequests(again);
  int64_t bogusAgain = 12345;
  again.push_back({pdb::Kind::Field, "UCanvas", "DoesNotExistForTest", &bogusAgain, true});
  const ULONGLONG t1 = GetTickCount64();
  Check(pdb::Resolve(mod, again, cache, error), "resolved again from the cache");
  const ULONGLONG cachedMs = GetTickCount64() - t1;
  printf("  took %llu ms\n", cachedMs);
  Check(bogus == -1 && bogusAgain == 12345, "a missing optional field stays unresolved");
  Check(cachedMs < 500, "the cache answered the missing field too (no PDB re-read)");
  {
    bool found = false;
    FILE* f = nullptr;
    char line[1024];
    if (_wfopen_s(&f, cache.c_str(), L"r") == 0 && f) {
      while (fgets(line, sizeof line, f)) found |= strncmp(line, "?F UCanvas::DoesNotExistForTest", 31) == 0;
      fclose(f);
    }
    Check(found, "cache records it as a '?' line (skipped by older builds)");
  }
  Check(memcmp(&before, &ue::O, sizeof before) == 0, "cached offsets match");
  Check(memcmp(&beforeA, &ue::A, sizeof beforeA) == 0, "cached addresses match");

  // A cache written by an older mod version lacks newer (optional) keys: it must be re-read from the
  // PDB rather than accepted with those offsets left unresolved.
  {
    std::vector<std::string> lines;
    FILE* f = nullptr;
    char line[1024];
    if (_wfopen_s(&f, cache.c_str(), L"r") == 0 && f) {
      while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "F UFont::LegacyFontSize=", 24) != 0) lines.push_back(line);
      }
      fclose(f);
    }
    if (_wfopen_s(&f, cache.c_str(), L"w") == 0 && f) {
      for (const std::string& l : lines) fputs(l.c_str(), f);
      fclose(f);
    }
    ue::O = ue::Offsets{};
    std::vector<pdb::Request> third;
    ue::AddRequests(third);
    states::AddRequests(third);
    Check(pdb::Resolve(mod, third, cache, error), "resolved with an outdated cache");
    Check(ue::O.Font_LegacyFontSize == 0x13c, "outdated cache triggered a fresh PDB read");
  }

  printf("\nwinmm proxy:\n");
  HMODULE proxy = LoadLibraryW(argv[2]);
  Check(proxy != nullptr, "proxy DLL loads");
  if (proxy) {
    using TimeGetTime = DWORD(WINAPI*)();
    auto fn = reinterpret_cast<TimeGetTime>(GetProcAddress(proxy, "timeGetTime"));
    Check(fn != nullptr, "timeGetTime export resolves");
    HMODULE real = GetModuleHandleW(L"C:\\Windows\\System32\\winmm.dll");
    Check(real != nullptr && real != proxy, "forwarder loaded the real System32 winmm.dll");
    if (fn) {
      const DWORD a = fn();
      Sleep(20);
      const DWORD b = fn();
      Check(b - a >= 15 && b - a < 1000, "timeGetTime works through the proxy");
    }
    Check(GetProcAddress(proxy, "PlaySoundW") != nullptr, "PlaySoundW export resolves");
  }

  printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL CHECKS PASSED", g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
