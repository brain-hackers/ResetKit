// This file is in public domain.

#include <windows.h>
#include <vector>
#include <algorithm>
#include <tchar.h>

#define MainWindowClassName L"SelectorMainWindowClass"

#define DataTypeAlertError          0x1000
#define DataTypeAlertInformation    0x1001
#define DataTypeAlertWarning        0x1002

static void showAlertWarning(LPCWSTR message,
                           LPCWSTR title){
    
    HWND selectorMainWindow=FindWindow(MainWindowClassName, NULL);
    
    if(!selectorMainWindow){
        MessageBox(NULL, message, title, 
                   MB_ICONWARNING);
        return;
        
    }
    
    wchar_t data[2048];
    wcscpy(data, message);
    data[wcslen(message)]=0;
    wcscpy(data+wcslen(message)+1, title);
    
    size_t dataLen=wcslen(message)+wcslen(title)+1;
    
    COPYDATASTRUCT info;
    info.dwData=DataTypeAlertWarning;
    info.cbData=dataLen*sizeof(wchar_t);
    
    HGLOBAL global=GlobalAlloc(GPTR, info.cbData);
    memcpy((LPVOID)global, data, info.cbData);
    
    info.lpData=(LPVOID)global;
    
    
    
    SendMessage(selectorMainWindow, WM_COPYDATA, (WPARAM)NULL,
                (LPARAM)&info);
    
    GlobalFree(global);
    
}

static bool isThisInstalled(){
	if(GetFileAttributes(L"\\Windows\\ResetKit\\StartDicProtect.exe")==(DWORD)-1){
		return false;
	}
	return true;
}

