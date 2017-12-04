// I guess I could have done a form or something, but we'll do the the old Win32 way
// In certain cases, we need to create a window so that we can receive Windows messages
// Some of this code borrowed from Classic99
//

#include "stdafx.h"
#include <windows.h>
#include <atlstr.h>
#include <Dbt.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <vector>
#include <shellapi.h>

HWND myWnd = NULL;
bool quitflag = false;
NOTIFYICONDATA icon;

void myprintf(char *fmt, ...);
void ProcessInsert(wchar_t letter);
extern int errs;
extern bool verbose;

void HandleDeviceChange(WPARAM wParam, LPARAM lParam);
bool EjectDrive(CString pStr);

/////////////////////////////////////////////////////////////////////////
// Window handler
/////////////////////////////////////////////////////////////////////////
LRESULT FAR PASCAL myproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (myWnd == hwnd) {	// Main window
		switch(msg) {
        case WM_USER:
            if (lParam == WM_LBUTTONDBLCLK) {
                myprintf("Got double click - will exit.\n");
                quitflag = true;
            }
            break;

    	case WM_DESTROY:
            myprintf("Got WM_DESTROY - will exit.\n");
            quitflag = true;
            break;

        case WM_DEVICECHANGE:
            // USB device change
            // This happens 5 or more times in a row in my system,
            // 3 times on removal. This may have to do with the number
            // of installed devices. Both EJECT and physical removal
            // actually trigger this, even if both are used!
            myprintf("Handle device change...\n");
            HandleDeviceChange(wParam, lParam);
            return TRUE;      // accept the new device

        case 0xc26e:
            // I don't know what this is - it's not documented
            // but I see it right at the end. technically it's in the
            // WM_USER range (I guess), so maybe it's my AV?
		default:
            myprintf("got message 0x%08x\n", msg);
  			return(DefWindowProc(hwnd, msg, wParam, lParam));
		}
		return 0;
	}

	return(DefWindowProc(hwnd, msg, wParam, lParam));
}

bool CreateMessageWindow() {
    ATOM myClass;
	WNDCLASS aclass;

	// create and register a class and open a window
	aclass.style = CS_DBLCLKS;
	aclass.lpfnWndProc = myproc;
	aclass.cbClsExtra = 0;
	aclass.cbWndExtra = 0;
	aclass.hInstance = NULL;
	aclass.hIcon = NULL;
	aclass.hCursor = NULL;
	aclass.hbrBackground = (HBRUSH)COLOR_BTNTEXT;
	aclass.lpszMenuName = NULL;
	aclass.lpszClassName = _T("tursicopy_class");
	myClass = RegisterClass(&aclass);
	if (0 == myClass) {	
		myprintf("Can't create class: 0x%x", GetLastError());
        return false;
	}
	myWnd = CreateWindow(_T("tursicopy_class"), _T("TursiCopy"), 0, CW_USEDEFAULT, CW_USEDEFAULT, 32, 32, NULL, NULL, NULL, NULL);
	if (NULL == myWnd) {	
		myprintf("Can't open window: 0x%x", GetLastError());
        return false;
	}

	ShowWindow(myWnd, SW_HIDE);
    return true;
}

/////////////////////////////////////////////////////////
// Window message loop
/////////////////////////////////////////////////////////
void WindowLoop() {
	MSG msg;

	// check for messages
    if (0 == PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
        Sleep(50);
        return;
    }

    // get the pending message
	if (0 == GetMessage(&msg, NULL, 0, 0)) {
        printf("GetMessage returned 0, exitting\n");
		quitflag=true;
	} else {
		if (msg.message == WM_QUIT) {
			// shouldn't happen, since GetMessage should return 0
            printf("Received WM_QUIT, exitting.\n");
			quitflag=true;
		}
			
		if (IsWindow(myWnd)) {
			if (IsDialogMessage(myWnd, &msg)) {
				// processed
				return;
			}
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);		// this will push it to the Window procedure
	}
}

