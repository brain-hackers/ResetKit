#pragma once

#include <windows.h>

#ifndef RESETKIT_API
#define RESETKIT_API __declspec(dllimport)
#endif

#define ERROR_GENERATION_NOT_SUPPORTED 0x1200
#define ERROR_NKFORCECLEANBOOT_NOT_FOUND 0x1201

#ifdef __cplusplus
extern "C" {
#endif

RESETKIT_API DWORD RKDeviceGeneration();

RESETKIT_API BOOL RKCanSoftReset();
RESETKIT_API BOOL RKCanHardReset();

RESETKIT_API BOOL RKDoSoftReset();
RESETKIT_API BOOL RKDoHardReset();

RESETKIT_API BOOL RKInstallDicProtect();

#ifdef __cplusplus
};
#endif
