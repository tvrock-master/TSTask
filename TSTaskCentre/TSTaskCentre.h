// Modified in this TSTask fork (recorded 2026-11-19).
#ifndef TS_TASK_CENTRE_H
#define TS_TASK_CENTRE_H


#ifndef RC_INVOKED
#include "../Common/TSTaskCommon.h"
#endif

#include "../Common/AppVersion.h"

#define APP_NAME	"TSTaskCentre"
#define APP_NAME_W	L"TSTaskCentre"

#if defined(_M_IX86)
#define APP_PLATFORM_W	L"x86"
#elif defined(_M_X64)
#define APP_PLATFORM_W	L"x64"
#endif


#endif
