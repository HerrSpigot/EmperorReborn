#include <windows.h>
#include <cstdint>
#include <stdio.h>
#include <detours.h>
#include "GameExeImports.hpp"
#include "PatchDebugLog.hpp"
#include "Log.hpp"
#include "DebugLogPatchTable.hpp"


void LogPatched(const char* format, ...)
{
  va_list args = nullptr;
  va_start(args, format);
  vaLog(format, args);
  va_end(args);
  Log("\n");
}

void Log2Patched(int someN, const char* format, ...)
{
  Log("%d: ", someN);
  va_list args = nullptr;
  va_start(args, format);
  vaLog(format, args);
  va_end(args);
  Log("\n");
}

void (*GameExeLog)(const char* format, ...) = (void (*)(const char* format, ...))0x401CB5;
void (*GameExeLog2)(int someN, const char* format, ...) = (void (*)(int someN, const char* format, ...))0x401CBA;

void patchDebugLog()
{
  // There is an empty function at 0x401CB0 that is called all over the place with string arguments.
  // It is clearly a debug logging function.
  // The problem with this function is that it's not one function, it's many functions.
  // The original binary for this function is just a single byte ret instruction.
  // My guess is that in the original source code, there were a bunch of debugging functions
  // that were behind #ifdef DEBUG, and in release builds they were replaced with empty functions,
  // also things like empty virtual functions to satisfy inheritance. They all end up compiling to
  // a single ret, and then the linker says "oh look, I have duplicated functions" and merges them
  // all. There are actually two distinct signatures, one normal printf style ("Log"), and another that's
  // the same but takes some integer id as a first parameter before the format string ("Log2").
  // This series of patches creates two empty functions in the padding space after the original function,
  // and redirects calls that are actual logging to use those two functions instead of the original.
  for (PatchElement element : debugLogPatches)
  {
    DWORD dwOldProtect = 0;
    VirtualProtect((LPVOID)element.address, 1, PAGE_EXECUTE_READWRITE, &dwOldProtect);

    uint8_t* patchLocation = (uint8_t*)element.address;
    if (*patchLocation != element.original)
      Log("bad patch! %p\n", patchLocation);

    *patchLocation = element.patched;

    VirtualProtect((LPVOID)element.address, 1, dwOldProtect, &dwOldProtect);
  }

  // These detours attach to the two new functions we just created
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID&)GameExeLog, LogPatched);
  DetourAttach(&(PVOID&)GameExeLog2, Log2Patched);
  DetourTransactionCommit();
}