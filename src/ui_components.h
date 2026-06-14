#pragma once
#include <windows.h>
#include "iup.h"

// Creates and returns the Process Filter IUP Frame widget
Ihandle* uiCreateProcessFilterFrame(void);

// Returns TRUE if the process filter toggle is checked (enabled)
BOOL uiIsProcessFilterEnabled(void);

// Retrieves the process target name entered in the text box
const char* uiGetProcessFilterTarget(void);

// Enables or disables the process filter UI input controls
void uiSetProcessFilterActive(BOOL active);

// Returns TRUE if the duration timer toggle is checked (enabled)
BOOL uiIsDurationEnabled(void);

// Retrieves the duration value entered in the text box (in seconds)
int uiGetDurationValue(void);
