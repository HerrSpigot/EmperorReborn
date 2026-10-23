#include <windows.h>
#include <cstdio>
#include <vector>
#include "GameExeCheck.hpp"
#include "../EmperorHooks/DebugLogPatchTable.hpp"

static std::vector<uint8_t> readWholeFile(const std::string& path)
{
  std::vector<uint8_t> data;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f)
    return data;

  if (fseek(f, 0, SEEK_END) == 0)
  {
    long size = ftell(f);
    if (size > 0 && fseek(f, 0, SEEK_SET) == 0)
    {
      data.resize(size);
      if (fread(data.data(), 1, size, f) != size_t(size))
        data.clear();
    }
  }

  fclose(f);
  return data;
}

// Translate a virtual address (ImageBase 0x400000) into an offset in the exe file, or -1 if it isn't backed
// by raw data in any section.
static long long vaToFileOffset(const std::vector<uint8_t>& file, DWORD va)
{
  if (file.size() < sizeof(IMAGE_DOS_HEADER))
    return -1;

  const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > file.size())
    return -1;

  const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
    return -1;

  DWORD rva = va - nt->OptionalHeader.ImageBase;
  const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
  if (reinterpret_cast<const uint8_t*>(sections + nt->FileHeader.NumberOfSections) > file.data() + file.size())
    return -1;

  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
  {
    const IMAGE_SECTION_HEADER& s = sections[i];
    if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.SizeOfRawData)
    {
      long long offset = (long long)s.PointerToRawData + (rva - s.VirtualAddress);
      return offset < (long long)file.size() ? offset : -1;
    }
  }

  return -1;
}

GameExeLayout checkGameExeLayout(const std::string& gameExePath)
{
  std::vector<uint8_t> file = readWholeFile(gameExePath);
  if (file.empty())
    return GameExeLayout::Missing;

  for (const PatchElement& element : debugLogPatches)
  {
    long long offset = vaToFileOffset(file, element.address);
    if (offset < 0 || file[size_t(offset)] != element.original)
      return GameExeLayout::Incompatible;
  }

  return GameExeLayout::Compatible;
}
