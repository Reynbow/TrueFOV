#include "pdb.h"

#include <windows.h>

#include <DbgHelp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "log.h"
#include "util.h"

namespace pdb {
namespace {

// cvconst.h values (not exposed by DbgHelp.h).
constexpr DWORD kSymTagData = 7;
constexpr DWORD kDataIsMember = 7;

// Cached as a "?key" line: looked up in this build's PDB and not found. Lets the cache answer
// every request, so requests added by a mod update trigger a fresh PDB read. (Older builds skip
// lines without '=', so to them such a key just looks absent.)
constexpr int64_t kMissing = INT64_MIN;

// Layout-compatible with VARIANT, without pulling in the OLE headers.
struct VariantLite {
  unsigned short vt;
  unsigned short reserved[3];
  union {
    long long i64;
    unsigned long long u64;
    long i32;
    unsigned long u32;
    short i16;
    unsigned short u16;
    signed char i8;
    unsigned char u8;
  } v;
  void* pad;
};
static_assert(sizeof(VariantLite) == 24, "VARIANT layout");

int64_t VariantToInt(const VariantLite& v) {
  switch (v.vt) {
    case 16: return v.v.i8;    // VT_I1
    case 17: return v.v.u8;    // VT_UI1
    case 2: return v.v.i16;    // VT_I2
    case 18: return v.v.u16;   // VT_UI2
    case 3:                    // VT_I4
    case 22: return v.v.i32;   // VT_INT
    case 19:                   // VT_UI4
    case 23: return v.v.u32;   // VT_UINT
    case 20: return v.v.i64;   // VT_I8
    case 21: return static_cast<int64_t>(v.v.u64);  // VT_UI8
    default: return v.v.i32;
  }
}

std::wstring Widen(const char* s) {
  std::wstring w;
  while (*s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s++)));
  return w;
}

std::string Narrow(const wchar_t* s) {
  std::string n;
  while (*s) n.push_back(static_cast<char>(*s++));
  return n;
}

std::string KeyOf(const Request& r) {
  switch (r.kind) {
    case Kind::Symbol: return std::string("S ") + r.a;
    case Kind::Field: return std::string("F ") + r.a + "::" + r.b;
    case Kind::EnumValue: return std::string("E ") + r.a + "::" + r.b;
  }
  return {};
}

std::string BuildId(const Module& m) {
  char buf[40];
  snprintf(buf, sizeof buf, "build %08X-%08X", m.timeStamp, m.imageSize);
  return buf;
}

void StripNewline(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

bool LoadCache(const std::wstring& path, const std::string& buildId, std::unordered_map<std::string, int64_t>& out) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return false;
  char line[1024];
  bool ok = false;
  if (fgets(line, sizeof line, f)) {
    StripNewline(line);
    ok = buildId == line;
  }
  while (ok && fgets(line, sizeof line, f)) {
    StripNewline(line);
    if (line[0] == '?') {
      out[line + 1] = kMissing;
      continue;
    }
    char* eq = strrchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    out[line] = _strtoi64(eq + 1, nullptr, 10);
  }
  fclose(f);
  return ok;
}

void SaveCache(const std::wstring& path, const std::string& buildId, const std::unordered_map<std::string, int64_t>& values) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"w") != 0 || !f) {
    LOG("Couldn't write symbol cache");
    return;
  }
  fprintf(f, "%s\n", buildId.c_str());
  for (const auto& [key, value] : values) {
    if (value == kMissing) {
      fprintf(f, "?%s\n", key.c_str());
    } else {
      fprintf(f, "%s=%lld\n", key.c_str(), static_cast<long long>(value));
    }
  }
  fclose(f);
}

struct DbgHelp {
  decltype(&::SymInitializeW) SymInitializeW = nullptr;
  decltype(&::SymCleanup) SymCleanup = nullptr;
  decltype(&::SymGetOptions) SymGetOptions = nullptr;
  decltype(&::SymSetOptions) SymSetOptions = nullptr;
  decltype(&::SymLoadModuleExW) SymLoadModuleExW = nullptr;
  decltype(&::SymGetModuleInfoW64) SymGetModuleInfoW64 = nullptr;
  decltype(&::SymFromNameW) SymFromNameW = nullptr;
  decltype(&::SymGetTypeFromNameW) SymGetTypeFromNameW = nullptr;
  decltype(&::SymGetTypeInfo) SymGetTypeInfo = nullptr;

