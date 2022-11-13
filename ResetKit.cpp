#define RESETKIT_API extern "C" __declspec(dllexport)
#include "ResetKit.h"
#include "ResetKitHelper.h"
#include <string.h>

static bool g_initialized=false;
static HANDLE g_helperHandle=NULL;
static HINSTANCE g_hInstance;

static void initialize(){
    if(g_initialized)
        return;
    
    g_initialized=true;
}

static void getThisDllDirectoryPath(LPWSTR buffer){
	// retrive the path of the application.
	GetModuleFileName(g_hInstance, buffer, 512);
	
    const size_t notFound=(size_t)-1;
    size_t i=0;
    size_t j=notFound;
    
    while(buffer[i]){
        if(buffer[i]==L'/' || buffer[i]==L'\\'){
            j=i;
        }
        i++;
    }
    
    if(j==notFound)
        return;
    
    buffer[j]=0;
    
}

static bool isDriverLoaded(){
    HANDLE handle;
    handle=CreateFile(L"RKH0:", GENERIC_READ|GENERIC_WRITE|FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(handle==INVALID_HANDLE_VALUE)
		handle=NULL;
    if(handle){
        CloseHandle(handle);
        return true;
    }
    return false;
}

static void loadDriverIfNeeded(){
    if(isDriverLoaded())
        return;
	
	OutputDebugString(L"ResetKit: installing ResetKitHelper...");
	
	DWORD err;
    HANDLE driver=NULL;
	
    wchar_t driverPath[1024];
    getThisDllDirectoryPath(driverPath);
    wcscat(driverPath, L"\\ResetKitHelper.dll");
	
	LPCWSTR newDriverPath;
	newDriverPath=L"\\Windows\\ResetKitHelper.dll";
	
    {
        wchar_t buf[1024];
        swprintf(buf, L"ResetKit: copying \"%ls\" to \"%ls\"",
                 driverPath, newDriverPath);
        OutputDebugString(buf);
    }
	
	if(!CopyFile(driverPath,
				 newDriverPath, FALSE)){
	
		
		if(GetFileAttributes(newDriverPath)==(DWORD)-1){
			OutputDebugString(L"ResetKit: failed to copy");
			return;
		}
        
	}
	/*
	addDriverToRegistry();
	OutputDebugString(L"ResetKit: activating ResetKitHelper...");
	ActivateDevice(L"Drivers\\ResetKitHelper", NULL);
	{
		wchar_t buf[256];
		DWORD err=GetLastError();
		if(err){
			swprintf(buf, L"ResetKit: ActivateDevice error: 0x%08x", (int)err);
			OutputDebugString(buf);
		}
	}
	*/
	
	OutputDebugString(L"ResetKit: registering ResetKitHelper...");
	
	try{
		driver=RegisterDevice(L"RKH", 0, L"\\Windows\\ResetKitHelper.dll", 0);
		
		if(driver==INVALID_HANDLE_VALUE)
			driver=NULL;
		
		if(!driver){
			
			driver=RegisterDevice(L"RKH", 0, L"ResetKitHelper.dll", 0);
			
			if(driver==INVALID_HANDLE_VALUE)
				driver=NULL;
			
		}
		
		err=GetLastError();
	}catch(DWORD e){
		err=e;
	}
	if(!driver && (err!=0x964)){
		OutputDebugString(L"ResetKit: failed to install...");
		return;
	}
	
	//addDriverToRegistry();
	

}

static void openDriver(){
    if(g_helperHandle)
        return;
    g_helperHandle=CreateFile(L"RKH0:", GENERIC_READ|GENERIC_WRITE|FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
	if(g_helperHandle==INVALID_HANDLE_VALUE)
		g_helperHandle=NULL;
}

extern "C" BOOL APIENTRY DllMain(HINSTANCE hInstance, 
                                           DWORD  ul_reason_for_call, 
                                           LPVOID lpReserved
                                           )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
            
            g_hInstance=hInstance;
            
            initialize();
            loadDriverIfNeeded();
            openDriver();
			
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}

RESETKIT_API DWORD RKDeviceGeneration(){
	DWORD outBuf[1];
	DWORD retSize;
	
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
	if(DeviceIoControl(g_helperHandle, IOCTL_RKH_GET_DEVICE_GENERATION,
					   NULL, 0, outBuf, 4,
					   &retSize, NULL)){
        SetLastError(0);
		return outBuf[0];
	}else{
		return FALSE;
	}
}

RESETKIT_API BOOL RKCanSoftReset(){
    DWORD outBuf[1];
	DWORD retSize;
	
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
	if(DeviceIoControl(g_helperHandle, IOCTL_RKH_CAN_SOFT_RESET,
					   NULL, 0, outBuf, 4,
					   &retSize, NULL)){
        SetLastError(0);
		return outBuf[0]!=0;
	}else{
		return FALSE;
	}
}
RESETKIT_API BOOL RKCanHardReset(){
    DWORD outBuf[1];
	DWORD retSize;
	
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
	if(DeviceIoControl(g_helperHandle, IOCTL_RKH_CAN_HARD_RESET,
					   NULL, 0, outBuf, 4,
					   &retSize, NULL)){
        SetLastError(0);
		return outBuf[0]!=0;
	}else{
		return FALSE;
	}
}

RESETKIT_API BOOL RKDoSoftReset(){
	
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
    return DeviceIoControl(g_helperHandle, IOCTL_RKH_DO_SOFT_RESET,
                           NULL, 0, NULL, 0,
                           NULL, NULL);
}
RESETKIT_API BOOL RKDoHardReset(){
	
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
    return DeviceIoControl(g_helperHandle, IOCTL_RKH_DO_HARD_RESET,
                           NULL, 0, NULL, 0,
                           NULL, NULL);
}
RESETKIT_API BOOL RKInstallDicProtect(){
	initialize();
    loadDriverIfNeeded();
    openDriver();
	
    return DeviceIoControl(g_helperHandle, IOCTL_RKH_INSTALL_DICPROTECT,
                           NULL, 0, NULL, 0,
                           NULL, NULL);
}

