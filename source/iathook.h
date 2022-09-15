#ifndef __IATHOOK_H
#define __IATHOOK_H

#include <windows.h>
#include <stdint.h>

/*
 * simple iathook, unreleased version
 */
#ifdef __cplusplus
namespace Iat_hook
{
#endif
    void** find_iat_func(const char* function, HMODULE hModule, const char* chModule, const DWORD ordinal)
    {
        if (!hModule)
            hModule = GetModuleHandle(0);

        PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)hModule;
        PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((BYTE*)img_dos_headers + img_dos_headers->e_lfanew);
        PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        //if (img_dos_headers->e_magic != IMAGE_DOS_SIGNATURE)
            //printf("ERROR: e_magic is not a valid DOS signature\n");

        for (IMAGE_IMPORT_DESCRIPTOR* iid = img_import_desc; iid->Name != 0; iid++) {
            if (chModule != NULL)
            {
                char* mod_name = (char*)((size_t*)(iid->Name + (size_t)hModule));
                if (lstrcmpiA(chModule, mod_name))
                    continue;
            }
            for (int func_idx = 0; *(func_idx + (void**)(iid->FirstThunk + (size_t)hModule)) != NULL; func_idx++) {
                size_t mod_func_ptr_ord = (size_t)(*(func_idx + (size_t*)(iid->OriginalFirstThunk + (size_t)hModule)));
                char* mod_func_name = (char*)(mod_func_ptr_ord + (size_t)hModule + 2);
                const intptr_t nmod_func_name = (intptr_t)mod_func_name;
                if (nmod_func_name >= 0) {
                    //printf("%s %s\n", mod_name, mod_func_name);
                    if (function != NULL && !lstrcmpA(function, mod_func_name))
                        return func_idx + (void**)(iid->FirstThunk + (size_t)hModule);
                }
                else if (IMAGE_SNAP_BY_ORDINAL(mod_func_ptr_ord))
                {
                    //printf("%s @%u\n", mod_name, IMAGE_ORDINAL(mod_func_ptr_ord));
                    if (chModule != NULL && ordinal != 0 && (ordinal == IMAGE_ORDINAL(mod_func_ptr_ord)))
                        return func_idx + (void**)(iid->FirstThunk + (size_t)hModule);
                }
            }
        }
        return 0;
    }

    uintptr_t detour_iat_ptr(const char* function, void* newfunction, HMODULE hModule = NULL, const char* chModule = NULL, const DWORD ordinal = 0)
    {
        void** func_ptr = find_iat_func(function, hModule, chModule, ordinal);
        if (!func_ptr || *func_ptr == newfunction || *func_ptr == NULL)
            return 0;

        DWORD old_rights, new_rights = PAGE_READWRITE;
        VirtualProtect(func_ptr, sizeof(uintptr_t), new_rights, &old_rights);
        uintptr_t ret = (uintptr_t)*func_ptr;
        *func_ptr = newfunction;
        VirtualProtect(func_ptr, sizeof(uintptr_t), old_rights, &new_rights);
        return ret;
    }
#ifdef __cplusplus
};
#endif

#endif //__IATHOOK_H