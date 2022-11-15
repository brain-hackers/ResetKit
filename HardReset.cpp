#include <algorithm>
#include <tchar.h>
#include <vector>
#include <windows.h>

#define MainWindowClassName L"SelectorMainWindowClass"

#define DataTypeAlertError 0x1000
#define DataTypeAlertInformation 0x1001
#define DataTypeAlertWarning 0x1002

static void showAlertWarning(LPCWSTR message, LPCWSTR title) {

	HWND selectorMainWindow = FindWindow(MainWindowClassName, NULL);

	if (!selectorMainWindow) {
		MessageBox(NULL, message, title, MB_ICONWARNING);
		return;
	}

	wchar_t data[2048];
	wcscpy(data, message);
	data[wcslen(message)] = 0;
	wcscpy(data + wcslen(message) + 1, title);

	size_t dataLen = wcslen(message) + wcslen(title) + 1;

	COPYDATASTRUCT info;
	info.dwData = DataTypeAlertWarning;
	info.cbData = dataLen * sizeof(wchar_t);

	HGLOBAL global = GlobalAlloc(GPTR, info.cbData);
	memcpy((LPVOID)global, data, info.cbData);

	info.lpData = (LPVOID)global;

	SendMessage(selectorMainWindow, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&info);

	GlobalFree(global);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPTSTR lpCmd, int nShow) {
	HINSTANCE lib = LoadLibrary(L"ResetKit");
	if (!lib) {
		wchar_t buf[256];
		swprintf(buf,
			 L"Cannot perform a soft reset.\n"
			 L"ResetKit was not loaded (0x%08x).",
			 GetLastError());
		showAlertWarning(buf, L"Error");
		return 1;
	}

	typedef BOOL (*RKCanHardResetProc)();
	RKCanHardResetProc RKCanHardReset =
	    (RKCanHardResetProc)GetProcAddress(lib, L"RKCanHardReset");
	if (!RKCanHardReset) {
		wchar_t buf[256];
		swprintf(buf,
			 L"Cannot perform a hard reset.\n"
			 L"RKCanHardReset not found (0x%08x).",
			 GetLastError());
		showAlertWarning(buf, L"Error");
		return 1;
	}

	if (!RKCanHardReset()) {
		wchar_t buf[256];
		swprintf(buf,
			 L"Cannot perform a hard reset.\n"
			 L"This device doesn't support the hard reset (0x%08x).",
			 GetLastError());
		showAlertWarning(buf, L"Error");
		return 1;
	}

	typedef BOOL (*RKDoHardResetProc)();
	RKDoHardResetProc RKDoHardReset = (RKDoHardResetProc)GetProcAddress(lib, L"RKDoHardReset");
	if (!RKDoHardReset) {
		wchar_t buf[256];
		swprintf(buf,
			 L"Cannot perform a hard reset.\n"
			 L"RKDoHardReset not found (0x%08x).",
			 GetLastError());
		showAlertWarning(buf, L"Error");
		return 1;
	}

	if (!RKDoHardReset()) {
		wchar_t buf[256];
		swprintf(buf,
			 L"Cannot perform a hard reset.\n"
			 L"Operation failed (0x%08x).",
			 GetLastError());
		showAlertWarning(buf, L"Error");
		return 1;
	}
	return 0;
}
