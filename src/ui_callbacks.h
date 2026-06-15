#pragma once
#include "iup.h"
#include "common.h"
#include "config_manager.h"

extern Ihandle *dialog, *topFrame, *bottomFrame;
extern Ihandle *statusLabel;
extern Ihandle *filterText, *filterButton;
extern Ihandle *hotkeyLabel;
extern Ihandle *filterSelectList;
extern Ihandle *stateIcon;
extern Ihandle *timer;
extern Ihandle *timeout;
extern Ihandle *durationTimer;

int uiOnDialogShow(Ihandle *ih, int state);
int uiStopCb(Ihandle *ih);
int uiStartCb(Ihandle *ih);
int uiTimerCb(Ihandle *ih);
int uiTimeoutCb(Ihandle *ih);
int uiDurationTimerCb(Ihandle *ih);
int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
int uiFilterTextCb(Ihandle *ih);
void uiSetupModule(Module *module, Ihandle *parent);
void toggleFiltering(void);
void uiApplyProfile(ProfileRecord *p);
void uiActiveSettingsToProfile(ProfileRecord *p, const char *name);
void uiRefreshPresetsList(void);
int uiSavePresetCb(Ihandle *ih);
int uiDeletePresetCb(Ihandle *ih);
int uiProcessFilterChangeCb(Ihandle *ih);
int uiProcessFilterToggleCb(Ihandle *ih, int state);


