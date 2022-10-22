// d3d9-wrapper-launcher.c
//

/*
 * Launcher for d3d9-wrapper
 *
 * Loads d3d9.dll to application process on launch
 *
 * Description:
 *  Only required for applications which load d3d9.dll dynamically instead of importing it while also
 *  loading it too late for the wrapper to hook the functions required for "ignore focus loss" option
 *
 *  If the application works fine without the launcher, you don't need this
 *
 *  Processor architecture must match, that means using 
 *  x64 launcher with x64 wrapper on x64 apps (64-bit)
 *  x86 launcher with x86 wrapper on x86 apps (32-bit)
 *
 * Additional notes:
 *  It is relatively trivial to modify this so that x64 launcher can launch both x64/x86 apps
 *  and possible but somewhat complicated for x86 launcher (running under WOW64) to also launch both x64/x86 apps
 *  but cba to do that for now so two launchers is what you get
 *
 */

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <io.h>

#define MAX_CMDLINE_LEN 4096 // max command line length

#ifdef _WIN64
#define TITLE_STR "d3d9-wrapper-launcher x64"
#define BIN_ARCH IMAGE_FILE_MACHINE_AMD64
#else
#define TITLE_STR "d3d9-wrapper-launcher x86"
#define BIN_ARCH IMAGE_FILE_MACHINE_I386
#endif

#define ARCHSTR(a) ((a==IMAGE_FILE_MACHINE_I386)?"x86":((a==IMAGE_FILE_MACHINE_IA64)?"ia64":((a==IMAGE_FILE_MACHINE_AMD64)?"x64":"unknown")))

BOOL FileExistsA(const char* fileName)
{
    DWORD fileAttr;
    fileAttr = GetFileAttributesA(fileName);
    if (INVALID_FILE_ATTRIBUTES == fileAttr) // 0xFFFFFFFF (-1)
    {
        switch (GetLastError())
        {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_NAME:
            case ERROR_INVALID_DRIVE:
            case ERROR_NOT_READY:
            case ERROR_INVALID_PARAMETER:
            case ERROR_BAD_PATHNAME:
            case ERROR_BAD_NETPATH:
                return FALSE;
            default:
                break;
        }
    }
    return TRUE;
}

char* trim(char* s)
{
    unsigned int i,x;
    i = (unsigned int)strlen(s)-1;
    while (i > 0 && s[i] != 0 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
       i--;
    }
    if (i == 0) {
        s[0] = 0;
        return s;
    }
    s[i+1] = 0;
    i = 0;
    x = 0;
    while (s[x] != 0 && (s[x] == ' ' || s[x] == '\t' || s[x] == '\r' || s[x] == '\n')) {
       x++;
    }
    if (x == 0)  {
        return s;
    }
    while (s[x] != 0) {
        s[i] = s[x];
        i++;
        x++;
    }
    s[i] = 0;
    return s;
}

char* strip_quotes(char* s)
{
    int i,x;
    i = 0;
    x = 0;
    while (s[x] != 0) {
        while (s[x] != 0 && s[x] == '\"') {
           x++;
        }
        s[i] = s[x];
        i++;
        x++;
    }
    s[i] = 0;
    return s;
}

void print_w32error(char* cmd, char* arg, DWORD dwErr)
{
    char* err_str = NULL;
    unsigned int len;
    DWORD_PTR pArgs[] = { (DWORD_PTR)"arg1", (DWORD_PTR)"arg2", (DWORD_PTR)"arg3", (DWORD_PTR)"arg4", (DWORD_PTR)"arg5", (DWORD_PTR)"arg6" };
    if (arg) {
        pArgs[0] = (DWORD_PTR)arg;
    }
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY, NULL, dwErr, 0, (LPSTR)&err_str, 0, (va_list*)pArgs)) {
        len = (unsigned int)strlen(err_str);
        if (len > 1) {
            err_str[len-2] = 0;
            fprintf(stderr, TITLE_STR "\n%s() failed; error = (0x%X) \'%s\'\n", cmd, dwErr, err_str);
        }
        LocalFree(err_str);
        if (len > 1) {
            return;
        }
    }
    fprintf(stderr, TITLE_STR "\n%s() failed; error code = 0x%08X%s%s%s\n", cmd, dwErr, arg?"; arg = \'":"", arg?arg:"",arg?"\'":"");
}

int pause_on_exit(int ec)
{
    if(_isatty(_fileno(stdin))) {
        puts("\nPress any key to exit ...");
        _getch();
    }
    return ec;
}

WORD GetFileArch(char* filePath)
{
    HANDLE hFile = NULL;
    HANDLE hMapping = NULL;
    LPVOID img_addr_header;
    PIMAGE_DOS_HEADER img_dos_headers;
    PIMAGE_NT_HEADERS img_nt_headers;

    hFile = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (!hFile || hFile == (HANDLE)-1) {
        print_w32error("CreateFile", filePath, GetLastError());
        return 0;
    }
    hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, 0);
    if (!hMapping || hMapping == (HANDLE)-1) {
        print_w32error("CreateFileMapping", filePath, GetLastError());
        CloseHandle(hFile);
        return 0;
    }
    img_addr_header = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!img_addr_header) {
        print_w32error("MapViewOfFile", filePath, GetLastError());
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }
    img_dos_headers = (PIMAGE_DOS_HEADER)img_addr_header;
    if (img_dos_headers->e_magic != IMAGE_DOS_SIGNATURE) {
        fprintf(stderr, TITLE_STR "\nFile \'%s\' is not a valid Win32 application\n", filePath);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }
    img_nt_headers = (PIMAGE_NT_HEADERS)((BYTE*)img_dos_headers + img_dos_headers->e_lfanew);
    if (img_nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        fprintf(stderr, TITLE_STR "\nFile \'%s\' is not a valid Win32 application\n", filePath);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return img_nt_headers->FileHeader.Machine;
}

