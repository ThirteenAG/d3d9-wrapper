#ifndef __HELPERS_H
#define __HELPERS_H

#include <windows.h>

typedef BOOL(WINAPI* LPFN_ISTOPLEVELWINDOW)(HWND);
LPFN_ISTOPLEVELWINDOW fnIsTopLevelWindow = NULL;

BOOL _fnIsTopLevelWindow(HWND hWnd)
{
    /*  IsTopLevelWindow is not available on all versions of Windows.
     *  Use GetModuleHandle to get a handle to the DLL that contains the function
     *  and GetProcAddress to get a pointer to the function if available.
     */
    if (fnIsTopLevelWindow == NULL) {
        fnIsTopLevelWindow = (LPFN_ISTOPLEVELWINDOW)GetProcAddress(GetModuleHandleA("user32"), "IsTopLevelWindow");
        if (fnIsTopLevelWindow == NULL) {
            fnIsTopLevelWindow = (LPFN_ISTOPLEVELWINDOW)-1;
        }
    }
    if (fnIsTopLevelWindow != (LPFN_ISTOPLEVELWINDOW)-1) {
        return fnIsTopLevelWindow(hWnd);
    }
    /* If no avail, use older method which is available in Win2000+ */
    return (GetAncestor(hWnd, GA_ROOT) == hWnd);
}

BOOL IsValueIntAtom(DWORD dw)
{
    return (HIWORD(dw) == 0 && LOWORD(dw) < 0xC000);
}

BOOL IsSystemClassNameA(LPCSTR classNameA)
{
    if (!lstrcmpiA(classNameA, "BUTTON"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "COMBOBOX"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "EDIT"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "LISTBOX"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "MDICLIENT"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "RICHEDIT"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "RICHEDIT_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "SCROLLBAR"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "STATIC"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "ANIMATE_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "DATETIMEPICK_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "HOTKEY_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "LINK_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "MONTHCAL_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "NATIVEFNTCTL_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "PROGRESS_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "REBARCLASSNAME"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "STANDARD_CLASSES"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "STATUSCLASSNAME"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "TOOLBARCLASSNAME"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "TOOLTIPS_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "TRACKBAR_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "UPDOWN_CLASS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_BUTTON"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_COMBOBOX"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_COMBOBOXEX"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_EDIT"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_HEADER"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_LISTBOX"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_IPADDRESS"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_LINK"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_LISTVIEW"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_NATIVEFONTCTL"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_PAGESCROLLER"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_SCROLLBAR"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_STATIC"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_TABCONTROL"))
        return TRUE;
    if (!lstrcmpiA(classNameA, "WC_TREEVIEW"))
        return TRUE;
    return FALSE;
}

BOOL IsSystemClassNameW(LPCWSTR classNameW)
{
    if (!lstrcmpiW(classNameW, L"BUTTON"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"COMBOBOX"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"EDIT"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"LISTBOX"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"MDICLIENT"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"RICHEDIT"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"RICHEDIT_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"SCROLLBAR"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"STATIC"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"ANIMATE_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"DATETIMEPICK_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"HOTKEY_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"LINK_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"MONTHCAL_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"NATIVEFNTCTL_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"PROGRESS_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"REBARCLASSNAME"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"STANDARD_CLASSES"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"STATUSCLASSNAME"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"TOOLBARCLASSNAME"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"TOOLTIPS_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"TRACKBAR_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"UPDOWN_CLASS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_BUTTON"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_COMBOBOX"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_COMBOBOXEX"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_EDIT"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_HEADER"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_LISTBOX"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_IPADDRESS"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_LINK"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_LISTVIEW"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_NATIVEFONTCTL"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_PAGESCROLLER"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_SCROLLBAR"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_STATIC"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_TABCONTROL"))
        return TRUE;
    if (!lstrcmpiW(classNameW, L"WC_TREEVIEW"))
        return TRUE;
    return FALSE;
}

#endif //__HELPERS_H