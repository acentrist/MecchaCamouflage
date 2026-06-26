#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <string>

namespace
{
    auto find_process_id(const std::wstring& process_name) -> DWORD
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        DWORD pid = 0;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, process_name.c_str()) == 0)
                {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return pid;
    }

    auto inject_dll(DWORD pid, const std::wstring& dll_path) -> int
    {
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                     FALSE,
                                     pid);
        if (!process)
        {
            std::wcerr << L"OpenProcess failed win32=" << GetLastError() << L"\n";
            return 2;
        }

        const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
        LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!remote)
        {
            std::wcerr << L"VirtualAllocEx failed win32=" << GetLastError() << L"\n";
            CloseHandle(process);
            return 3;
        }

        if (!WriteProcessMemory(process, remote, dll_path.c_str(), bytes, nullptr))
        {
            std::wcerr << L"WriteProcessMemory failed win32=" << GetLastError() << L"\n";
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            return 4;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
        if (!thread)
        {
            std::wcerr << L"CreateRemoteThread failed win32=" << GetLastError() << L"\n";
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            return 5;
        }

        WaitForSingleObject(thread, 10000);
        DWORD remote_exit = 0;
        GetExitCodeThread(thread, &remote_exit);
        CloseHandle(thread);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);

        if (remote_exit == 0)
        {
            std::wcerr << L"LoadLibraryW failed in target process\n";
            return 6;
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3)
    {
        std::wcerr << L"usage: runtime-injector.exe <process.exe> <bridge.dll>\n";
        return 64;
    }
    const std::wstring process_name = argv[1];
    const std::wstring dll_path = argv[2];
    const DWORD pid = find_process_id(process_name);
    if (!pid)
    {
        std::wcerr << L"process not found: " << process_name << L"\n";
        return 1;
    }
    const int result = inject_dll(pid, dll_path);
    if (result == 0)
    {
        std::wcout << L"injected pid=" << pid << L" dll=" << dll_path << L"\n";
    }
    return result;
}
