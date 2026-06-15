#pragma once
#include <Windows.h>

#define HOTKEY_ID 1
#define DEFAULT_HOTKEY_MOD (MOD_CONTROL | MOD_SHIFT)
#define DEFAULT_HOTKEY_KEY 'C'

extern UINT hotkeyModifiers;
extern UINT hotkeyVirtualKey;
extern HWND mainHwnd;
extern BOOL hotkeyRegistered;
extern WNDPROC originalWndProc;
extern char hotkeyConfigStr[64];

void parseHotkeyConfig(const char* hotkeyStr);
void formatHotkeyString(char* buf, size_t bufSize);
LRESULT CALLBACK hotkeyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
