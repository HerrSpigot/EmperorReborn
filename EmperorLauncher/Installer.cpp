#include <windows.h>
#include <string>
#include "../EmperorHooks/Error.hpp"
#include "resource.h"
#include "Localize.hpp"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <fdi.h>
#include <fcntl.h>
#include <corecrt_io.h>
#pragma comment(lib,"cabinet.lib")


std::string getExeDir()
{
  char buf[MAX_PATH] = {};
  GetModuleFileNameA(NULL, buf, MAX_PATH);
  std::string p = buf;
  size_t slash = p.find_last_of("\\/");
  return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

// The official v1.09 patch exists per language: EM109EN.EXE for the English CDs, EM109DE.EXE for the German
// ones (some mirrors call it EM109GE.EXE). The patch has to match the language of the discs, otherwise
// RTPatch refuses to touch the files. We take whichever one the user put next to the launcher, preferring
// the German one when both are present (an English install never has the German patch lying around).
static const char* knownPatchNames[] = { "EM109DE.EXE", "EM109GE.EXE", "EM109EN.EXE" };

static std::string findPatchExe()
{
  std::string exeDir = getExeDir();

  for (const char* name : knownPatchNames)
  {
    if (std::filesystem::exists(exeDir + "\\" + name))
      return name;
  }

  // Anything else that looks like a v1.09 patch (other languages, lowercase names, ...).
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(exeDir, ec))
  {
    std::string name = entry.path().filename().string();
    if (name.size() == 11 && _strnicmp(name.c_str(), "EM109", 5) == 0 && _stricmp(name.c_str() + 7, ".EXE") == 0)
      return name;
  }

  return "";
}

