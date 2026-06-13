#pragma once
#include <windows.h>

// Initialize synchronization events and spawn the background worker thread
void processFilterInit(void);

// Clean up events and terminate the background worker thread
void processFilterCleanup(void);

// Trigger the background worker thread to perform process lookup and generate the filter string
// Returns TRUE if successful, FALSE if timed out or failed
BOOL processFilterTrigger(const char *processNameSub);

// Retrieve the dynamically generated WinDivert filter expression string
const char* processFilterGetExpression(void);