  bool Load() {
    HMODULE m = LoadLibraryW(L"dbghelp.dll");
    if (!m) return false;
#define DH_LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(m, #name))
    DH_LOAD(SymInitializeW);
    DH_LOAD(SymCleanup);
    DH_LOAD(SymGetOptions);
    DH_LOAD(SymSetOptions);
    DH_LOAD(SymLoadModuleExW);
    DH_LOAD(SymGetModuleInfoW64);
    DH_LOAD(SymFromNameW);
    DH_LOAD(SymGetTypeFromNameW);
    DH_LOAD(SymGetTypeInfo);
#undef DH_LOAD
    return SymInitializeW && SymCleanup && SymGetOptions && SymSetOptions && SymLoadModuleExW &&
           SymGetModuleInfoW64 && SymFromNameW && SymGetTypeFromNameW && SymGetTypeInfo;
  }
};

struct SymbolBuffer {
  SYMBOL_INFOW info;
  wchar_t name[1024];

  SymbolBuffer() {
    memset(this, 0, sizeof *this);
    info.SizeOfStruct = sizeof(SYMBOL_INFOW);
    info.MaxNameLen = 1024;
  }
};

// Maps member (or enumerator) name -> offset (or value) for one type.
using MemberMap = std::unordered_map<std::string, int64_t>;

bool CollectMembers(DbgHelp& d, HANDLE h, DWORD64 mb, const char* type, bool enumerators, MemberMap& out) {
  SymbolBuffer sb;
  if (!d.SymGetTypeFromNameW(h, mb, Widen(type).c_str(), &sb.info)) return false;
  const ULONG tid = sb.info.TypeIndex;
  DWORD count = 0;
  if (!d.SymGetTypeInfo(h, mb, tid, TI_GET_CHILDRENCOUNT, &count) || count == 0) return false;
  std::vector<char> buf(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
  auto* params = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(buf.data());
  params->Count = count;
  params->Start = 0;
  if (!d.SymGetTypeInfo(h, mb, tid, TI_FINDCHILDREN, params)) return false;
  for (DWORD i = 0; i < count; ++i) {
    const ULONG child = params->ChildId[i];
    DWORD tag = 0;
    if (!d.SymGetTypeInfo(h, mb, child, TI_GET_SYMTAG, &tag) || tag != kSymTagData) continue;
    WCHAR* wname = nullptr;
    if (!d.SymGetTypeInfo(h, mb, child, TI_GET_SYMNAME, &wname) || !wname) continue;
    const std::string name = Narrow(wname);
    LocalFree(wname);
    if (enumerators) {
      VariantLite v{};
      if (d.SymGetTypeInfo(h, mb, child, TI_GET_VALUE, &v)) out[name] = VariantToInt(v);
    } else {
      DWORD kind = 0, offset = 0;
      if (d.SymGetTypeInfo(h, mb, child, TI_GET_DATAKIND, &kind) && kind == kDataIsMember &&
          d.SymGetTypeInfo(h, mb, child, TI_GET_OFFSET, &offset)) {
        out[name] = offset;
      }
    }
  }
  return true;
}

bool ResolveWithDbgHelp(const Module& mod, std::vector<Request>& reqs, std::unordered_map<std::string, int64_t>& resolved,
                        std::string& error) {
  DbgHelp d;
  if (!d.Load()) {
    error = "dbghelp.dll unavailable";
    return false;
  }
  // A private session: dbghelp keys sessions by this value, so it can't collide with the
  // engine's own crash-reporting session on the real process handle.
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x7F0F4F56));
  const std::wstring dir = mod.path.substr(0, mod.path.find_last_of(L"\\/"));
  const DWORD oldOptions = d.SymGetOptions();
  d.SymSetOptions(SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  bool ok = false;
  if (!d.SymInitializeW(h, dir.c_str(), FALSE)) {
    error = "SymInitialize failed";
  } else {
    const DWORD64 mb = d.SymLoadModuleExW(h, nullptr, mod.path.c_str(), nullptr, mod.base, mod.imageSize, nullptr, 0);
    IMAGEHLP_MODULEW64 info{};
    info.SizeOfStruct = sizeof info;
    if (!mb) {
      error = "SymLoadModuleEx failed";
    } else if (!d.SymGetModuleInfoW64(h, mb, &info) || info.SymType != SymPdb || info.PdbUnmatched) {
      error = "Townfall-Win64-Shipping.pdb is missing or doesn't match the exe";
    } else {
      ok = true;
      std::unordered_map<std::string, MemberMap> types;
      for (Request& r : reqs) {
        int64_t value = 0;
        bool found = false;
        if (r.kind == Kind::Symbol) {
          SymbolBuffer sb;
          if (d.SymFromNameW(h, Widen(r.a).c_str(), &sb.info) && sb.info.Address) {
            value = static_cast<int64_t>(sb.info.Address - mod.base);  // cached as an RVA
            found = true;
          }
        } else {
          const std::string typeKey = std::string(r.kind == Kind::Field ? "F " : "E ") + r.a;
          auto it = types.find(typeKey);
          if (it == types.end()) {
            MemberMap members;
            CollectMembers(d, h, mb, r.a, r.kind == Kind::EnumValue, members);
            it = types.emplace(typeKey, std::move(members)).first;
          }
          auto m = it->second.find(r.b);
          if (m != it->second.end()) {
            value = m->second;
            found = true;
          }
        }
        if (found) resolved[KeyOf(r)] = value;
      }
    }
    d.SymCleanup(h);
  }
  d.SymSetOptions(oldOptions);
  return ok;
}

}  // namespace