// Copies the user's patch exe into the install dir and returns its file name.
std::string extractPatchExe(const std::string& installDir)
{
  // Copyright note: the official Westwood/EA v1.09 patch (EM109EN.EXE / EM109DE.EXE) is NOT
  // bundled with this program because it is copyrighted by EA/Westwood. The user
  // must supply their own copy by placing it next to the launcher executable.
  std::string patchName = findPatchExe();

  while (patchName.empty())
  {
    std::string message = std::string(tr(
      "The official Westwood/EA v1.09 patch is required but was not found.\n\n"
      "Please place your own copy of the patch matching your discs next to EmperorReborn.exe:\n\n"
      "    English CDs: EM109EN.EXE\n"
      "    German CDs:  EM109DE.EXE\n\n"
      "Folder: ",
      "Der offizielle Westwood/EA Patch v1.09 wird benoetigt, wurde aber nicht gefunden.\n\n"
      "Bitte lege deine eigene Kopie des Patches passend zur Sprache deiner CDs neben EmperorReborn.exe:\n\n"
      "    Deutsche CDs:  EM109DE.EXE\n"
      "    Englische CDs: EM109EN.EXE\n\n"
      "Ordner: ")) + getExeDir() + tr(
      "\n\nThis file is copyrighted by EA/Westwood and is not distributed with Emperor "
      "Reborn. See README.txt for details.\n\n"
      "Click OK after you have copied the file, or Cancel to quit.",
      "\n\nDiese Datei ist urheberrechtlich geschuetzt (EA/Westwood) und wird nicht mit Emperor "
      "Reborn ausgeliefert. Details in der INSTALL.txt.\n\n"
      "Klicke OK, sobald die Datei dort liegt, oder Abbrechen zum Beenden.");

    if (MessageBoxA(nullptr, message.c_str(),
          tr("Emperor Reborn - v1.09 patch required", "Emperor Reborn - Patch v1.09 benoetigt"),
          MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
      exit(1);

    patchName = findPatchExe();
  }

  printf("Using patch %s\n", patchName.c_str());

  std::error_code ec;
  std::filesystem::copy_file(getExeDir() + "\\" + patchName, installDir + "\\" + patchName,
    std::filesystem::copy_options::overwrite_existing, ec);
  release_assert(!ec);

  return patchName;
}

void extractPatchDll(std::string patchExePath, std::string patchDllPath)
{
  HMODULE exeModule = LoadLibraryExA(patchExePath.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  release_assert(exeModule);

  // The RTPatch runtime DLL is stored as resource 19274 of type 20530 in EM109EN.EXE. Other language builds
  // of the patch were packed separately, so if the id differs just take the first resource of that type.
  HRSRC resource = FindResourceA(exeModule, MAKEINTRESOURCEA(19274), MAKEINTRESOURCEA(20530));
  if (!resource)
  {
    WORD firstId = 0;
    EnumResourceNamesA(exeModule, MAKEINTRESOURCEA(20530), [](HMODULE, LPCSTR, LPSTR name, LONG_PTR param) -> BOOL
    {
      if (!IS_INTRESOURCE(name))
        return TRUE;
      *reinterpret_cast<WORD*>(param) = WORD(reinterpret_cast<ULONG_PTR>(name));
      return FALSE;
    }, reinterpret_cast<LONG_PTR>(&firstId));

    if (firstId)
      resource = FindResourceA(exeModule, MAKEINTRESOURCEA(firstId), MAKEINTRESOURCEA(20530));
  }
  release_assert(resource);

  void* resourceData = LockResource(LoadResource(exeModule, resource));
  release_assert(resourceData);

  DWORD dllSize = *((DWORD*)resourceData);
  uint8_t* dllData = ((uint8_t*)resourceData) + 4;

  FILE* f = fopen(patchDllPath.c_str(), "wb");
  release_assert(f);
  release_assert(fwrite(dllData, 1, dllSize, f) == dllSize);
  fclose(f);

  FreeResource(resourceData);
  FreeLibrary(exeModule);
}


char* __stdcall patchCallback(int type, char* msg)
{
  switch (type)
  {
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
      if (msg)
        puts(msg);
      break;
    default: break;
  }

  return (char*)"";
}

void patch(const std::string& installDir)
{
  std::string patchName = extractPatchExe(installDir);
  std::string patchExePath = installDir + "\\" + patchName;
  std::string patchDllPath = installDir + "\\" + patchName.substr(0, patchName.size() - 4) + ".DLL";

  extractPatchDll(patchExePath, patchDllPath);

  HMODULE patchDll = LoadLibraryA(patchDllPath.c_str());
  release_assert(patchDll);

  typedef int(__stdcall* PFN_RTPatchApply32)(char*, void*, int);
  PFN_RTPatchApply32 RTPatchApply32 = (PFN_RTPatchApply32)GetProcAddress(patchDll, "RTPatchApply32@12");
  release_assert(RTPatchApply32);

  std::string command = "/z /noa \"" + patchExePath + "\"";


  RegDeleteTreeA(HKEY_CURRENT_USER, "emperorlauncher_westwood_patch_temp");

  HKEY key = nullptr;
  release_assert(RegCreateKeyA(HKEY_CURRENT_USER, "emperorlauncher_westwood_patch_temp", &key) == ERROR_SUCCESS);
  release_assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, key) == ERROR_SUCCESS);
  RegCloseKey(key);

  std::string gameExePath = installDir + "\\Emperor.EXE";
  release_assert(RegSetKeyValueA(HKEY_LOCAL_MACHINE, "Software\\Westwood\\Emperor", "InstallPath", REG_SZ, gameExePath.c_str(), DWORD(gameExePath.size()) + 1) == ERROR_SUCCESS);

  RTPatchApply32((char*)command.c_str(), patchCallback, 1);

  release_assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr) == ERROR_SUCCESS);
  release_assert(RegDeleteTreeA(HKEY_CURRENT_USER, "emperorlauncher_westwood_patch_temp") == ERROR_SUCCESS);

  FreeLibrary(patchDll);

  DeleteFileA(patchExePath.c_str());
  DeleteFileA(patchDllPath.c_str());
}


