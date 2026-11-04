// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <FEXCore/fextl/string.h>
#include <winternl.h>

namespace FEX::Windows {
// Converts into a stack buffer rather than letting ntdll allocate from the process heap. This is called while holding locks that
// heap code can also need (e.g. ThreadCreationMutex on image map, taken by heap growth through the NtAllocateVirtualMemory
// notification), so a process heap allocation here can deadlock against another thread holding the heap lock.
inline fextl::string UnicodeStringToAnsi(const UNICODE_STRING& StringW) {
  std::array<char, PATH_MAX * 3> Buf;
  STRING StringA {.Length = 0, .MaximumLength = Buf.size(), .Buffer = Buf.data()};
  if (RtlUnicodeStringToAnsiString(&StringA, &StringW, FALSE)) {
    return {};
  }

  return fextl::string(StringA.Buffer, StringA.Length);
}

inline fextl::string GetExecutableFilePath() {
  std::array<WCHAR, PATH_MAX> Buf;
  UNICODE_STRING PathW {.Length = 0, .MaximumLength = Buf.size() * sizeof(WCHAR), .Buffer = Buf.data()};

  if (LdrGetDllFullName(nullptr, &PathW)) {
    return {};
  }

  return UnicodeStringToAnsi(PathW);
}

inline fextl::string GetSectionFilePath(uint64_t Address) {
  struct {
    MEMORY_SECTION_NAME Info;
    std::array<WCHAR, PATH_MAX> PathW;
  } Buffer;

  if (NtQueryVirtualMemory(NtCurrentProcess(), reinterpret_cast<void*>(Address), MemoryMappedFilenameInformation, &Buffer, sizeof(Buffer), NULL)) {
    return {};
  }

  return UnicodeStringToAnsi(Buffer.Info.SectionFileName);
}

inline std::string_view BaseName(std::string_view Path) {
  return Path.substr(Path.find_last_of('\\') + 1);
}
} // namespace FEX::Windows