void HandleDeviceChange(WPARAM wParam, LPARAM lParam) {
    DEV_BROADCAST_HDR *pHdr = (DEV_BROADCAST_HDR*)lParam;
    DEV_BROADCAST_VOLUME *pVol = (DEV_BROADCAST_VOLUME*)lParam;

    switch (wParam) {
        // these are the actual events that we need to care about
        // (well, maybe not so much removal...)
        case DBT_DEVICEARRIVAL:
            switch (pHdr->dbch_devicetype) {
                case DBT_DEVTYP_DEVICEINTERFACE:
#ifdef _DEBUG
                    myprintf("added device\n");
#endif
                    break;
                case DBT_DEVTYP_HANDLE:
#ifdef _DEBUG
                    myprintf("added file system handle\n");
#endif
                    break;
                case DBT_DEVTYP_OEM:
#ifdef _DEBUG
                    myprintf("added OEM device\n");
#endif
                    break;
                case DBT_DEVTYP_PORT:
#ifdef _DEBUG
                    myprintf("added port (serial or parallel)\n");
#endif
                    break;

                // this is the only one I care about
                case DBT_DEVTYP_VOLUME:
                    {
                        static time_t lastInsert = (time_t)0;
                        CString newDrives;
                        myprintf("Added a logical volume: ");
                        // MSDN notes there is no guarantee you only get one
                        // notification. We should probably throttle inbound starts
                        // just in case (since we probably won't track removals).
                        for (int idx=0; idx<26; ++idx) {
                            if (pVol->dbcv_unitmask & (1<<idx)) {
                                myprintf("%c: ", idx+'A');
                                newDrives += (wchar_t)(_T('A')+idx);
                            }
                        }
                        // these flags don't show up, though...
                        if (pVol->dbcv_flags&DBTF_MEDIA) {
                            myprintf("(Removable) ");
                        }
                        if (pVol->dbcv_flags&DBTF_NET) {
                            myprintf("(Network) ");
                        }
                        myprintf("\n");

                        // Now trigger our operation
                        time_t now = time_t(NULL);
                        if ((now < lastInsert) || (now > lastInsert+5)) {
                            // if less than 5 seconds since the last one, ignore
                            myprintf("Throttling insert message... no action\n");
                        } else {
                            for (int idx=0; idx<newDrives.GetLength(); ++idx) {
                                ProcessInsert(newDrives[idx]);
                            }
                        }
                    }
                    break;

                default:
                    myprintf("Connected unknown type %d\n", pHdr->dbch_devicetype);
                    break;
            }
            break;

        case DBT_DEVICEREMOVECOMPLETE:
            switch (pHdr->dbch_devicetype) {
                case DBT_DEVTYP_DEVICEINTERFACE:
#ifdef _DEBUG
                    myprintf("removed device\n");
#endif
                    break;
                case DBT_DEVTYP_HANDLE:
#ifdef _DEBUG
                    myprintf("removed file system handle\n");
#endif
                    break;
                case DBT_DEVTYP_OEM:
#ifdef _DEBUG
                    myprintf("removed OEM device\n");
#endif
                    break;
                case DBT_DEVTYP_PORT:
#ifdef _DEBUG
                    myprintf("removed port (serial or parallel)\n");
#endif
                    break;

                // this is the only one I care about
                case DBT_DEVTYP_VOLUME:
                    if (verbose) {
                        myprintf("Removed a logical volume: ");
                        for (int idx=0; idx<26; ++idx) {
                            if (pVol->dbcv_unitmask & (1<<idx)) {
                                myprintf("%c: ", idx+'A');
                            }
                        }
                        // again, these flags don't show up
                        if (pVol->dbcv_flags&DBTF_MEDIA) {
                            myprintf("(Removable) ");
                        }
                        if (pVol->dbcv_flags&DBTF_NET) {
                            myprintf("(Network) ");
                        }
                        myprintf("\n");
                    }
                    break;

                default:
#ifdef _DEBUG
                    myprintf("Removed unknown type %d\n", pHdr->dbch_devicetype);
#endif
                    break;
            }
            break;
        
        // I see a lot of this, header says "This is used by ring3 people which
        // need to be refreshed whenever any devnode changed occur (like
        // device manager)."
        case DBT_DEVNODES_CHANGED:
        // the rest of these I don't see on USB insert/remove
        case DBT_CONFIGCHANGECANCELED:
        case DBT_CONFIGCHANGED:
        case DBT_CUSTOMEVENT:
        case DBT_DEVICEQUERYREMOVE:
        case DBT_DEVICEQUERYREMOVEFAILED:
        case DBT_DEVICEREMOVEPENDING:
        case DBT_DEVICETYPESPECIFIC:
        case DBT_QUERYCHANGECONFIG:
        case DBT_USERDEFINED:
        default:
#ifdef _DEBUG
            myprintf("Got device change message 0x%llx(0x%llx)\n", wParam, lParam);
#endif
            break;

    }
}

// create the tray icon
void CreateTrayIcon() {
    memset(&icon, 0, sizeof(icon));
    icon.cbSize = sizeof(icon);
    icon.hWnd = myWnd;
    icon.uID = 0;
    icon.uFlags = NIF_MESSAGE|NIF_TIP|NIF_ICON;
    icon.uCallbackMessage = WM_USER;
    icon.hIcon=LoadIcon(NULL, IDI_SHIELD);
    wcscpy_s(icon.szTip, _T("Tursicopy: dbl-click to exit"));
    icon.dwState = 0;
    icon.dwStateMask = 0;
    icon.uVersion = 0;
    if (!Shell_NotifyIcon(NIM_ADD, &icon)) {
        // not much we can do, but not critical either
        myprintf("Failed to add shell icon\n");
    }
}

void RemoveTrayIcon() {
    if (icon.cbSize) {
        if (!Shell_NotifyIcon(NIM_DELETE, &icon)) {
            // not much we can do, but not critical either
            if (verbose) {
                myprintf("Failed to remove shell icon\n");
            }
        }
    }
}