static void getThisPath(LPWSTR buffer){
	// retrive the path of the application.
	GetModuleFileName(NULL, buffer, 512);
	
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


static void installThis(){
	wchar_t fn[512];
	
	OutputDebugString(L"StartDicProtect: installing ResetKit...");
	
	// create directory
	CreateDirectory(L"\\Windows\\ResetKit", NULL);
	
	// copy this application
	GetModuleFileName(NULL, fn, 512);
	
	CopyFile(fn, L"\\Windows\\ResetKit\\StartDicProtect.exe", FALSE);
	
	// copy ResetKit.dll
	getThisPath(fn);
	wcscat(fn, L"\\ResetKit.dll");
	CopyFile(fn, L"\\Windows\\ResetKit\\ResetKit.dll", FALSE);
	
	// copy ResetKitHelper.dll
	getThisPath(fn);
	wcscat(fn, L"\\ResetKitHelper.dll");
	CopyFile(fn, L"\\Windows\\ResetKit\\ResetKitHelper.dll", FALSE);
	
	// setup autostart
	HKEY    regKey=NULL;
	DWORD Disposition ;
	UINT32 status;
	TCHAR Launc52Val[] = TEXT("\\Windows\\ResetKit\\StartDicProtect.exe");
	BYTE Depend52Val[] = { 0x14, 0x00, 0x1E, 0x00 };
	TCHAR *SystemPathVal;
	TCHAR NewSystemPath[] = TEXT("\\NOR Flash\\Test\\" );
	DWORD NumBytes = 0;
	DWORD Type;
	
	status = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
							TEXT("Init"),
							0,
							0,
							REG_OPTION_NON_VOLATILE,
							0,
							NULL,
							&regKey,
							&Disposition
							);
	
	if( status == ERROR_SUCCESS )
	{
	
		// The value doesn't exist, so add it and change the system path
		RegSetValueEx( regKey,
					  TEXT("Launch80"),
					  0,
					  REG_SZ,
					  (const BYTE*)Launc52Val,
					  (wcslen(Launc52Val)+1)*sizeof( TCHAR)
					  );
		RegSetValueEx( regKey,
					  TEXT("Depend80"),
					  0,
					  REG_BINARY,
					  (const BYTE*)Depend52Val,
					  4
					  );
		// Persist the registry, then close the key
		RegFlushKey( regKey );
		RegCloseKey( regKey );
		
		/*
		status = RegCreateKeyEx(HKEY_LOCAL_MACHINE,
								TEXT("Loader"),
								0,
								0,
								REG_OPTION_NON_VOLATILE,
								0,
								NULL,
								&regKey,
								&Disposition
								);*/
		/*
		if( status == ERROR_SUCCESS )
		{
			// This is a fake read, all it does is fill in NumBytes with the number of
			// bytes in the string value plus the null character.
			status = RegQueryValueEx( regKey, TEXT("SystemPath"), NULL, &Type, NULL, &NumBytes );
			if( NumBytes > 0 )
			{
				// Now we know how big the string is allocate and read it
				SystemPathVal = (TCHAR *)malloc( NumBytes + (wcslen(NewSystemPath)+1)*sizeof( TCHAR));
				if( SystemPathVal != NULL )
				{
					status = RegQueryValueEx( regKey, TEXT("SystemPath"), NULL, &Type,
											 (LPBYTE)SystemPathVal, &NumBytes );
					// Make the math easier, convert to number of characters
					NumBytes /= sizeof( TCHAR );
					// Concatinate the new path on, which overwrites the last null, leaving one
					wsprintf( &SystemPathVal[ NumBytes - 1 ], NewSystemPath );
					NumBytes += wcslen(NewSystemPath);
					// Tack on the second null
					SystemPathVal[ NumBytes ]       = '\0';
					NumBytes += 1;
					
					RegSetValueEx( regKey,
								  TEXT("SystemPath"),
								  0,
								  REG_MULTI_SZ,
								  (const BYTE*)SystemPathVal,
								  (NumBytes * sizeof( TCHAR))
								  );
					free( SystemPathVal );
				}
			}
			// Persist the registry, then close the key
			RegFlushKey( regKey );
			RegCloseKey( regKey );
		}
		*/
	
	}

}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPTSTR lpCmdLine, int nShow){
	//Sleep(2000);
	
	if(wcsstr(lpCmdLine, L"Opened")==0){
		
		// need to open another instance because
		// SHARP Simulator waits for this app to exit.
		
		wchar_t buf[512];
		// retrive the path of the application.
		GetModuleFileName(NULL, buf, 512);
		
		SHELLEXECUTEINFO info;
		
		memset(&info, 0, sizeof(info));
		
		info.cbSize=sizeof(SHELLEXECUTEINFO);
		info.lpFile=buf;
		info.lpParameters=L"Opened";
		
		ShellExecuteEx(&info);
		
		return 0;
	}
	
	if(isThisInstalled()){
		Sleep(1000);
	}
	
    HINSTANCE lib=LoadLibrary(L"ResetKit");
	if(!lib){
		wchar_t buf[256];
		swprintf(buf, L"Cannot install DicProtect.\n"
				L"ResetKit was not loaded (0x%08x).",
				GetLastError());
        showAlertWarning(buf, L"Error");
        return 1;
    }
    
    
    typedef BOOL (*RKInstallDicProtectProc)();
    RKInstallDicProtectProc RKInstallDicProtect=
    (RKInstallDicProtectProc)GetProcAddress(lib, L"RKInstallDicProtect");
    if(!RKInstallDicProtect){
        wchar_t buf[256];
		swprintf(buf, L"Cannot install DicProtect.\n"
                 L"RKInstallDicProtect not found; ResetKit is too old (0x%08x).",
                 GetLastError());
        showAlertWarning(buf, L"Error");
        return 1;
    }
    
    if(!RKInstallDicProtect()){
        wchar_t buf[256];
		swprintf(buf, L"Cannot install DicProtect.\n"
                 L"DicProtect is unavailable (0x%08x).",
                 GetLastError());
        showAlertWarning(buf, L"Error");
        return 1;
    }
	
	if(!isThisInstalled()){
		installThis();
	}
    return 0;
}


