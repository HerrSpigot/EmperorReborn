#pragma once
#include <windows.h>

// The launcher's own messages: German on a German Windows (most people installing from the German
// "Schlacht um Dune" CDs), English everywhere else.
inline bool uiIsGerman()
{
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
}

inline const char* tr(const char* english, const char* german)
{
  return uiIsGerman() ? german : english;
}