Module DescribeModule(void* hmodule) {
  Module m;
  auto* base = static_cast<unsigned char*>(hmodule);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  m.base = reinterpret_cast<uintptr_t>(base);
  m.imageSize = nt->OptionalHeader.SizeOfImage;
  m.timeStamp = nt->FileHeader.TimeDateStamp;
  wchar_t path[MAX_PATH * 2] = {};
  GetModuleFileNameW(static_cast<HMODULE>(hmodule), path, MAX_PATH * 2);
  m.path = path;
  return m;
}

bool Resolve(const Module& mod, std::vector<Request>& reqs, const std::wstring& cachePath, std::string& error) {
  const std::string buildId = BuildId(mod);
  const double start = util::Now();
  std::unordered_map<std::string, int64_t> values;

  // The cache is usable only if it has an answer (a value or "not found") for every request.
  bool complete = LoadCache(cachePath, buildId, values);
  if (complete) {
    for (const Request& r : reqs) {
      if (!values.count(KeyOf(r))) complete = false;
    }
  }
  if (complete) {
    LOG("Symbols loaded from cache (%s)", buildId.c_str());
  } else {
    LOG("Reading symbols from %ls ...", mod.path.c_str());
    values.clear();
    if (!ResolveWithDbgHelp(mod, reqs, values, error)) return false;
    bool requiredMissing = false;
    for (const Request& r : reqs) {
      const bool added = values.emplace(KeyOf(r), kMissing).second;  // no-op for resolved keys
      requiredMissing |= added && !r.optional;
    }
    // Don't cache a failed start: a repaired PDB (e.g. after verifying game files) gets read next time.
    if (!requiredMissing) SaveCache(cachePath, buildId, values);
    LOG("Symbols resolved from PDB in %.2fs", util::Now() - start);
  }

  std::string missing;
  for (Request& r : reqs) {
    auto it = values.find(KeyOf(r));
    if (it == values.end() || it->second == kMissing) {
      LOG("  %s: %s", r.optional ? "optional, not found" : "MISSING", KeyOf(r).c_str());
      if (!r.optional) missing += (missing.empty() ? "" : ", ") + KeyOf(r);
      continue;
    }
    *r.out = r.kind == Kind::Symbol ? static_cast<int64_t>(mod.base) + it->second : it->second;
  }
  if (!missing.empty()) {
    error = "unresolved: " + missing;
    return false;
  }
  return true;
}

}  // namespace pdb