FNALLOC(fnMemAlloc) {
  return malloc(cb);
}

FNFREE(fnMemFree) {
  free(pv);
}

FNOPEN(fnFileOpen) {
  return _open(pszFile, oflag, pmode);
}

FNREAD(fnFileRead) {
  return (UINT)_read(hf, pv, cb);
}

FNWRITE(fnFileWrite) {
  return (UINT)_write(hf, pv, cb);
}

FNCLOSE(fnFileClose) {
  return _close(hf);
}

FNSEEK(fnFileSeek) {
  return _lseek(hf, dist, seektype);
}

FNFDINOTIFY(fnNotify) {
  switch (fdint) {
  case fdintCOPY_FILE: {
    std::string dest = ((const char*)pfdin->pv);
    dest += "\\";

    const char* subPath = pfdin->psz1;
    if (subPath[0] == '\\')
      subPath++;

    dest += subPath;

    printf("Extracting %s\n", dest.data());

    std::filesystem::create_directories(dest.substr(0, dest.find_last_of('\\')));
    return fnFileOpen(dest.data(), _O_BINARY | _O_CREAT | _O_WRONLY | _O_SEQUENTIAL, _S_IREAD | _S_IWRITE);
  }

  case fdintCLOSE_FILE_INFO: {
    fnFileClose(pfdin->hf);
    SetFileAttributesA(pfdin->psz1, pfdin->attribs);
    return TRUE;
  }

  default:
    return 0;
  }
}

void ExtractCabinetFiles(
  const char* pszCabinetName,
  const char*  pszCabinetPath,
  const char* pszDestinationDir
)
{
  ERF  erf;         //FDI error structure
  HFDI hfdi = NULL; //FDI handle
  BOOL bSuccessful = FALSE;


  hfdi = FDICreate(fnMemAlloc,  //function to allocate memory
    fnMemFree,   //function to free memory
    fnFileOpen,  //function to open a file
    fnFileRead,  //function to read data from a file
    fnFileWrite, //function to write data to a file
    fnFileClose, //function to close a file
    fnFileSeek,  //function to move the file pointer
    cpuUNKNOWN,  //only used by the 16-bit version of FDI
    &erf);       //pointer the FDI error structure

  release_assert(hfdi);


  release_assert(FDICopy(hfdi,   //FDI handle
    (char*)pszCabinetName,       //the cabinet name
    (char*)pszCabinetPath,       //the cabinet path
    0,                           //not used, set to zero
    fnNotify,                    //function for notifications
    NULL,                        //not used, set to NULL
    (char*)pszDestinationDir)); //this value is application-specific

  FDIDestroy(hfdi);
}


static const byte EmperorDat[] = {
  0x6e, 0x3a, 0x76, 0x15, 0xd7, 0xed, 0x16, 0x78, 0x36, 0x0b, 0xef, 0x80, 0x25, 0xf7, 0x5c, 0x19,
  0x50, 0x53, 0x00
};

