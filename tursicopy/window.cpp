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

HWND myWnd = NULL;
bool quitflag = false;

void myprintf(char *fmt, ...);
extern int errs;

void HandleDeviceChange(WPARAM wParam, LPARAM lParam);
bool EjectDrive(const char* pStr);

/////////////////////////////////////////////////////////////////////////
// Window handler
/////////////////////////////////////////////////////////////////////////
LRESULT FAR PASCAL myproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (myWnd == hwnd) {	// Main window
		switch(msg) {
    	case WM_DESTROY:
			PostQuitMessage(0);
			break;

        case WM_DEVICECHANGE:
            // USB device change
            // This happens 5 or more times in a row in my system,
            // 3 times on removal. This may have to do with the number
            // of installed devices. Both EJECT and physical removal
            // actually trigger this, even if both are used!
            printf("Handle device change...\n");
            HandleDeviceChange(wParam, lParam);
            return TRUE;      // accept the new device

        case 0xc26e:
            // I don't know what this is - it's not documented
            // but I see it right at the end. technically it's in the
            // WM_USER range (I guess), so maybe it's my AV?
		default:
            printf("got message 0x%08x\n", msg);
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
	aclass.style = 0;
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
		printf("Can't create class: 0x%x", GetLastError());
        return false;
	}
	myWnd = CreateWindow(_T("tursicopy_class"), _T("TursiCopy"), 0, CW_USEDEFAULT, CW_USEDEFAULT, 32, 32, NULL, NULL, NULL, NULL);
	if (NULL == myWnd) {	
		printf("Can't open window: 0x%x", GetLastError());
        return false;
	}

	ShowWindow(myWnd, SW_HIDE);
    return true;
}

/////////////////////////////////////////////////////////
// Window message loop
/////////////////////////////////////////////////////////
void WindowThread() {
	MSG msg;

	while (!quitflag) {
		// check for messages
		if (0 == GetMessage(&msg, NULL, 0, 0)) {
			quitflag=1;
			break;
		} else {
			if (msg.message == WM_QUIT) {
				// shouldn't happen, since GetMessage should return 0
				quitflag=1;
			} 
			
			if (IsWindow(myWnd)) {
				if (IsDialogMessage(myWnd, &msg)) {
					// processed
					continue;
				}
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);		// this will push it to the Window procedure
		}
	}
}

