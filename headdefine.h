#pragma once

#ifdef _WIN32
#include <Windows.h>
#define CCSleep(x) ::Sleep(x)
#else
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/time.h>
typedef unsigned long DWORD;

#define CCSleep(x) usleep(x)
#endif

#ifdef _DEBUG
#define TIMES_FAST 500000
#else
#define TIMES_FAST 2000000
#endif


