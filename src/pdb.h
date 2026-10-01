#pragma once

// Resolves function addresses, struct member offsets and enum values from the game's own PDB
// (Townfall-Win64-Shipping.pdb ships next to the exe). Because nothing is hard-coded, the mod
// keeps working after game patches as long as the PDB is updated alongside the exe.

#include <cstdint>
#include <string>
#include <vector>

namespace pdb {

enum class Kind { Symbol, Field, EnumValue };

struct Request {
  Kind kind;
  const char* a;  // Symbol: symbol name. Field / EnumValue: type name.
  const char* b;  // Field: member name. EnumValue: enumerator name. Unused for Symbol.
  int64_t* out;   // Symbol: absolute address. Field: byte offset. EnumValue: value.
  bool optional;
};

struct Module {
  uintptr_t base = 0;
  uint32_t imageSize = 0;
  uint32_t timeStamp = 0;
  std::wstring path;
};

Module DescribeModule(void* hmodule);

// Fills every request's |out|. Results are cached in |cachePath| keyed on the exe build, so
// dbghelp only runs on the first launch after the game updates. Returns false (with |error|
// set) if a non-optional request couldn't be resolved.
bool Resolve(const Module& mod, std::vector<Request>& reqs, const std::wstring& cachePath, std::string& error);

}  // namespace pdb