static const byte WoldataKey[] = {
  0x31, 0x35, 0x35, 0x35, 0x37, 0x31, 0x33, 0x32, 0x37, 0x35, 0x37, 0x30, 0x39, 0x32, 0x33, 0x30,
  0x38, 0x32, 0x32, 0x31, 0x39, 0x37, 0x32, 0x31, 0x32, 0x34, 0x38, 0x37, 0x37, 0x34, 0x30, 0x34,
  0x31, 0x32, 0x32, 0x31, 0x30, 0x31, 0x38, 0x32, 0x32, 0x32, 0x32, 0x30, 0x37, 0x34, 0x31, 0x31,
  0x37, 0x32, 0x39, 0x34, 0x33, 0x30, 0x39, 0x36, 0x39, 0x31, 0x34, 0x38, 0x36, 0x36, 0x33, 0x31,
  0x30, 0x38, 0x30, 0x33, 0x32, 0x36, 0x32, 0x34, 0x33, 0x32, 0x31, 0x31, 0x30, 0x33, 0x32, 0x34,
  0x32, 0x33, 0x31, 0x38, 0x31, 0x39, 0x31, 0x34, 0x32, 0x33, 0x32, 0x33, 0x30, 0x35, 0x32, 0x32,
  0x31, 0x34, 0x33, 0x39, 0x38, 0x36, 0x38, 0x30, 0x36, 0x31, 0x33, 0x36, 0x35, 0x33, 0x31, 0x38,
  0x39, 0x33, 0x37, 0x37, 0x38, 0x39, 0x36, 0x32, 0x35, 0x34, 0x38, 0x31, 0x36, 0x32, 0x36, 0x36,
  0x74, 0x0c, 0x5b
};

static const byte KeysMap[] = {
  0x02, 0x00, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00,
  0x00, 0x04, 0x00, 0x00, 0x00, 0x26, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x28, 0x00, 0x02,
  0x00, 0x06, 0x00, 0x00, 0x00, 0x25, 0x00, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00, 0x27, 0x00, 0x02,
  0x00, 0x08, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x0a, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00,
  0x00, 0x0c, 0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x47, 0x00, 0x00,
  0x00, 0x0e, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x43, 0x00, 0x00,
  0x00, 0x10, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00,
  0x00, 0x12, 0x00, 0x00, 0x00, 0x4e, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00,
  0x00, 0x14, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00,
  0x00, 0x16, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x4b, 0x00, 0x00,
  0x00, 0x18, 0x00, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x2d, 0x00, 0x00,
  0x00, 0x1a, 0x00, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00,
  0x00, 0x1c, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00, 0xdc, 0x00, 0x00,
  0x00, 0x1e, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x51, 0x00, 0x00,
  0x00, 0x20, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00,
  0x00, 0x22, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x49, 0x00, 0x00,
  0x00, 0x24, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00,
  0x00, 0x26, 0x00, 0x00, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00,
  0x00, 0x28, 0x00, 0x00, 0x00, 0x52, 0x00, 0x01, 0x00, 0x29, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00,
  0x00, 0x2a, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00,
  0x00, 0x2c, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x2d, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00,
  0x00, 0x2e, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00,
  0x00, 0x30, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x39, 0x00, 0x00,
  0x00, 0x32, 0x00, 0x00, 0x00, 0x31, 0x00, 0x02, 0x00, 0x33, 0x00, 0x00, 0x00, 0x32, 0x00, 0x02,
  0x00, 0x34, 0x00, 0x00, 0x00, 0x33, 0x00, 0x02, 0x00, 0x35, 0x00, 0x00, 0x00, 0x34, 0x00, 0x02,
  0x00, 0x36, 0x00, 0x00, 0x00, 0x35, 0x00, 0x02, 0x00, 0x37, 0x00, 0x00, 0x00, 0x36, 0x00, 0x02,
  0x00, 0x38, 0x00, 0x00, 0x00, 0x37, 0x00, 0x02, 0x00, 0x39, 0x00, 0x00, 0x00, 0x38, 0x00, 0x02,
  0x00, 0x3a, 0x00, 0x00, 0x00, 0x39, 0x00, 0x02, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x31, 0x00, 0x04,
  0x00, 0x3c, 0x00, 0x00, 0x00, 0x32, 0x00, 0x04, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x33, 0x00, 0x04,
  0x00, 0x3e, 0x00, 0x00, 0x00, 0x34, 0x00, 0x04, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x35, 0x00, 0x04,
  0x00, 0x40, 0x00, 0x00, 0x00, 0x36, 0x00, 0x04, 0x00, 0x41, 0x00, 0x00, 0x00, 0x37, 0x00, 0x04,
  0x00, 0x42, 0x00, 0x00, 0x00, 0x38, 0x00, 0x04, 0x00, 0x43, 0x00, 0x00, 0x00, 0x39, 0x00, 0x04,
  0x00, 0x44, 0x00, 0x00, 0x00, 0x70, 0x00, 0x02, 0x00, 0x45, 0x00, 0x00, 0x00, 0x71, 0x00, 0x02,
  0x00, 0x46, 0x00, 0x00, 0x00, 0x72, 0x00, 0x02, 0x00, 0x47, 0x00, 0x00, 0x00, 0x73, 0x00, 0x02,
  0x00, 0x48, 0x00, 0x00, 0x00, 0x74, 0x00, 0x02, 0x00, 0x49, 0x00, 0x00, 0x00, 0x75, 0x00, 0x02,
  0x00, 0x4a, 0x00, 0x00, 0x00, 0x76, 0x00, 0x02, 0x00, 0x4b, 0x00, 0x00, 0x00, 0x77, 0x00, 0x02,
  0x00, 0x4c, 0x00, 0x00, 0x00, 0x78, 0x00, 0x02, 0x00, 0x4d, 0x00, 0x00, 0x00, 0x79, 0x00, 0x02,
  0x00, 0x4e, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x71, 0x00, 0x00,
  0x00, 0x50, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x73, 0x00, 0x00,
  0x00, 0x52, 0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x75, 0x00, 0x00,
  0x00, 0x54, 0x00, 0x00, 0x00, 0x76, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00,
  0x00, 0x56, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x57, 0x00, 0x00, 0x00, 0x79, 0x00, 0x00,
  0x00, 0x58, 0x00, 0x00, 0x00, 0x70, 0x00, 0x04, 0x00, 0x59, 0x00, 0x00, 0x00, 0x71, 0x00, 0x04,
  0x00, 0x5a, 0x00, 0x00, 0x00, 0x72, 0x00, 0x04, 0x00, 0x5b, 0x00, 0x00, 0x00, 0x73, 0x00, 0x04,
  0x00, 0x5c, 0x00, 0x00, 0x00, 0x74, 0x00, 0x04, 0x00, 0x5d, 0x00, 0x00, 0x00, 0x75, 0x00, 0x04,
  0x00, 0x5e, 0x00, 0x00, 0x00, 0x76, 0x00, 0x04, 0x00, 0x5f, 0x00, 0x00, 0x00, 0x77, 0x00, 0x04,
  0x00, 0x60, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0x00, 0x61, 0x00, 0x00, 0x00, 0x79, 0x00, 0x04,
  0x00, 0x62, 0x00, 0x00, 0x00, 0x56, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0xbd, 0x00, 0x00,
  0x00, 0x64, 0x00, 0x00, 0x00, 0xbb, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00,
  0x00, 0x66, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x42, 0x00, 0x00,
  0x00
};

