#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <Windows.h>
#include "common.h"
#include "hotkey_manager.h"
#include "ui_callbacks.h"

UINT hotkeyModifiers = DEFAULT_HOTKEY_MOD;
UINT hotkeyVirtualKey = DEFAULT_HOTKEY_KEY;
HWND mainHwnd = NULL;
BOOL hotkeyRegistered = FALSE;
WNDPROC originalWndProc = NULL;
char hotkeyConfigStr[64] = "";

// Parse hotkey configuration string like "ctrl+shift+c" or "alt+f10"
void parseHotkeyConfig(const char* hotkeyStr) {
    char buf[64];
    char *token, *saveptr;
    UINT mods = 0;
    UINT key = 0;
    
    if (!hotkeyStr || strlen(hotkeyStr) == 0) {
        LOG("No hotkey config, using default");
        return;
    }
    
    strncpy(buf, hotkeyStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Convert to lowercase for easier parsing
    for (char *p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    
    // Parse tokens separated by +
    token = strtok_s(buf, "+", &saveptr);
    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        
        // Check for modifiers
        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            mods |= MOD_CONTROL;
        } else if (strcmp(token, "alt") == 0) {
            mods |= MOD_ALT;
        } else if (strcmp(token, "shift") == 0) {
            mods |= MOD_SHIFT;
        } else if (strcmp(token, "win") == 0) {
            mods |= MOD_WIN;
        }
        // Check for function keys F1-F12
        else if (token[0] == 'f' && strlen(token) <= 3) {
            int fnum = atoi(token + 1);
            if (fnum >= 1 && fnum <= 12) {
                key = VK_F1 + (fnum - 1);
            }
        }
        // Check for single letter a-z
        else if (strlen(token) == 1 && token[0] >= 'a' && token[0] <= 'z') {
            key = 'A' + (token[0] - 'a'); // VK codes are uppercase
        }
        // Check for number 0-9
        else if (strlen(token) == 1 && token[0] >= '0' && token[0] <= '9') {
            key = '0' + (token[0] - '0');
        }
        
        token = strtok_s(NULL, "+", &saveptr);
    }
    
    // Only update if we got a valid key
    if (key != 0) {
        hotkeyModifiers = mods;
        hotkeyVirtualKey = key;
        LOG("Hotkey configured: mods=0x%x key=0x%x", mods, key);
    } else {
        LOG("Invalid hotkey config '%s', using default", hotkeyStr);
    }
}

// Format hotkey as human-readable string (e.g., "Ctrl+Shift+C")
void formatHotkeyString(char* buf, size_t bufSize) {
    char keyName[32] = "";
    size_t pos = 0;
    buf[0] = '\0';
    
    // Build modifier string
    if (hotkeyModifiers & MOD_CONTROL) {
        pos += snprintf(buf + pos, bufSize - pos, "Ctrl+");
    }
    if (hotkeyModifiers & MOD_ALT) {
        pos += snprintf(buf + pos, bufSize - pos, "Alt+");
    }
    if (hotkeyModifiers & MOD_SHIFT) {
        pos += snprintf(buf + pos, bufSize - pos, "Shift+");
    }
    if (hotkeyModifiers & MOD_WIN) {
        pos += snprintf(buf + pos, bufSize - pos, "Win+");
    }
    
    // Format key name
    if (hotkeyVirtualKey >= VK_F1 && hotkeyVirtualKey <= VK_F12) {
        snprintf(keyName, sizeof(keyName), "F%d", hotkeyVirtualKey - VK_F1 + 1);
    } else if (hotkeyVirtualKey >= 'A' && hotkeyVirtualKey <= 'Z') {
        snprintf(keyName, sizeof(keyName), "%c", (char)hotkeyVirtualKey);
    } else if (hotkeyVirtualKey >= '0' && hotkeyVirtualKey <= '9') {
        snprintf(keyName, sizeof(keyName), "%c", (char)hotkeyVirtualKey);
    } else {
        snprintf(keyName, sizeof(keyName), "0x%X", hotkeyVirtualKey);
    }
    
    snprintf(buf + pos, bufSize - pos, "%s", keyName);
}

// Subclassed window procedure to handle WM_HOTKEY messages
LRESULT CALLBACK hotkeyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY && wParam == HOTKEY_ID) {
        LOG("Hotkey pressed, toggling filtering");
        toggleFiltering();
        return 0;
    }
    // Call original window procedure for all other messages
    return CallWindowProc(originalWndProc, hWnd, msg, wParam, lParam);
}
