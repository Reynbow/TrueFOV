#include "canvas.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <utility>

#include "log.h"

namespace canvas {
namespace {

using ue::A;
using ue::At;
using ue::FColor;
using ue::FLinearColor;
using ue::FStringView;
using ue::FVector2D;
using ue::O;
using ue::Ptr;

using DrawTextFn = float(__fastcall*)(void* canvas, void* font, FStringView text, float x, float y, float xScale,
                                      float yScale, const void* renderInfo);
using TextSizeFn = void(__fastcall*)(void* canvas, void* font, FStringView text, float* xl, float* yl, float xScale,
                                     float yScale);
using DrawTextureFn = void(__fastcall*)(void* canvas, void* texture, FVector2D pos, FVector2D size, FVector2D uvPos,
                                        FVector2D uvSize, FLinearColor color, int blendMode, float rotation,
                                        FVector2D pivot);

constexpr int kBlendTranslucent = 2;  // EBlendMode::BLEND_Translucent
constexpr uint8_t kRuntimeFontCache = 1;
constexpr float kRoleLineDp[] = {30.f, 24.f, 19.f, 40.f, 20.f};
static_assert(sizeof(kRoleLineDp) / sizeof(kRoleLineDp[0]) == static_cast<int>(Role::Count), "one size per role");
constexpr float kMinLinePx = 14.f;

FStringView View(const wchar_t* s) { return {s, static_cast<int32_t>(wcslen(s))}; }

// The pending LegacyFontSize write, so EmergencyRestore can undo it after a fault mid-draw.
int32_t* g_sizeSlot = nullptr;
int32_t g_sizeOld = 0;

class SizeScope {
 public:
  SizeScope(void* font, int32_t size) {
    if (!font || size <= 0 || O.Font_LegacyFontSize < 0) return;
    slot_ = &At<int32_t>(font, O.Font_LegacyFontSize);
    old_ = *slot_;
    *slot_ = size;
    g_sizeSlot = slot_;
    g_sizeOld = old_;
  }
  ~SizeScope() {
    if (!slot_) return;
    *slot_ = old_;
    g_sizeSlot = nullptr;
  }
  SizeScope(const SizeScope&) = delete;
  SizeScope& operator=(const SizeScope&) = delete;

