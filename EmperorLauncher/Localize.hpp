#pragma once
#include <windows.h>
#include <string>

// The launcher's own texts come in German and English. The language is picked in the launcher (saved in the
// settings); until the user picks one it follows the Windows display language.
inline bool& uiGermanFlag()
{
  static bool german = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
  return german;
}

// code: "de", "en", or empty to follow Windows.
inline void setLanguage(const std::string& code)
{
  if (code == "de")
    uiGermanFlag() = true;
  else if (code == "en")
    uiGermanFlag() = false;
  else
    uiGermanFlag() = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
}

inline bool uiIsGerman()
{
  return uiGermanFlag();
}

inline const char* tr(const char* english, const char* german)
{
  return uiIsGerman() ? german : english;
}

inline const wchar_t* trw(const wchar_t* english, const wchar_t* german)
{
  return uiIsGerman() ? german : english;
}
