#include <fstream>
#include <regex>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include "models.h"

#define FSNOTIFY_POWER_OFF      1
#define FSNOTIFY_POWER_ON       0


#define RESETKITHELPER_API __declspec(dllexport)

#include "ResetKitHelper.h"

#define FILE_DEVICE_POWER   FILE_DEVICE_ACPI    

#define IOCTL_POWER_CAPABILITIES    \
CTL_CODE(FILE_DEVICE_POWER, 0x400, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_GET             \
CTL_CODE(FILE_DEVICE_POWER, 0x401, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_SET             \
CTL_CODE(FILE_DEVICE_POWER, 0x402, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_QUERY           \
CTL_CODE(FILE_DEVICE_POWER, 0x403, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _CEDEVICE_POWER_STATE {
    PwrDeviceUnspecified = -1,
    D0 = 0, // Full On: full power,  full functionality
    D1,     // Low Power On: fully functional at low power/performance
    D2,     // Standby: partially powered with automatic wake
    D3,     // Sleep: partially powered with device initiated wake
    D4,     // Off: unpowered
    PwrDeviceMaximum
} CEDEVICE_POWER_STATE, *PCEDEVICE_POWER_STATE;

typedef struct _POWER_CAPABILITIES {
    UCHAR DeviceDx;
    UCHAR WakeFromDx;
    UCHAR InrushDx;
    DWORD Power[PwrDeviceMaximum];
    DWORD Latency[PwrDeviceMaximum];
    DWORD Flags;
} POWER_CAPABILITIES, *PPOWER_CAPABILITIES;

#define VALID_DX(dx)  ( dx > PwrDeviceUnspecified && dx < PwrDeviceMaximum)


typedef BOOL (*KernelIoControlProc)(DWORD dwIoControlCode, LPVOID lpInBuf,
									DWORD nInBufSize, LPVOID lpOutBuf, DWORD nOutBufSize,
									LPDWORD lpBytesReturned);
static KernelIoControlProc KernelIoControl;

typedef PVOID (*MmMapIoSpaceProc)(DWORD, ULONG, BOOL);
static MmMapIoSpaceProc MmMapIoSpace;


typedef void (*FileSystemPowerFunctionProc)(DWORD);
static FileSystemPowerFunctionProc FileSystemPowerFunction;

typedef void (*NKForceCleanBootProc)(BOOL);

//static void EDNA2_whiteoutScreen();
static bool canSoftReset();
static bool doSoftReset();


static bool g_isDicProtectInstalled=false;
static bool g_dicProtecUserspaceRequested=false;

static void disableInterrupts(){
    asm volatile("mrs	r0, cpsr\n"
        "orr	r0,r0,#0x80\n"
        "msr	cpsr_c,r0\n"
        "mov	r0,#1":::"r0");
}

// only for 0x80000000 - 0x80100000
#define EDNA2Register(addr) (*(volatile uint32_t *)((addr)+0x30000000))

#define HW_ICOLL_INTERRUPTS51				EDNA2Register(0x80000450)

#define HW_RTC_MILLISECONDS					EDNA2Register(0x80056020)

#define HW_PINCTRL_DIN3						EDNA2Register(0x80018930)
#define HW_PINCTRL_PIN2IRQ3					EDNA2Register(0x80019030)
#define HW_PINCTRL_PIN2IRQ3_CLR				EDNA2Register(0x80019038)

#define HW_PWM_ACTIVE0						EDNA2Register(0x80064010)
#define HW_PWM_PERIOD0						EDNA2Register(0x80064020)

#define HW_TIMROT_TIMCTRL3					EDNA2Register(0x800680e0)
#define HW_TIMROT_TIMCTRL3_SET				EDNA2Register(0x800680e4)
#define HW_TIMROT_TIMCTRL3_CLR				EDNA2Register(0x800680e8)
#define HW_TIMROT_TIMCTRL3_TOGGLE			EDNA2Register(0x800680ec)
#define HW_TIMROT_FIXED_COUNT3				EDNA2Register(0x80068100)
#define HW_PINCTRL_DIN3_PIN(pid)			((HW_PINCTRL_DIN3)&(1<<(pid)))
#define HW_PINCTRL_PIN2IRQ3_PIN_CLR(pid)	HW_PINCTRL_PIN2IRQ3_CLR=(1<<(pid))

#define HW_LCDIF_CUR_BUF					EDNA2Register(0x80030040)

static void EDNA2_dimScreen(){
	uint32_t lastValue=HW_PWM_PERIOD0;
	unsigned int active=HW_PWM_ACTIVE0>>16;
	
	uint32_t startTime=HW_RTC_MILLISECONDS;
	uint32_t tickTime;
	unsigned int newActive;
	
	while(tickTime=HW_RTC_MILLISECONDS-startTime, tickTime<512){
		newActive=(active*(512-tickTime))>>9;
		HW_PWM_ACTIVE0=(newActive<<16);
		HW_PWM_PERIOD0=lastValue;
	}
	active>>=1;
	
	while(tickTime=HW_RTC_MILLISECONDS-startTime, tickTime<1024){
		newActive=(active*(tickTime-512))>>9;
		HW_PWM_ACTIVE0=(newActive<<16);
		HW_PWM_PERIOD0=lastValue;
	}
	
	HW_PWM_ACTIVE0=(active<<16);
	HW_PWM_PERIOD0=lastValue;
}

static NKForceCleanBootProc EDNA2_findNKForceCleanBoot(){
    static NKForceCleanBootProc cache=NULL;
    static bool searchFailed=false;
    if(cache==NULL && searchFailed==false){
        
        for(size_t i=0;i<(0xe00000-8);i++){
            uint32_t *universe=(uint32_t *)0x80200000+i;
            if(universe[0]!=0xe59f3020)
                continue;
            if(universe[1]!=0xe59f2018)
                continue;
            if(universe[3]!=0xe5933000)
                continue;
            if(universe[4]!=0xe5821000)
                continue;
            if(universe[5]!=0xe3530000)
                continue;
            if(universe[6]!=0x13a02000)
                continue;
            if(universe[7]!=0x15832004)
                continue;
            if(universe[8]!=0xe12fff1e)
                continue;
            cache=(NKForceCleanBootProc)universe;
        }
        
        if(!cache){
            searchFailed=true;
            OutputDebugString(L"ResetKit: NKForceCleanBoot not found.");
        }else{
            wchar_t buf[256];
            swprintf(buf, L"ResetKit: NKForceCleanBoot found at 0x%08x\n",
                     (int)cache);
            OutputDebugString(buf);
        }
        
    }
    
    return cache;
}

static bool EDNA2_disableCleanBoot(){
    uint32_t *code=(uint32_t *)EDNA2_findNKForceCleanBoot();
    if(!code){
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    
    code[2]=0xe12fff1e;
    
    return true;
}

static void EDNA2_physicalInvoker(){
	// r0-r7=params
	// r8=proc address
	asm volatile("nop\n" // who cares interrupt vectors?
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "msr	cpsr_c, #211\n"	// to supervisor mode
				 "mov	r9, #0\n"
				 "mcr	p15,0,r9,c13,c0,0\n" // clear fcse PID
				 "mrc	p15,0,r9,c1,c0,0\n" // read ctrl regs
				 "bic	r9, r9, #5\n" // disable MMU/DCache
				 "bic	r9, r9, #4096\n" // disable ICache
				 "orr	r9, r9, #8192\n" // and reset vectors to upper
				 "mcr	p15,0,r9,c1,c0,0\n" // write ctrl regs
				 "mov	r9, #0\n"
				 "mcr	p15,0,r9,c7,c7,0\n" // invalidate cache
				 "mcr	p15,0,r9,c8,c7,0\n" // invalidate tlb
				 "mov	pc, r8\n"
				 "nop\n"
				 "nop\n"
				 );
}


static void EDNA2_installPhysicalInvoker(){
	void *ptr=(void *)0xa8000000;
	wchar_t buf[256];
	swprintf(buf, L"ResetKit: copying to 0x%08x from 0x%08x\n",
			 (int)(ptr), (int)(&EDNA2_physicalInvoker));
	OutputDebugString(buf);
	memcpy(ptr, (const void *)&EDNA2_physicalInvoker, 64*4);
	//clearCache();
}


__attribute__((noreturn))
static void EDNA2_runPhysicalInvoker(){
	// r0=info
	asm volatile("msr	cpsr_c, #211\n" // to supervisor mode
				 "mrc	p15,0,r0,c1,c0,0\n" // read ctrl regs
				 "bic	r0, r0, #8192\n" // reset vector to lower
				 "mcr	p15,0,r0,c1,c0,0\n" // write ctrl regs
				 
				 "ldr	r0, =0x0000\n"
				 "ldr	r1, =0x0000\n"
				 "ldr	r2, =0x0000\n"
				 "ldr	r3, =0x0000\n"
				 "ldr	r4, =0x0000\n"
				 "ldr	r5, =0x0000\n"
				 "ldr	r6, =0x0000\n"
				 "ldr	r7, =0x0000\n"
				 "ldr	r8, =0x40200000\n"
				 "ldr	r9, =0x0000\n"
				 
				 "mrc	p15,0,r10,c1,c0,0\n" // read ctrl regs
				 "bic	r10, r10, #5\n" // disable MMU/DCache
				 "mcr	p15,0,r10,c1,c0,0\n" // write ctrl regs
				 "swi	#0\n" // jump!
                 );
	
	// never reach here
	while(true);
}

__attribute__((noreturn))
static DWORD EDNA2_callKernelEntryPoint(){
	
	OutputDebugString(L"ResetKit: disabling interrupts");
    disableInterrupts();
	OutputDebugString(L"ResetKit: injecting code to internal ram");
	EDNA2_installPhysicalInvoker();
	OutputDebugString(L"ResetKit: invoking");
	EDNA2_runPhysicalInvoker();
}

__attribute__((noreturn))
static void EDNA2_resetMPU(){
#undef HW_CLKCTRL_RESET
    volatile uint32_t *HW_CLKCTRL_RESET=(volatile uint32_t *)0xb00401e4;
    *HW_CLKCTRL_RESET=2;
    
    while(true){}
}

static inline bool isBatteryCoverOpen(){
	return HW_PINCTRL_DIN3_PIN(3)==0;
}

static void EDNA2_timerHandler(){
	static unsigned int count=0;
	
	if(isBatteryCoverOpen()){
		count++;
	}else{
		count=0;
	}
	
	if(count==10 || g_dicProtecUserspaceRequested){
		
		// stop interrupt
		//HW_ICOLL_INTERRUPTS51=0;
		
		while(true)
			doSoftReset();
	}
	
}

static void EDNA2_timerThread(LPVOID){
	CeSetThreadPriority(GetCurrentThread(), 0);
	OutputDebugString(L"ResetKit: software timer running.");
	while(true){
		EDNA2_timerHandler();
		Sleep(100);
	}
}

static void EDNA2_keyWaitThread(LPVOID){
	CeSetThreadPriority(GetCurrentThread(), 1);
	OutputDebugString(L"ResetKit: key wait running.");
	while(true){
		if(GetKeyState(VK_BACK)&0x80){
			if(GetKeyState('a')&0x80){
				if(GetKeyState('c')&0x80){
					g_dicProtecUserspaceRequested=true;
				}
			}
		}
		Sleep(50);
	}
}

static void EDNA2_installDicProtectTimer(){
	DWORD tid;
	static bool timerRunning=false;
	
	OutputDebugString(L"ResetKit: default battery-cover interrupt disabled.");
	HW_PINCTRL_PIN2IRQ3_PIN_CLR(3);
	
	if(timerRunning)
		return;
	OutputDebugString(L"ResetKit: starting software timers.");
	CreateThread(NULL, 16384, (LPTHREAD_START_ROUTINE)EDNA2_timerThread,
				 NULL, 0, &tid);
	CreateThread(NULL, 16384, (LPTHREAD_START_ROUTINE)EDNA2_keyWaitThread,
				 NULL, 0, &tid);
	
	timerRunning=true;
	
	
}


/*
static uint8_t handlerStack[16384];

static void EDNA2_installDicProtectFIQ(){
	
	OutputDebugString(L"ResetKit: installing FIQ handler.");
	// install new FIQ handler.
	
	uint32_t *src=(uint32_t *)EDNA2_fiqLowLevelHandler;
	uint32_t *dest=(uint32_t *)0xffff001c;
	
	while(*src!=0xdeadbeef){
		*(dest++)=*(src++);
	}
	*(dest++)=(uint32_t)EDNA2_fiqHighLevelHandler;
	*(dest++)=(uint32_t)((uint8_t *)handlerStack+sizeof(handlerStack));
	
	OutputDebugString(L"ResetKit: invalidating ICache/DCache.");
	clearCache();
	
	OutputDebugString(L"ResetKit: starting Timer3.");
	
	// enable interrupt for Timer3.
	HW_TIMROT_FIXED_COUNT3=100; // every 100ms
	HW_TIMROT_TIMCTRL3=0x40ce; // enable interrupt, 1khz, auto reset
	HW_ICOLL_INTERRUPTS51=0x17; // FIQ, enable, priority 3
	HW_PINCTRL_PIN2IRQ3_PIN_CLR(3);

	
}*/

static void EDNA2_installDicProtect(){
	if(!canSoftReset()){
		OutputDebugString(L"ResetKit: cannot install DicProtect because a soft reset is unavailable.");
		return;
	}
	
	OutputDebugString(L"ResetKit: DicProtect supported.");
	
	//EDNA2_installDicProtectFIQ();
	EDNA2_installDicProtectTimer();
	
	g_isDicProtectInstalled=true;
}

/*
static uint16_t *EDNA2_framebuffer(){
	uint32_t physAddr=HW_LCDIF_CUR_BUF;
	return (uint16_t *)(physAddr+0x60000000);
}
*/
/*
static void EDNA2_whiteoutScreen(){
	uint16_t *fb=EDNA2_framebuffer();
	whiteoutBitmap(fb, 480*320);
}*/

static int deviceGeneration(){
	static std::wifstream iVersion;
	static std::wstring line, model;
	static std::wregex modelRe(L"[A-Z]{2}-[A-Z0-9]+");
	static std::wsmatch match;

	iVersion.open("\\NAND\\version.txt");
	while (getline(iVersion, line))
	{
		if (regex_search(line, match, modelRe))
		{
			model = match[0].str();
			break;
		}
	}

	if (model.length() == 0)
	{
		// During the reset by DicProtect, regex match may fail.
		// Fallback to the old 2nd gen detection method to workaround the issue.

		HINSTANCE lb = LoadLibrary(L"EDNA2_BUZZER");
		if (lb) {
			return 2;
		}

		OutputDebugString(L"ResetKit: failed to match the model name");
		MessageBox(NULL, L"Failed to match the model name", L"ResetKit", MB_ICONWARNING);
		return 0;
	}

	OutputDebugString(L"ResetKit: internal model name");
	OutputDebugString(model.c_str());

	auto iter = models.find(model);
	if (iter != models.end())
	{
		return iter->second;
	}

	OutputDebugString(L"ResetKit: internal model name is unknown");
	OutputDebugString(model.c_str());
	MessageBox(NULL, L"Unknown internal model name", L"ResetKit", MB_ICONWARNING);
	return 0;
}

static bool canSoftReset(){
    static int cache=-1;
    if(cache==-1){
        switch (deviceGeneration()) {
            case 2:
            case 3:
                cache=1;
                if(!EDNA2_findNKForceCleanBoot())
                    cache=0;
                break;
            default:
                cache=0;
        }
        
        if(cache)
            OutputDebugString(L"ResetKit: soft reset supported.");
        else
            OutputDebugString(L"ResetKit: soft reset NOT supported.");
    }
    return cache!=0;
}

static bool canHardReset(){
    static int cache=-1;
    if(cache==-1){
        switch (deviceGeneration()) {
            case 2:
            case 3:
                cache=1;
                break;
            default:
                cache=0;
        }
        
        if(cache)
            OutputDebugString(L"ResetKit: hard reset supported.");
        else
            OutputDebugString(L"ResetKit: hard reset NOT supported.");
    }
    return cache!=0;
}

static bool doSoftReset(){
    if(!canSoftReset())
        return false;
    OutputDebugString(L"ResetKit: performing soft reset.");
    switch(deviceGeneration()){
        case 2:
        case 3:
            if(!EDNA2_disableCleanBoot())
                return false;
			//EDNA2_whiteoutScreen();
			EDNA2_dimScreen();
            EDNA2_callKernelEntryPoint();
            return true;
    }
    return false;
}

static bool doHardReset(){
    if(!canHardReset())
        return false;
    OutputDebugString(L"ResetKit: performing hard reset.");
    switch(deviceGeneration()){
        case 2:
        case 3:
			EDNA2_dimScreen();
            EDNA2_resetMPU();
            return true;
    }
    return false;
}

static void installDicProtect(){
	if(deviceGeneration()==2){
		EDNA2_installDicProtect();
	}
}

extern "C" RESETKITHELPER_API BOOL RKH_IOControl(DWORD handle, DWORD dwIoControlCode, DWORD *pInBuf, DWORD nInBufSize, DWORD * pOutBuf, DWORD nOutBufSize, 
                                           PDWORD pBytesReturned){
    SetLastError(0);
    
    switch(dwIoControlCode){
        case IOCTL_RKH_GET_DEVICE_GENERATION:
            if(nOutBufSize<4 || pOutBuf==NULL){
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            pOutBuf[0]=deviceGeneration();
            return TRUE;
        case IOCTL_RKH_CAN_SOFT_RESET:
            if(nOutBufSize<4 || pOutBuf==NULL){
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            pOutBuf[0]=(canSoftReset()?1:0);
            return TRUE;
        case IOCTL_RKH_CAN_HARD_RESET:
            if(nOutBufSize<4 || pOutBuf==NULL){
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            pOutBuf[0]=(canHardReset()?1:0);
            return TRUE;
        case IOCTL_RKH_DO_SOFT_RESET:
            if(!canSoftReset()){
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
            }
            if(FileSystemPowerFunction)
                FileSystemPowerFunction(FSNOTIFY_POWER_OFF);
            if(!doSoftReset()){
                if(FileSystemPowerFunction)
                    FileSystemPowerFunction(FSNOTIFY_POWER_ON);
                return FALSE;
            }
            
            return TRUE;
        case IOCTL_RKH_DO_HARD_RESET:
            if(!canHardReset()){
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
            }
            if(FileSystemPowerFunction)
                FileSystemPowerFunction(FSNOTIFY_POWER_OFF);
            if(!doHardReset()){
                if(FileSystemPowerFunction)
                    FileSystemPowerFunction(FSNOTIFY_POWER_ON);
                return FALSE;
            }
            return TRUE;
		case IOCTL_RKH_INSTALL_DICPROTECT:
			installDicProtect();
			return g_isDicProtectInstalled?TRUE:FALSE;
			
	
    }
    return FALSE;
}

extern "C" RESETKITHELPER_API BOOL RKH_Read(DWORD handle, LPVOID pBuffer, DWORD dwNumBytes){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

extern "C" RESETKITHELPER_API BOOL RKH_Write(DWORD handle, LPVOID pBuffer, DWORD dwNumBytes){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

extern "C" RESETKITHELPER_API DWORD RKH_Seek(DWORD handle, long lDistance, DWORD dwMoveMethod){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}


extern "C" RESETKITHELPER_API void RKH_PowerUp(void){
	OutputDebugString(L"ResetKit: resuming.");
	installDicProtect();
	
}


extern "C" RESETKITHELPER_API void RKH_PowerDown(void){
	
}


extern "C" RESETKITHELPER_API DWORD RKH_Init(LPCTSTR pContext,
									   DWORD dwBusContext){
    
	void *ctx;
	ctx=(void *)LocalAlloc(LPTR, sizeof(4));
	
	
	
	return (DWORD)ctx;
}


extern "C" RESETKITHELPER_API DWORD RKH_Open(DWORD dwData, DWORD dwAccess, DWORD dwShareMode){
	
	//SetProcPermissions(-1);
	
	void *hnd=(void *)LocalAlloc(LPTR, 4);
	return (DWORD)hnd;
}

extern "C" RESETKITHELPER_API BOOL RKH_Close(DWORD handle){
	LocalFree((void *)handle);
	
	return TRUE;
}

extern "C" RESETKITHELPER_API BOOL RKH_Deinit(DWORD dwContext){
	
	LocalFree((void *)dwContext);
	return TRUE;
}




extern "C" BOOL APIENTRY DllMainCRTStartup( HANDLE hModule, 
                                           DWORD  ul_reason_for_call, 
                                           LPVOID lpReserved
                                           )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
			
			KernelIoControl=(KernelIoControlProc)
			GetProcAddress(LoadLibrary(L"COREDLL"),
						   L"KernelIoControl");
			
			MmMapIoSpace=(MmMapIoSpaceProc)
			GetProcAddress(LoadLibrary(L"CEDDK"),
						   L"MmMapIoSpace");
            
            FileSystemPowerFunction=(FileSystemPowerFunctionProc)
			GetProcAddress(LoadLibrary(L"COREDLL"),
						   L"FileSystemPowerFunction");
            
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}