 private:
  int32_t* slot_ = nullptr;
  int32_t old_ = 0;
};

// UCanvas state other HUD code may have left behind. DrawText honours origin, clip (it word-wraps at
// ClipX) and centering while K2_DrawTexture doesn't, so neutralize them while we draw.
struct SavedCanvas {
  void* canvas = nullptr;
  float orgX = 0, orgY = 0, clipX = 0, clipY = 0;
  uint32_t centerBits = 0;
};
SavedCanvas g_saved;

void SaveAndNeutralize(void* canvas) {
  g_saved = {};
  g_saved.canvas = canvas;
  if (O.Canvas_OrgX >= 0) g_saved.orgX = std::exchange(At<float>(canvas, O.Canvas_OrgX), 0.f);
  if (O.Canvas_OrgY >= 0) g_saved.orgY = std::exchange(At<float>(canvas, O.Canvas_OrgY), 0.f);
  if (O.Canvas_ClipX >= 0) g_saved.clipX = std::exchange(At<float>(canvas, O.Canvas_ClipX), 1.0e6f);
  if (O.Canvas_ClipY >= 0) g_saved.clipY = std::exchange(At<float>(canvas, O.Canvas_ClipY), 1.0e6f);
  if (O.Canvas_CenterBits >= 0) {
    uint32_t& bits = At<uint32_t>(canvas, O.Canvas_CenterBits);
    g_saved.centerBits = bits;
    bits &= ~3u;  // bCenterX, bCenterY
  }
}

void RestoreCanvas() {
  void* canvas = g_saved.canvas;
  if (!canvas) return;
  if (O.Canvas_OrgX >= 0) At<float>(canvas, O.Canvas_OrgX) = g_saved.orgX;
  if (O.Canvas_OrgY >= 0) At<float>(canvas, O.Canvas_OrgY) = g_saved.orgY;
  if (O.Canvas_ClipX >= 0) At<float>(canvas, O.Canvas_ClipX) = g_saved.clipX;
  if (O.Canvas_ClipY >= 0) At<float>(canvas, O.Canvas_ClipY) = g_saved.clipY;
  if (O.Canvas_CenterBits >= 0) At<uint32_t>(canvas, O.Canvas_CenterBits) = g_saved.centerBits;
  g_saved = {};
}

// Line height ("Ag" measures the font's full line height) at |size| points; 0 keeps the font's own size.
float MeasureLine(void* canvas, void* font, int32_t size) {
  SizeScope scope(font, size);
  float w = 0.f, h = 0.f;
  reinterpret_cast<TextSizeFn>(A.CanvasTextSize)(canvas, font, View(L"Ag"), &w, &h, 1.f, 1.f);
  return h;
}

// Prefers an engine font that is rasterized at runtime (resizable); otherwise the first one found.
void* PickFont(void* engine, bool& runtime, const char*& slotName) {
  const struct {
    int64_t offset;
    const char* name;
  } slots[] = {{O.Eng_MediumFont, "MediumFont"},
               {O.Eng_LargeFont, "LargeFont"},
               {O.Eng_SubtitleFont, "SubtitleFont"},
               {O.Eng_SmallFont, "SmallFont"}};
  void* fallback = nullptr;
  const char* fallbackName = "";
  const bool canResize = O.Font_CacheType >= 0 && O.Font_LegacyFontSize >= 0;
  for (const auto& slot : slots) {
    if (slot.offset < 0) continue;
    void* font = Ptr(engine, slot.offset);
    if (!font) continue;
    if (!fallback) {
      fallback = font;
      fallbackName = slot.name;
    }
    if (canResize && At<uint8_t>(font, O.Font_CacheType) == kRuntimeFontCache) {
      runtime = true;
      slotName = slot.name;
      return font;
    }
  }
  runtime = false;
  slotName = fallbackName;
  return fallback;
}

struct Calibration {
  void* font = nullptr;
  float nativeLine = 0.f;  // line height at the font's own size
  float perPoint = 0.f;    // line height per point, runtime fonts only
};
Calibration g_calibration;

struct SizeCache {
  void* font = nullptr;
  float unit = 0.f;
  float dpi = 0.f;
  int32_t size[static_cast<int>(Role::Count)] = {};
  float line[static_cast<int>(Role::Count)] = {};
};
SizeCache g_sizes;

// Arrow cursor, one row per cell: black spans (outline) then white spans (fill), [a, b) in cells.
struct Span {
  int8_t a, b;
};
struct CursorRow {
  Span black[2];
  Span white[2];
};
// clang-format off
constexpr CursorRow kArrow[] = {
    {{{0, 1}, {0, 0}},  {{0, 0}, {0, 0}}},
    {{{0, 2}, {0, 0}},  {{0, 0}, {0, 0}}},
    {{{0, 3}, {0, 0}},  {{1, 2}, {0, 0}}},
    {{{0, 4}, {0, 0}},  {{1, 3}, {0, 0}}},
    {{{0, 5}, {0, 0}},  {{1, 4}, {0, 0}}},
    {{{0, 6}, {0, 0}},  {{1, 5}, {0, 0}}},
    {{{0, 7}, {0, 0}},  {{1, 6}, {0, 0}}},
    {{{0, 8}, {0, 0}},  {{1, 7}, {0, 0}}},
    {{{0, 9}, {0, 0}},  {{1, 8}, {0, 0}}},
    {{{0, 10}, {0, 0}}, {{1, 9}, {0, 0}}},
    {{{0, 11}, {0, 0}}, {{1, 10}, {0, 0}}},
    {{{0, 12}, {0, 0}}, {{1, 7}, {0, 0}}},
    {{{0, 8}, {0, 0}},  {{1, 4}, {5, 7}}},
    {{{0, 8}, {0, 0}},  {{1, 3}, {5, 7}}},
    {{{0, 3}, {5, 9}},  {{1, 2}, {6, 8}}},
    {{{0, 2}, {5, 9}},  {{6, 8}, {0, 0}}},
    {{{0, 1}, {6, 10}}, {{7, 9}, {0, 0}}},
    {{{6, 10}, {0, 0}}, {{7, 9}, {0, 0}}},
    {{{7, 9}, {0, 0}},  {{0, 0}, {0, 0}}},
};
// clang-format on

}  // namespace

bool Painter::Begin(void* canvas, float unit) {
  End();
  void* engine = *reinterpret_cast<void**>(A.GEngine);
  if (!canvas || !engine) return false;
  width_ = static_cast<float>(At<int32_t>(canvas, O.Canvas_SizeX));
  height_ = static_cast<float>(At<int32_t>(canvas, O.Canvas_SizeY));
  if (width_ < 64.f || height_ < 64.f) return false;
  const char* slot = "";
  font_ = PickFont(engine, runtimeFont_, slot);
  if (!font_) return false;
  canvas_ = canvas;
  white_ = O.Canvas_DefaultTexture >= 0 ? Ptr(canvas, O.Canvas_DefaultTexture) : nullptr;

  // The debug canvas works in DPI-scaled units: FCanvas::Construct bakes the DPI scale (Windows
  // display scaling, 1.5 at 150%) into the base transform, so every rectangle and text position is
  // multiplied by it, while UCanvas::SizeX/SizeY stay in physical pixels. The menu lays out in
  // physical pixels and divides by the scale when drawing. Runtime-font glyphs are rasterized at
  // size * DPI scale.
  dpi_ = 1.f;
  if (O.Canvas_Canvas >= 0 && O.FCanvas_DPIScale >= 0) {
    if (void* fc = Ptr(canvas, O.Canvas_Canvas)) {
      // Mirror FCanvas::GetDPIScale: the engine applies whatever value is there (Windows allows up to 5.0).
      const bool stereo = O.FCanvas_Stereo >= 0 && At<bool>(fc, O.FCanvas_Stereo);
      const float d = At<float>(fc, O.FCanvas_DPIScale);
      if (!stereo && std::isfinite(d) && d > 0.f && d <= 16.f) dpi_ = d;
    }
  }
  // Runtime-font glyphs are rasterized at size * DPI; bitmap-font glyphs are drawn at texel size.
  const bool dpiGlyphs = O.Font_CacheType < 0 || At<uint8_t>(font_, O.Font_CacheType) == kRuntimeFontCache;
  sizeDpi_ = dpiGlyphs ? dpi_ : 1.f;

  if (g_calibration.font != font_) {
    g_calibration = {};
    g_calibration.font = font_;
    g_calibration.nativeLine = MeasureLine(canvas, font_, 0);
    if (runtimeFont_) g_calibration.perPoint = MeasureLine(canvas, font_, 32) / 32.f;
    LOG("Menu font: GEngine->%s, %s, native size %d (line %.1f px), %.3f px per point, DPI scale %.2f, canvas %.0fx%.0f",
        slot, runtimeFont_ ? "runtime (resizable)" : "bitmap (fixed size)",
        O.Font_LegacyFontSize >= 0 ? At<int32_t>(font_, O.Font_LegacyFontSize) : -1, g_calibration.nativeLine,
        g_calibration.perPoint, dpi_, width_, height_);
    g_sizes.font = nullptr;
  }
  if (g_sizes.font != font_ || g_sizes.unit != unit || g_sizes.dpi != dpi_) {
    g_sizes.font = font_;
    g_sizes.unit = unit;
    g_sizes.dpi = dpi_;
    for (int r = 0; r < static_cast<int>(Role::Count); ++r) {
      const float target = (std::max)(kMinLinePx, kRoleLineDp[r] * unit);
      if (runtimeFont_ && g_calibration.perPoint > 0.1f) {
        const int32_t size = std::clamp(static_cast<int32_t>(std::lround(target / (g_calibration.perPoint * dpi_))), 6, 200);
        g_sizes.size[r] = size;
        g_sizes.line[r] = MeasureLine(canvas, font_, size) * dpi_;
      } else {
        g_sizes.size[r] = 0;
        g_sizes.line[r] = g_calibration.nativeLine * sizeDpi_;
      }
    }
  }
  std::copy(std::begin(g_sizes.size), std::end(g_sizes.size), size_);
  std::copy(std::begin(g_sizes.line), std::end(g_sizes.line), lineH_);

  SaveAndNeutralize(canvas);
  begun_ = true;
  return true;
}

void Painter::End() {
  if (!begun_) return;
  RestoreCanvas();
  begun_ = false;
}

void Painter::Fill(const Rect& r, FLinearColor color) const {
  if (!begun_ || r.w <= 0.f || r.h <= 0.f) return;
  // Edges on whole physical pixels, then into canvas units.
  const float x0 = std::round(r.x), y0 = std::round(r.y);
  const float x1 = (std::max)(x0 + 1.f, std::round(r.x + r.w)), y1 = (std::max)(y0 + 1.f, std::round(r.y + r.h));
  const double s = 1.0 / dpi_;
  reinterpret_cast<DrawTextureFn>(A.CanvasDrawTexture)(canvas_, white_, {x0 * s, y0 * s}, {(x1 - x0) * s, (y1 - y0) * s},
                                                       {0, 0}, {1, 1}, color, kBlendTranslucent, 0.f, {0.5, 0.5});
}

void Painter::Outline(const Rect& r, FLinearColor color) const {
  const float x0 = std::round(r.x), y0 = std::round(r.y);
  const float w = std::round(r.x + r.w) - x0, h = std::round(r.y + r.h) - y0;
  if (w < 2.f || h < 2.f) return;
  Fill({x0, y0, w, 1.f}, color);
  Fill({x0, y0 + h - 1.f, w, 1.f}, color);
  Fill({x0, y0 + 1.f, 1.f, h - 2.f}, color);
  Fill({x0 + w - 1.f, y0 + 1.f, 1.f, h - 2.f}, color);
}

void Painter::Text(float x, float y, const wchar_t* s, Role role, FColor color) const {
  if (!begun_ || !s || !*s) return;
  At<FColor>(canvas_, O.Canvas_DrawColor) = color;
  alignas(16) unsigned char renderInfo[0x40] = {};  // FFontRenderInfo: no clipping, shadow or glow
  SizeScope scope(runtimeFont_ ? font_ : nullptr, size_[static_cast<int>(role)]);
  reinterpret_cast<DrawTextFn>(A.CanvasDrawText)(canvas_, font_, View(s), TextCoord(x), TextCoord(y), 1.f, 1.f,
                                                 renderInfo);
}

// UCanvas::DrawText floors the position in canvas units before it's multiplied by the DPI scale,
// so text can only start at multiples of the scale (1.5 px steps at 150%). Picks the canvas unit
// closest to |physical|, preferring whole pixels: the glyph atlas is sampled bilinearly, so a
// glyph half a pixel off looks blurry.
float Painter::TextCoord(float physical) const {
  if (dpi_ == 1.f) return std::floor(physical + 0.5f);
  const float first = std::floor(physical / dpi_) - 3.f;
  float best = first, bestCost = 1e30f;
  for (int i = 0; i <= 7; ++i) {
    const float unit = first + static_cast<float>(i);
    const float at = unit * dpi_;
    const float cost = std::fabs(at - physical) + 3.f * std::fabs(at - std::round(at));
    if (cost < bestCost) {
      bestCost = cost;
      best = unit;
    }
  }
  return best;
}

void Painter::TextCentered(const Rect& r, const wchar_t* s, Role role, FColor color) const {
  Text(r.x + (r.w - TextWidth(s, role)) * 0.5f, r.y + (r.h - LineHeight(role)) * 0.5f, s, role, color);
}

float Painter::TextWidth(const wchar_t* s, Role role) const {
  if (!begun_ || !s || !*s) return 0.f;
  SizeScope scope(runtimeFont_ ? font_ : nullptr, size_[static_cast<int>(role)]);
  float w = 0.f, h = 0.f;
  reinterpret_cast<TextSizeFn>(A.CanvasTextSize)(canvas_, font_, View(s), &w, &h, 1.f, 1.f);
  return w * sizeDpi_;
}

void Painter::Cursor(float x, float y, bool pressed) const {
  const float cell = (std::max)(1.f, std::round(height_ / 1080.f));
  x = std::floor(x);
  y = std::floor(y);
  const FLinearColor black{0.f, 0.f, 0.f, 1.f};
  const FLinearColor white = pressed ? FLinearColor{0.8f, 0.8f, 0.8f, 1.f} : FLinearColor{1.f, 1.f, 1.f, 1.f};
  for (int pass = 0; pass < 2; ++pass) {
    for (int row = 0; row < static_cast<int>(sizeof(kArrow) / sizeof(kArrow[0])); ++row) {
      for (const Span& span : pass == 0 ? kArrow[row].black : kArrow[row].white) {
        if (span.b <= span.a) continue;
        Fill({x + span.a * cell, y + row * cell, (span.b - span.a) * cell, cell}, pass == 0 ? black : white);
      }
    }
  }
}

void EmergencyRestore() {
  if (g_sizeSlot) {
    *g_sizeSlot = g_sizeOld;
    g_sizeSlot = nullptr;
  }
  RestoreCanvas();
}

}  // namespace canvas
