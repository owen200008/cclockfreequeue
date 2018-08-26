#pragma once

#include <stdio.h>
#include <stdlib.h>
#ifdef __GNUC__
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
typedef unsigned long DWORD;
#define CCSleep(x) usleep(x)
#define ccsnprintf snprintf
#else
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/time.h>
#include <unistd.h>
typedef unsigned long DWORD;
#define CCSleep(x) usleep(x)
#define ccsnprintf snprintf
#endif
#elif defined(_MSC_VER)
#include <Windows.h>
#define CCSleep(x) ::Sleep(x)
#define ccsnprintf sprintf_s
#endif

#ifdef _DEBUG
#define TIMES_FAST 500000
#else
#define TIMES_FAST 2000000
#endif