std::string resourceCfg = R"(MODEL_DATA
data\model

AI_DATA
data\ai

AI_SCRIPTS_DATA
data\ai\scripts

UIDATA
data\ui

AUDIO
data\audio

3DDATA
data\3ddata

MAPS
data\maps

CAMPAIGN_DATA
data\campaign

MISSION_DATA
data\missions

MOVIES
data\movies

MOVIES1
disks\EMPEROR2\Movies

MOVIES2
disks\EMPEROR2\Movies

MOVIES3
disks\EMPEROR3\Movies

MOVIES4
disks\EMPEROR4\Movies

SAVED_GAMES
data\saves

PLAYER_DETAILS
data\details

STRINGS
data\strings

RECORD
data\record

LOGS
logs

SOUNDS
data\sounds

CD1
disks\EMPEROR2

CD2
disks\EMPEROR2

CD3
disks\EMPEROR3

CD4
disks\EMPEROR4
)";

struct DiscInfo
{
  std::string root;   // "D:\"
  std::string label;
  DWORD serial = 0;
};

static std::vector<DiscInfo> listVolumes()
{
  std::vector<DiscInfo> volumes;
  for (char drive = 'A'; drive <= 'Z'; drive++)
  {
    DiscInfo info;
    info.root = std::string(1, drive) + ":\\";
    char volumeNameBuffer[MAX_PATH] = {};

    UINT oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS); // no "insert a disk" popups for empty drives
    BOOL ok = GetVolumeInformationA(info.root.c_str(), volumeNameBuffer, MAX_PATH, &info.serial, NULL, NULL, NULL, 0);
    SetErrorMode(oldErrorMode);

    if (ok)
    {
      info.label = volumeNameBuffer;
      volumes.push_back(info);
    }
  }
  return volumes;
}

