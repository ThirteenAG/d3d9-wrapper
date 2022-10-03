#ifndef __IATHOOK_H
#define __IATHOOK_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
namespace Iat_hook
{
#endif
    void** find_iat_func(const char* function, HMODULE hModule, const char* chModule, const DWORD ordinal)
    {
        if (!hModule)
            hModule = GetModuleHandle(nullptr);
        
        const DWORD_PTR instance = reinterpret_cast<DWORD_PTR>(GetModuleHandle(nullptr));
        const PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(instance + reinterpret_cast<PIMAGE_DOS_HEADER>(instance)->e_lfanew);
        PIMAGE_IMPORT_DESCRIPTOR pImports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(instance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        __try
        {
            for (; pImports->Name != 0; pImports++)
            {
                auto mod_name = reinterpret_cast<const char*>((size_t*)(pImports->Name + (size_t)hModule));
                if (_stricmp(reinterpret_cast<const char*>(instance + pImports->Name), mod_name) == 0)
                {
                    if (pImports->OriginalFirstThunk != 0)
                    {
                        const PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);
                        for (ptrdiff_t j = 0; pThunk[j].u1.AddressOfData != 0; j++)
                        {
                            if (strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pThunk[j].u1.AddressOfData)->Name, function) == 0)
                            {
                                void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
                                return pAddress;
                            }
                        }
                    }
                    else
                    {
                        void** pFunctions = reinterpret_cast<void**>(instance + pImports->FirstThunk);
                        for (ptrdiff_t j = 0; pFunctions[j] != nullptr; j++)
                        {
                            if (pFunctions[j] == GetProcAddress(GetModuleHandle(mod_name), function))
                            {
                                return &pFunctions[j];
                            }
                        }
                    }
                }
            }
        }
        __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
        }

        __try
        {
            for (IMAGE_IMPORT_DESCRIPTOR* iid = pImports; iid->Name != 0; iid++) {
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
                        if (function != NULL && !lstrcmpA(function, mod_func_name))
                            return func_idx + (void**)(iid->FirstThunk + (size_t)hModule);
                    }
                    else if (IMAGE_SNAP_BY_ORDINAL(mod_func_ptr_ord))
                    {
                        if (chModule != NULL && ordinal != 0 && (ordinal == IMAGE_ORDINAL(mod_func_ptr_ord)))
                            return func_idx + (void**)(iid->FirstThunk + (size_t)hModule);
                    }
                }
            }
        }
        __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
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