int main(int argc, char **argv) {
	int i;
    int len;
    int c;
	void *page;
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	HANDLE hThread;
    WORD wArchApp = 0;
    WORD wArchDLL = 0;
    char dll_path[MAX_PATH+1];
    char ini_path[512];
    char exe_path[512];
    char cmdline[MAX_CMDLINE_LEN];

    if (!GetModuleFileNameA(NULL, dll_path, MAX_PATH)) {
        print_w32error("GetModuleFileNameA", NULL, GetLastError());
        return pause_on_exit(1);
    }
    i = strrchr(dll_path, '\\')-dll_path+1;
    len = i+9;
    if (len > MAX_PATH) {
        fprintf(stderr, TITLE_STR "\npath length (%d) exceeds MAX_PATH (%d).\n", len, MAX_PATH);
        return pause_on_exit(1);
    }

    strncpy(ini_path, dll_path, i);
    strcpy(ini_path+i, "d3d9.ini");
    strcpy(dll_path+i, "d3d9.dll");

    if (!FileExistsA(dll_path)) {
        fprintf(stderr, TITLE_STR "\nd3d9-wrapper not found, path \'%s\' does not exist\n", dll_path);
        return pause_on_exit(1);
    }

    exe_path[0] = 0;
    if (argc < 2) {
        GetPrivateProfileString("LAUNCHER", "AppExe", NULL, exe_path, sizeof(exe_path)-1, ini_path);
        if (exe_path[0] == 0) {
            fprintf(stderr, TITLE_STR "\nUsage: d3d9-wrapper-launcher EXE [ARGS]\n");
            return pause_on_exit(1);
        }
    }
    else {
        strncpy(exe_path, argv[1], sizeof(exe_path)-2);
    }

    exe_path[sizeof(exe_path)-1] = 0;
    trim(exe_path);
    strip_quotes(exe_path);

    if (!FileExistsA(exe_path)) {
        fprintf(stderr, TITLE_STR "\nAppExe not found, path \'%s\' does not exist\n", exe_path);
        return pause_on_exit(1);
    }

    wArchDLL = GetFileArch(dll_path);
    if (!wArchDLL) {
        return pause_on_exit(1);
    }

    wArchApp = GetFileArch(exe_path);
    if (!wArchApp) {
        return pause_on_exit(1);
    }

    if (wArchDLL != BIN_ARCH || wArchApp != BIN_ARCH) {
        fprintf(stderr, TITLE_STR "\nProcessor architecture mismatch !\n"
            "Launcher   = \"%s\"\n"
            "WrapperDLL = \"%s\" (\'%s\')\n"
            "AppExe     = \"%s\" (\'%s\')\n",
            ARCHSTR(BIN_ARCH), ARCHSTR(wArchDLL), dll_path, ARCHSTR(wArchApp), exe_path);
        return pause_on_exit(1);
    }

    cmdline[sizeof(cmdline)-1] = 0;
    cmdline[0] = '"';
    strcpy(cmdline+1, exe_path);
    i = (int)strlen(cmdline);
    cmdline[i++] = '"';
    cmdline[i] = 0;

    if (argc < 2) {
        cmdline[i++] = ' ';
        cmdline[i] = 0;
        GetPrivateProfileString("LAUNCHER", "AppArgs", NULL, cmdline+i, sizeof(cmdline)-i-1, ini_path);
    }
    else if (argc > 2) {
        for (c = 2; c < argc; c++)
        {
            if (sizeof(cmdline)-i < 5) {
                fprintf(stderr, TITLE_STR "\ncommand line length exceeds MAX_CMDLINE_LEN (%d).\n", MAX_CMDLINE_LEN);
                return pause_on_exit(1);
            }
            cmdline[i++] = ' ';
            cmdline[i] = 0;
            strncpy(cmdline+i, argv[c], sizeof(cmdline)-i-2);
            i = (int)strlen(cmdline);
        }
    }

    si.cb = sizeof(STARTUPINFO);
    if (!CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        print_w32error("CreateProcess", cmdline, GetLastError());
        return pause_on_exit(1);
    }

    page = VirtualAllocEx(pi.hProcess, NULL, MAX_PATH, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!page) {
        print_w32error("VirtualAllocEx", NULL, GetLastError());
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pause_on_exit(1);
    }

    if (!WriteProcessMemory(pi.hProcess, page, dll_path, len, NULL)) {
        print_w32error("WriteProcessMemory", NULL, GetLastError());
        VirtualFreeEx(pi.hProcess, page, 0, MEM_RELEASE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pause_on_exit(1);
    }

    hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, page, 0, NULL);
    if (!hThread) {
        print_w32error("CreateRemoteThread", NULL, GetLastError());
        VirtualFreeEx(pi.hProcess, page, 0, MEM_RELEASE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pause_on_exit(1);
    }

    if (WaitForSingleObject(hThread, INFINITE) == WAIT_FAILED) {
        print_w32error("WaitForSingleObject", NULL, GetLastError());
        CloseHandle(hThread);
        VirtualFreeEx(pi.hProcess, page, 0, MEM_RELEASE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pause_on_exit(1);
    }

    CloseHandle(hThread);

    if (ResumeThread(pi.hThread) == -1) {
        print_w32error("ResumeThread", NULL, GetLastError());
        VirtualFreeEx(pi.hProcess, page, 0, MEM_RELEASE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pause_on_exit(1);
    }

    VirtualFreeEx(pi.hProcess, page, 0, MEM_RELEASE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
