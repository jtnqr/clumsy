#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ui_components.h"
#include "common.h"
#include "ui_callbacks.h"


static Ihandle *processFilterFrame = NULL;
static Ihandle *processFilterToggle = NULL;
static Ihandle *processFilterText = NULL;
static Ihandle *processFilterLabel = NULL;
static Ihandle *processFilterDurationLabel = NULL;
static Ihandle *processFilterDurationText = NULL;
static Ihandle *processFilterDurationToggle = NULL;

Ihandle* uiCreateProcessFilterFrame(void) {
    Ihandle *sessionTimerFrame;
    Ihandle *processFilterHbox, *sessionTimerHbox, *masterHbox;

    processFilterFrame = IupFrame(
        processFilterHbox = IupHbox(
            processFilterLabel = IupLabel("Target Process:"),
            processFilterText = IupText(NULL),
            processFilterToggle = IupToggle("Enable Process Filtering", NULL),
            NULL
        )
    );
    IupSetAttribute(processFilterFrame, "TITLE", "Process Filter");
    IupSetAttribute(processFilterHbox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(processFilterHbox, "MARGIN", "6x6");
    IupSetAttribute(processFilterHbox, "GAP", "6");

    sessionTimerFrame = IupFrame(
        sessionTimerHbox = IupHbox(
            processFilterDurationLabel = IupLabel("Duration:"),
            processFilterDurationText = IupText(NULL),
            IupLabel("ms"),
            processFilterDurationToggle = IupToggle("Enable Duration Limit", NULL),
            NULL
        )
    );
    IupSetAttribute(sessionTimerFrame, "TITLE", "Session Timer");
    IupSetAttribute(sessionTimerHbox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(sessionTimerHbox, "MARGIN", "6x6");
    IupSetAttribute(sessionTimerHbox, "GAP", "6");

    masterHbox = IupHbox(
        processFilterFrame,
        IupFill(),
        sessionTimerFrame,
        NULL
    );
    IupSetAttribute(masterHbox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(masterHbox, "ALIGNMENT", "ACENTER");

    IupSetAttribute(processFilterText, "VISIBLECOLUMNS", "10");
    IupSetAttribute(processFilterText, "VALUE", "");
    IupSetAttribute(processFilterToggle, "VALUE", "OFF");
    IupSetAttribute(processFilterDurationText, "VISIBLECOLUMNS", "5");
    IupSetAttribute(processFilterDurationText, "VALUE", "10");
    IupSetAttribute(processFilterDurationToggle, "VALUE", "OFF");

    IupSetAttribute(processFilterText, PARAM_KEY, "process-filter-target");
    IupSetAttribute(processFilterToggle, PARAM_KEY, "process-filter-enabled");
    IupSetAttribute(processFilterDurationText, PARAM_KEY, "process-filter-duration");
    IupSetAttribute(processFilterDurationToggle, PARAM_KEY, "process-filter-duration-enabled");

    IupSetCallback(processFilterText, "VALUECHANGED_CB", (Icallback)uiProcessFilterChangeCb);
    IupSetCallback(processFilterToggle, "ACTION", (Icallback)uiProcessFilterToggleCb);
    IupSetCallback(processFilterDurationText, "VALUECHANGED_CB", (Icallback)uiProcessFilterChangeCb);
    IupSetCallback(processFilterDurationToggle, "ACTION", (Icallback)uiProcessFilterToggleCb);

    IupSetHandle("process_filter_text", processFilterText);
    IupSetHandle("process_filter_toggle", processFilterToggle);
    IupSetHandle("process_filter_duration_text", processFilterDurationText);
    IupSetHandle("process_filter_duration_toggle", processFilterDurationToggle);


    // Restore from IupGlobal if loaded from state
    if (IupGetGlobal("process-filter-target")) {
        IupStoreAttribute(processFilterText, "VALUE", IupGetGlobal("process-filter-target"));
    }
    if (IupGetGlobal("process-filter-enabled")) {
        const char *enabled = IupGetGlobal("process-filter-enabled");
        if (strcmp(enabled, "on") == 0) {
            IupSetAttribute(processFilterToggle, "VALUE", "ON");
        } else {
            IupSetAttribute(processFilterToggle, "VALUE", "OFF");
        }
    }
    if (IupGetGlobal("process-filter-duration")) {
        IupStoreAttribute(processFilterDurationText, "VALUE", IupGetGlobal("process-filter-duration"));
    }
    if (IupGetGlobal("process-filter-duration-enabled")) {
        const char *enabled = IupGetGlobal("process-filter-duration-enabled");
        if (strcmp(enabled, "on") == 0) {
            IupSetAttribute(processFilterDurationToggle, "VALUE", "ON");
        } else {
            IupSetAttribute(processFilterDurationToggle, "VALUE", "OFF");
        }
    }

    return masterHbox;
}

BOOL uiIsProcessFilterEnabled(void) {
    if (!processFilterToggle) return FALSE;
    return IupGetInt(processFilterToggle, "VALUE");
}

void uiSetProcessFilterEnabled(BOOL enabled) {
    if (processFilterToggle) {
        IupSetAttribute(processFilterToggle, "VALUE", enabled ? "ON" : "OFF");
    }
}

const char* uiGetProcessFilterTarget(void) {
    if (!processFilterText) return "";
    const char *val = IupGetAttribute(processFilterText, "VALUE");
    return val ? val : "";
}

void uiSetProcessFilterTarget(const char *target) {
    if (processFilterText) {
        IupSetAttribute(processFilterText, "VALUE", target ? target : "");
    }
}

void uiSetProcessFilterActive(BOOL active) {
    if (processFilterText) {
        IupSetAttribute(processFilterText, "ACTIVE", active ? "YES" : "NO");
    }
    if (processFilterToggle) {
        IupSetAttribute(processFilterToggle, "ACTIVE", active ? "YES" : "NO");
    }
    if (processFilterDurationText) {
        IupSetAttribute(processFilterDurationText, "ACTIVE", active ? "YES" : "NO");
    }
    if (processFilterDurationToggle) {
        IupSetAttribute(processFilterDurationToggle, "ACTIVE", active ? "YES" : "NO");
    }
}

BOOL uiIsDurationEnabled(void) {
    if (!processFilterDurationToggle) return FALSE;
    return IupGetInt(processFilterDurationToggle, "VALUE");
}

void uiSetDurationEnabled(BOOL enabled) {
    if (processFilterDurationToggle) {
        IupSetAttribute(processFilterDurationToggle, "VALUE", enabled ? "ON" : "OFF");
    }
}

int uiGetDurationValue(void) {
    if (!processFilterDurationText) return 0;
    const char *rawDur = IupGetAttribute(processFilterDurationText, "VALUE");
    
    BOOL valid = TRUE;
    if (!rawDur || *rawDur == '\0') {
        valid = FALSE;
    } else {
        const char *ptr = rawDur;
        while (*ptr) {
            if (*ptr < '0' || *ptr > '9') {
                valid = FALSE;
                break;
            }
            ptr++;
        }
    }
    
    int ms = 0;
    if (valid) {
        ms = atoi(rawDur);
        if (ms <= 0) {
            valid = FALSE;
        }
    }
    
    if (!valid) {
        ms = 1000;
        IupSetAttribute(processFilterDurationText, "VALUE", "1000");
        LOG("Duration input is invalid or 0. Coerced to 1000ms fallback.");
    }
    
    return ms;
}

void uiSetDurationValue(int durationMs) {
    if (processFilterDurationText) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", durationMs);
        IupStoreAttribute(processFilterDurationText, "VALUE", buf);
    }
}