static bool isInstallDisc(const std::string& root)
{
  return std::filesystem::exists(root + "INSTALL\\GAME1.CAB");
}

// The house discs carry the in-game music; the game's own CD check looks for exactly this file.
static bool isHouseDisc(const std::string& root)
{
  return std::filesystem::exists(root + "MUSIC.BAG") && !isInstallDisc(root);
}

// Finds one of the four game discs. The English CDs are labelled EMPEROR1..EMPEROR4, so a matching label is
// taken straight away. Localised CDs (e.g. the German "Schlacht um Dune" set) don't necessarily use those
// labels, so we also recognise the discs by their content: disc 1 has INSTALL\GAME1.CAB, the house discs
// have MUSIC.BAG in the root. All house discs look the same from the outside, so for those we ask the user
// which house they inserted. Discs we already copied are skipped by volume serial number.
static DiscInfo getDisc(const std::string& volumeName, const std::string& friendlyName, bool installDisc,
                        std::vector<DWORD>& usedSerials)
{
  auto alreadyUsed = [&](const DiscInfo& d)
  {
    return std::find(usedSerials.begin(), usedSerials.end(), d.serial) != usedSerials.end();
  };

  while (true)
  {
    printf("Searching for %s... ", friendlyName.c_str());

    std::vector<DiscInfo> volumes = listVolumes();

    for (const DiscInfo& d : volumes)
    {
      if (_stricmp(d.label.c_str(), volumeName.c_str()) == 0 && !alreadyUsed(d) &&
          (installDisc ? isInstallDisc(d.root) : isHouseDisc(d.root)))
      {
        printf("Found at %s\n", d.root.c_str());
        usedSerials.push_back(d.serial);
        return d;
      }
    }

    for (const DiscInfo& d : volumes)
    {
      if (alreadyUsed(d) || !(installDisc ? isInstallDisc(d.root) : isHouseDisc(d.root)))
        continue;

      if (!installDisc)
      {
        std::string question = std::string(tr("Found a game disc in drive ", "Spiel-CD gefunden in Laufwerk ")) +
          d.root + " (\"" + d.label + "\").\n\n" +
          tr("Is this ", "Ist das ") + friendlyName + "?";
        if (MessageBoxA(nullptr, question.c_str(), "Emperor Reborn", MB_YESNO | MB_ICONQUESTION) != IDYES)
          continue;
      }

      printf("Found at %s (label \"%s\")\n", d.root.c_str(), d.label.c_str());
      usedSerials.push_back(d.serial);
      return d;
    }

    printf("Not found!\n");
    std::string message = std::string(tr("Please insert / mount ", "Bitte einlegen / einbinden: ")) + friendlyName;
    if (MessageBoxA(nullptr, message.c_str(), "Emperor Reborn", MB_OKCANCEL) == IDCANCEL)
      exit(1);
  }
}

void copyFile(const std::string& source, const std::string& destination)
{
  printf("Copying %s\n", destination.c_str());
  std::filesystem::copy(source, destination);
}

