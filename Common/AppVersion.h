#ifndef TSTASK_APP_VERSION_H
#define TSTASK_APP_VERSION_H

#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 2
#define APP_VERSION_REVISION 0
#endif

#define APP_VERSION_BUILD 0

#define TSTASK_VERSION_STRING_IMPL(major, minor, revision) #major "." #minor "." #revision
#define TSTASK_VERSION_STRING(major, minor, revision) TSTASK_VERSION_STRING_IMPL(major, minor, revision)
#define TSTASK_VERSION_WSTRING_IMPL(major, minor, revision) L ## #major L"." L ## #minor L"." L ## #revision
#define TSTASK_VERSION_WSTRING(major, minor, revision) TSTASK_VERSION_WSTRING_IMPL(major, minor, revision)

#define APP_VERSION_TEXT TSTASK_VERSION_STRING(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_REVISION)
#define APP_VERSION_TEXT_W TSTASK_VERSION_WSTRING(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_REVISION)

#endif
