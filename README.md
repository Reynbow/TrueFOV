# TRUE FOV - Field of View and 3rd Person for Silent Hill: Townfall

A mod for Silent Hill: Townfall: your own field of view for gameplay only (cutscenes and inspected items keep the game's), a separate FOV for your arms and held items, and an experimental third-person camera, all set live in a menu in the game.

Download and install instructions are on [Nexus Mods](https://www.nexusmods.com/silenthilltownfall/mods/55). This repository is the full source.

## Features

- Gameplay only: cutscenes, cinematics, examining items and scripted interaction cameras are never changed.
- Smooth transitions: customise the blend time to be instant or over a set timespan.
- In-game menu (F7) that works with the mouse or keyboard.
- Zoom FOV: set your own FOV for the right-click zoom.
- Viewmodel FOV: set the FOV of your arms and held items separately.
- Third-person camera (experimental, F8): an over-the-shoulder view with the full body and head of whoever you're playing, its own FOV and an adjustable camera position. Interactions and cutscenes switch back to first person automatically.
- Per-situation control: choose whether your FOV also applies while hiding, in combat QTEs, healing, on ladders, during radio calls, in the quick inventory, examining items or using the map.
- Hotkeys: Page Up / Page Down change the FOV at any time (with a small popup), End toggles the mod for a quick comparison, F8 toggles third person.
- Update-friendly: reads the game's own debug symbols instead of hard-coded addresses, so it adapts to most game patches.

## Requirements (to play)

None. It's a standalone `winmm.dll`: no ReShade, UE4SS or other loader needed.

## Building

Requires Windows and the Visual Studio 2022 Build Tools (C++ workload). Run `build.bat`; it builds `build\winmm.dll` and `build\offline_check.exe`. To install your build, put `winmm.dll` in `Townfall\Binaries\Win64\` in the game folder, next to `Townfall-Win64-Shipping.exe`.

`offline_check.exe <path to Townfall-Win64-Shipping.exe> build\winmm.dll` maps the game executable without running it and checks the symbol lookups, the symbol cache, the vtable hooking and the winmm forwarding against the installed game. `tools/gen_proxy_exports.py` regenerates `src/proxy_exports.h` from the system `winmm.dll`.

## How it works

The game loads `winmm.dll` from its own folder first. This one forwards every real winmm function to `C:\Windows\System32\winmm.dll` and starts the mod inside the game process.

- **Symbols:** nothing is hard-coded. Function addresses, struct offsets and enum values are read from `Townfall-Win64-Shipping.pdb`, which ships with the game, and cached per game build in `TownfallFOV.cache`. After a game update the mod re-reads the PDB once (a few seconds, in the background).
- **Game states:** the game keeps a stack of game states (`InGame_FPS`, `InGame_Cutscene`, `InGame_Examine`, ...), read every frame. The pause menu and tutorial cards are looked through, so pausing doesn't change the background camera.
- **The FOV:** the player camera manager's `ApplyCameraModifiers` runs once per view target every frame; the mod rescales the FOV only when that view target is the player character, or the transition camera the camera controller stands in for it while moving the view (after closing the map, in some interactions). Cutscene cameras are separate view targets, so they're never touched, and blends between them and the player interpolate naturally. Interactions that borrow a cinematic lens or use the close-up "beauty" camera are left alone too. Changes blend in and out over the configurable time, in tan(FOV/2) space so they're proportional on screen. With **Keep the game's FOV effects** on, your FOV scales around the game's own gameplay FOV (`CameraFieldOfView`), so FOV effects the game plays are kept.
- **Zoom:** the right-click zoom is a Blueprint animation that eases the game's FOV about 30° below its gameplay FOV (like `FInterpTo` at speed 15). The mod detects it from the FOV itself, so it also works on a controller, and eases towards the Zoom FOV at the same speed.
- **Viewmodel FOV:** while the game's first-person rendering is on (`UFirstPersonRenderingController` switches it per game state), the first-person meshes are drawn with a FOV of their own, normally the game's gameplay FOV. The mod sets `FMinimalViewInfo::FirstPersonFOV` in `UTownfallLocalPlayer::GetViewPoint` after the game fills it in, blended like the main FOV, and keeps any change the game makes to it (such as the zoom) scaled around its usual value.
- **Third person, the view:** the same `GetViewPoint` hook moves only the view the frame is rendered from. The camera manager keeps the first-person view, which the game uses for interaction traces, aiming and the audio listener. `bUseFirstPersonParameters` is cleared so first-person meshes draw as ordinary meshes. Collision is a `SphereTraceSingle` (radius 12, camera channel) from the eyes to the wanted camera position; the camera snaps in to the hit and eases back out, like a spring arm.
- **Third person, the bodies:** the character has a first-person body (`Mesh1P`, `bOnlyOwnerSee`) and a full body for the shadow (`CharacterMesh0`, `bOwnerNoSee`). The mod flips those flags, keeps the hidden first-person body animating (the game drives things like the zoom through its animations), and turns the full body back by the character's change of yaw so looking around doesn't spin it; above 20 cm/s it turns towards the movement direction. Every change is put back when third person ends. It applies only in free movement (`InGame_FPS`, the character's own camera); interactions and cutscenes show first person.
- **Third person, the head:** the full body has no head. `kHeads` in `thirdperson.cpp` maps each playable character's body mesh to the head parts the game's own character setups (the hero puppets in `/Game/Developers/chris/HeroPoses`) use. The mod loads them (`StaticLoadObject`), adds them to the character (`AddComponentByClass`) and attaches the face to the body. The face runs that character's `ABP_Metahuman_HeadAttachment_*`, which places the face's spine, neck, head and shoulder bones on the body's each frame with per-character offsets, once its `ParentSkeletalMesh` variable is set (as the game's `SetParentMesh` does; the mod sets it through reflection). Grooms follow the face bone by bone, or run their own animation Blueprint when they have bones of their own (hair with simulated strands). The head is rebuilt when the game gives the body another character's mesh.
- **The menu:** drawn with the engine's own `UCanvas` from `UGameViewportClient::PostRender` (no D3D12 hooking), with text rasterized at the exact size needed so it stays sharp at any resolution and Windows display scaling. Keyboard and mouse input come from the game window's messages; keys the mod uses, and the mouse while the menu is open, aren't passed on to the game.

There is no network code; the mod writes only its own settings, log and symbol cache in the game's `Binaries\Win64` folder.

## Antivirus false positives

Some antivirus tools may flag `winmm.dll` because it is an unsigned proxy DLL that hooks into the game. This is a false positive. The full source is here, so you can check it or build the DLL yourself with `build.bat`.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.
