#pragma once
#include <windows.h>
#include "iup.h"

// Creates and returns the Process Filter and Session Timer horizontal box container
Ihandle* uiCreateProcessFilterFrame(void);

// Returns TRUE if the process filter toggle is checked (enabled)
BOOL uiIsProcessFilterEnabled(void);

// Sets the process filter toggle state
void uiSetProcessFilterEnabled(BOOL enabled);

// Retrieves the process target name entered in the text box
const char* uiGetProcessFilterTarget(void);

// Sets the process target name in the UI text box
void uiSetProcessFilterTarget(const char *target);

// Enables or disables the process filter UI input controls
void uiSetProcessFilterActive(BOOL active);

// Returns TRUE if the duration timer toggle is checked (enabled)
BOOL uiIsDurationEnabled(void);

// Sets the duration timer toggle state
void uiSetDurationEnabled(BOOL enabled);

// Retrieves the duration value entered in the text box (in milliseconds).
// Enforces validation and falls back to 1000ms if the input is invalid.
int uiGetDurationValue(void);

// Sets the duration value in the UI text box
void uiSetDurationValue(int durationMs);
