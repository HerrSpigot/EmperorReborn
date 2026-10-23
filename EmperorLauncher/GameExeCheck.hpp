#pragma once
#include <string>

// md5 of the English v1.09 Game.exe that all the hooks were written against.
constexpr const char* gameExeV109EnglishMd5 = "7dc4fda2f6b7e8a6b70e846686a81938";

enum class GameExeLayout
{
  Missing,      // no Game.exe at all
  Compatible,   // same code layout as English v1.09 (e.g. the German v1.09 build), all patches apply
  Incompatible, // different code (other patch level, cracked/modded exe, ...)
};

// Checks a Game.exe on disk against the v1.09 code fingerprint in DebugLogPatchTable.hpp.
GameExeLayout checkGameExeLayout(const std::string& gameExePath);
