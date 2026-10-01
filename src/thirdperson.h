#pragma once

// Third-person view (experimental; F8, the menu's Third person tab and the ini's [ThirdPerson]).
// The game keeps running its first-person camera (body placement, interaction traces, audio); only
// the rendered viewpoint moves behind the player. By default the full character model is shown
// instead of the first-person body, with Simon's MetaHuman head borrowed from the game's assets.

namespace tp {

bool Available();  // the symbols it needs were found
void Toggle();
bool IsOn();
bool ViewActive();  // the third-person view is in use right now (on, and not in an interaction)
// Once per frame on the game thread, after fov::OnFrame. Also hooks the view each frame is rendered
// from, which the viewmodel FOV (fov::ApplyToView) uses whether or not third person is available.
void OnFrame();
// From the player camera update (game thread, during the world tick): loads and attaches the
// borrowed head when one is due. Loading there keeps it out of the middle of drawing a frame.
void OnCameraUpdate(void* cameraManager);

}  // namespace tp