void HandleDeviceChange(WPARAM wParam, LPARAM lParam) {
    DEV_BROADCAST_HDR *pHdr = (DEV_BROADCAST_HDR*)lParam;
    DEV_BROADCAST_VOLUME *pVol = (DEV_BROADCAST_VOLUME*)lParam;

    switch (wParam) {
        // these are the actual events that we need to care about
        // (well, maybe not so much removal...)
        case DBT_DEVICEARRIVAL:
            // TODO: I'm getting a types:
            // USB (net.com) - 38 (0x26)...???
            // Card reader - 114  (0x72)... (twice)
            switch (pHdr->dbch_devicetype) {
                case DBT_DEVTYP_DEVICEINTERFACE:
                    printf("added device\n");
                    break;
                case DBT_DEVTYP_HANDLE:
                    printf("added file system handle\n");
                    break;
                case DBT_DEVTYP_OEM:
                    printf("added OEM device\n");
                    break;
                case DBT_DEVTYP_PORT:
                    printf("added port (serial or parallel)\n");
                    break;

                // this is the only one I care about
                case DBT_DEVTYP_VOLUME:
                    printf("Added a logical volume: ");
                    // TODO: this is now working!!
                    // TODO2: MSDN notes there is no guarantee you only get one
                    // notification. We should probably throttle inbound starts
                    // just in case (since we probably won't track removals).
                    for (int idx=0; idx<26; ++idx) {
                        if (pVol->dbcv_unitmask & (1<<idx)) {
                            printf("%c: ", idx+'A');
                        }
                    }
                    // these flags don't show up, though...
                    if (pVol->dbcv_flags&DBTF_MEDIA) {
                        printf("(Removable) ");
                    }
                    if (pVol->dbcv_flags&DBTF_NET) {
                        printf("(Network) ");
                    }
                    printf("\n");

                    // todo: testing removal
                    Sleep(1000);
                    EjectDrive("E");
                    break;

                default:
                    printf("Connected unknown type %d\n", pHdr->dbch_devicetype);
                    break;
            }
            break;

        case DBT_DEVICEREMOVECOMPLETE:
            switch (pHdr->dbch_devicetype) {
                case DBT_DEVTYP_DEVICEINTERFACE:
                    printf("removed device\n");
                    break;
                case DBT_DEVTYP_HANDLE:
                    printf("removed file system handle\n");
                    break;
                case DBT_DEVTYP_OEM:
                    printf("removed OEM device\n");
                    break;
                case DBT_DEVTYP_PORT:
                    printf("removed port (serial or parallel)\n");
                    break;

                // this is the only one I care about
                case DBT_DEVTYP_VOLUME:
                    // TODO: this is working
                    printf("Removed a logical volume: ");
                    for (int idx=0; idx<26; ++idx) {
                        if (pVol->dbcv_unitmask & (1<<idx)) {
                            printf("%c: ", idx+'A');
                        }
                    }
                    // again, these flags don't show up
                    if (pVol->dbcv_flags&DBTF_MEDIA) {
                        printf("(Removable) ");
                    }
                    if (pVol->dbcv_flags&DBTF_NET) {
                        printf("(Network) ");
                    }
                    printf("\n");
                    break;

                default:
                    printf("Removed unknown type %d\n", pHdr->dbch_devicetype);
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
            printf("Got device change message 0x%llx(0x%llx)\n", wParam, lParam);
            break;

    }
}

// Input MUST be a drive letter!!
bool EjectDrive(const char* pStr) {
    // very likely this is the right answer, thansk to Andreas Magnusson at
    // https://stackoverflow.com/questions/58670/windows-cdrom-eject
    // Further notes by James Johnston
    // They in turn link to 
    // https://support.microsoft.com/en-us/help/165721/how-to-ejecting-removable-media-in-windows-nt-windows-2000-windows-xp
    TCHAR tmp[10];
    _stprintf_s(tmp, _T("\\\\.\\%c:"), pStr[0]);
    // open the volume like "\\.\C:"
    HANDLE handle = CreateFile(tmp, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (INVALID_HANDLE_VALUE == handle) {
        printf("Failed to open volume, code %d\n", GetLastError());
        CloseHandle(handle);
        return false;
    }
    DWORD bytes = 0;
    // Lock the volume by issuing the FSCTL_LOCK_VOLUME IOCTL via DeviceIoControl. 
    // If any other application or the system is using the volume, this IOCTL fails. 
    // Once this function returns successfully, the application is guaranteed that 
    // the volume is not used by anything else in the system.
    // MSDN tries this a few times, just in case
    for (int idx=10; idx>=0; --idx) {
        if (!DeviceIoControl(handle, FSCTL_LOCK_VOLUME, 0, 0, 0, 0, &bytes, 0)) {
            if (idx == 0) {
                printf("Failed to lock volume (in use?) Code %d\n", GetLastError());
                CloseHandle(handle);
                return false;
            }
            Sleep(500);
            continue;
        }
        break;
    }

    // Dismount the volume by issuing the FSCTL_DISMOUNT_VOLUME IOCTL. 
    // This causes the file system to remove all knowledge of the volume and to 
    // discard any internal information that it keeps regarding the volume.
    if (!DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0, &bytes, 0)) {
        printf("Failed to dismount volume... Code %d\n", GetLastError());
        // should we unlock?
        DeviceIoControl(handle, FSCTL_UNLOCK_VOLUME, 0, 0, 0, 0, &bytes, 0);
        CloseHandle(handle);
        return false;
    }

    // Make sure the media can be removed by issuing the IOCTL_STORAGE_MEDIA_REMOVAL IOCTL. 
    // Set the PreventMediaRemoval member of the PREVENT_MEDIA_REMOVAL structure to FALSE 
    // before calling this IOCTL. This clears any previous lock on the media.
    // Note that the system can not eject things like USB sticks, only CDs and tapes,
    // so a failure at this point is EXPECTED.
    PREVENT_MEDIA_REMOVAL PMRBuffer;
    PMRBuffer.PreventMediaRemoval = FALSE;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_MEDIA_REMOVAL, &PMRBuffer, sizeof(PMRBuffer), 0, 0, &bytes, 0)) {
        printf("Media can not be ejected, but it is safe to remove... code %d\n", GetLastError());
    } else {
        // Eject the media with the IOCTL_STORAGE_EJECT_MEDIA IOCTL. This is skipped if the
        // previous IOCTL indicated that ejection is not possible.
        if (!DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, 0, 0, 0, 0, &bytes, 0)) {
            printf("Failed to eject volume, but it is safe to remove... code %d\n", GetLastError());
        }
    }

    // Close the volume handle obtained in the first step or issue the 
    // FSCTL_UNLOCK_VOLUME IOCTL. This allows the drive to be used by other processes.
    // (For devices like CDROMs which are still attached).
    CloseHandle(handle);
    return true;
}