// For the loose files in the disc's INSTALL folder: their set differs a bit between languages (the German CD
// has LIESMICH.* instead of README.*, for example), so a missing one is not an error.
void copyFileIfExists(const std::string& source, const std::string& destination)
{
  if (std::filesystem::exists(source))
    copyFile(source, destination);
  else
    printf("Skipping %s (not on this disc)\n", source.c_str());
}

void copyFolderRecursive(const std::string& source, const std::string& destination)
{
  std::filesystem::create_directories(destination);

  for (const auto& entry : std::filesystem::directory_iterator(source))
  {
    std::string entryPath = entry.path().string();
    std::string entryName = entry.path().filename().string();

    if (entry.is_directory())
      copyFolderRecursive(entryPath, destination + "\\" + entryName);
    else
      copyFile(entryPath, destination + "\\" + entryName);
  }
}

void installEmperor(const std::string& installDir)
{
  std::filesystem::create_directories(installDir);

  std::vector<DWORD> usedSerials;

  // Base install + patch to v1.09
  {
    std::string diskPath = getDisc("EMPEROR1", tr("disc 1 (Install disc)", "CD 1 (Installations-CD)"), true, usedSerials).root;

    copyFolderRecursive(diskPath + "\\INSTALL\\DATA", installDir + "\\DATA");
    copyFile(diskPath + "\\INSTALL\\EMPEROR.EXE", installDir);
    for (const char* name : { "EMPEROR.LCF", "NOTES.ICO", "PATCHGET.DAT", "PATCHW32.DLL", "README.DOC", "README.TXT",
                              "LIESMICH.DOC", "LIESMICH.TXT", "UNINST.EXE", "UNINSTLL.EXE" })
      copyFileIfExists(diskPath + "\\INSTALL\\" + name, installDir);
    if (std::filesystem::exists(diskPath + "\\WOLAPI"))
      copyFile(diskPath + "\\WOLAPI", installDir + "\\WOL");

    FILE* f = fopen((installDir + "\\Emperor.DAT").c_str(), "wb");
    release_assert(f);
    release_assert(fwrite(EmperorDat, 1, sizeof(EmperorDat), f) == sizeof(EmperorDat));
    fclose(f);

    f = fopen((installDir + "\\Woldata.KEY").c_str(), "wb");
    release_assert(f);
    release_assert(fwrite(WoldataKey, 1, sizeof(WoldataKey), f) == sizeof(WoldataKey));
    fclose(f);

    f = fopen((installDir + "\\KEYS.MAP").c_str(), "wb");
    release_assert(f);
    release_assert(fwrite(KeysMap, 1, sizeof(KeysMap), f) == sizeof(KeysMap));
    fclose(f);

    ExtractCabinetFiles("Game1.cab", (diskPath + "\\INSTALL\\").c_str(), installDir.c_str());

    patch(installDir);
  }

  // Atreides data
  {
    std::string diskPath = getDisc("EMPEROR2", tr("disc 2 (Atreides)", "CD 2 (Atreides)"), false, usedSerials).root;
    copyFolderRecursive(diskPath, installDir + "\\disks\\EMPEROR2");
  }

  // Harkonnen data
  {
    std::string diskPath = getDisc("EMPEROR3", tr("disc 3 (Harkonnen)", "CD 3 (Harkonnen)"), false, usedSerials).root;
    copyFolderRecursive(diskPath, installDir + "\\disks\\EMPEROR3");
  }

  // Ordos data
  {
    std::string diskPath = getDisc("EMPEROR4", tr("disc 4 (Ordos)", "CD 4 (Ordos)"), false, usedSerials).root;
    copyFolderRecursive(diskPath, installDir + "\\disks\\EMPEROR4");
  }

  // resource.cfg, patched to point at the disk directories we just created
  {
    FILE* f = fopen((installDir + "\\resource.cfg").c_str(), "w");
    release_assert(f);
    release_assert(fwrite(resourceCfg.data(), 1, resourceCfg.size(), f) == resourceCfg.size());
    fclose(f);
  }
}
