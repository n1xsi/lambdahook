#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

static DWORD find_process(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                CloseHandle(snap);
                return pe.th32ProcessID;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* dll_path;

    if (argc >= 2) {
        dll_path = argv[1];
    } else {
        dll_path = "C:\\Users\\Administrator\\Documents\\.Projects\\C++\\dod-cheat\\build\\bin\\libdodcheat.dll";
    }

    printf("Looking for hl.exe...\n");
    DWORD pid = find_process("hl.exe");
    if (!pid) {
        printf("ERROR: hl.exe not found. Launch the game first.\n");
        return 1;
    }
    printf("Found hl.exe (PID %lu)\n", pid);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        printf("OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    size_t path_len = strlen(dll_path) + 1;
    void* remote_buf = VirtualAllocEx(hProc, NULL, path_len,
                                      MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!remote_buf) {
        printf("VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(hProc);
        return 1;
    }

    if (!WriteProcessMemory(hProc, remote_buf, dll_path, path_len, NULL)) {
        printf("WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote_buf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void* pLoadLibraryA = (void*)GetProcAddress(k32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibraryA, remote_buf, 0, NULL);
    if (!hThread) {
        printf("CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote_buf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exit_code = 0;
    GetExitCodeThread(hThread, &exit_code);

    if (exit_code == 0) {
        printf("ERROR: LoadLibraryA returned NULL! DLL failed to load.\n");
    } else {
        printf("SUCCESS: DLL injected at 0x%lX\n", exit_code);
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote_buf, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return 0;
}
