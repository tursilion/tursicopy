// tursicopy - hardware.cpp : Defines the entry point for the console application.
// some hardware support functions for the monitor/disable features

#include <Windows.h>
#include <atlstr.h>
#include <setupapi.h>
#include <stdio.h>
#include <vector>

void myprintf(char *fmt, ...);
extern int errs;
extern bool verbose;

// Enable/disable based on https://stackoverflow.com/questions/1438371/win32-api-function-to-programmatically-enable-disable-device
bool EnableDisk(const CString& instanceId, bool enable);
std::vector<SP_DEVINFO_DATA> GetDeviceInfoData(HDEVINFO handle);
int GetIndexOfInstance(HDEVINFO handle, std::vector<SP_DEVINFO_DATA>& diData, const CString& instanceId);
bool EnableDevice(HDEVINFO handle, SP_DEVINFO_DATA& diData, bool enable);

/// <summary>
/// Enable or disable a disk device.
/// </summary>
/// <param name="instanceId">The device instance id of the device. Available in the device manager.</param>
/// <param name="enable">True to enable, False to disable.</param>
/// <remarks>returns false if the device is not Disableable.</remarks>
bool EnableDisk(const CString& instanceId, bool enable)
{
    HDEVINFO diSetHandle = INVALID_HANDLE_VALUE;
    
    //DISK_GUID "{4D36E967-E325-11CE-BFC1-08002BE10318}"
    GUID diskGuid = {
        0x4D36E967, 0xE325, 0x11CE,
        { 0xBF,0xC1,0x08,0x00,0x2B,0xE1,0x03,0x18 }
    };

    myprintf("Attempting to %sable %S\n", enable?"en":"dis", instanceId);

    // Get the handle to a device information set for all devices matching classGuid that are present on the 
    // system.
    diSetHandle = SetupDiGetClassDevs(&diskGuid, NULL, NULL, DIGCF_PRESENT );
    if (INVALID_HANDLE_VALUE == diSetHandle) {
        myprintf("Was not able to get class device for %s\n", instanceId.GetString());
        return false;
    }
    // Verify that we got one and only one device
    std::vector<SP_DEVINFO_DATA> diData = GetDeviceInfoData(diSetHandle);

    if (verbose) {
        myprintf("Got %d devices in disk class...\n", diData.size());
    }

    if (diData.size() < 1) {
        myprintf("Could not find any devices to control.\n");
        SetupDiDestroyDeviceInfoList(diSetHandle);
        return false;
    }
    // Find the index of our instance.
    int index = GetIndexOfInstance(diSetHandle, diData, instanceId);
    if (-1 == index) {
        myprintf("Failed to locate device with instance %s\n", instanceId);
    SetupDiDestroyDeviceInfoList(diSetHandle);
        return false;
    }
    if (verbose) {
        myprintf("Matched index %d...\n", index);
    }

    if (!EnableDevice(diSetHandle, diData[index], enable)) {
        myprintf("Device action failed!\n");
        SetupDiDestroyDeviceInfoList(diSetHandle);
        ++errs;
        return false;
    }

    if (verbose) {
        myprintf("%sable succeeded...\n", enable?"en":"dis");
    }

    SetupDiDestroyDeviceInfoList(diSetHandle);

    return true;
}

std::vector<SP_DEVINFO_DATA> GetDeviceInfoData(HDEVINFO handle)
{
    std::vector<SP_DEVINFO_DATA> data;
    SP_DEVINFO_DATA did;
    did.cbSize = sizeof(did);
    DWORD index = 0;
    while (SetupDiEnumDeviceInfo(handle, index, &did))
    {
        data.push_back(did);
        ++index;
        did.cbSize = sizeof(did);
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
        myprintf("Error getting device information list on element %d, code %d\n", index, GetLastError());
    }

    return data;
}

// Find the index of the particular DeviceInfoData for the instanceId.
int GetIndexOfInstance(HDEVINFO handle, std::vector<SP_DEVINFO_DATA>& diData, const CString& instanceId)
{
    for (unsigned int index = 0; index < diData.size(); ++index) {
        DWORD requiredSize = 0;
        wchar_t strout[1024];

        bool result = SetupDiGetDeviceInstanceId(handle, &diData[index], strout, 1024, &requiredSize);
        if (result == false) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                myprintf("Device instance string too long to parse (req=%d) for index %d\n", requiredSize, index);
            } else {
                myprintf("Failed to get device instance information for index %d, code %d\n", index, GetLastError());
            }
        } else {
            if (instanceId.CompareNoCase(strout) == 0) {
                return index;
            } else if (verbose) {
                myprintf("Did not match device %d: %S\n", index, strout);
            }
        }
    }
    // not found
    return -1;
}

// enable/disable...
bool EnableDevice(HDEVINFO handle, SP_DEVINFO_DATA& diData, bool enable)
{
    SP_PROPCHANGE_PARAMS params;
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.Scope = DICS_FLAG_GLOBAL;
    if (enable)
    {
        params.StateChange = DICS_ENABLE;
    }
    else
    {
        params.StateChange = DICS_DISABLE;
    }
    // current hardware profile
    params.HwProfile = 0;

    bool result = SetupDiSetClassInstallParams(handle, &diData, (SP_CLASSINSTALL_HEADER*)&params, sizeof(params));
    if (!result) {
        myprintf("Failed on SetClassInstallParams, error %d\n", GetLastError());
        return false;
    }

    result = SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, handle, &diData);
    if (!result) {
        DWORD err = GetLastError();
        // TODO: need to look up the names for these codes
        if (err == ERROR_ACCESS_DENIED) {
            myprintf("Access denied to change device state - running as administrator?\n");
            return false;
        } else if ((err == 0xe0000231)&&(enable==false)) {
            myprintf("Device can't be disabled (programmatically or in Device Manager).\n");
        } else if (err >= 0xe0000200 && err <= 0xe0000245) {
            myprintf("SetupAPI error: 0x%08X\n", err);
        } else {
            myprintf("Class Installer failed with code 0x%08X (enable:%d)\n", err, enable);
        }
        return false;
    }
    
    // per MS, check for reboot request. To us, that's a failure.
    SP_DEVINSTALL_PARAMS data;
    memset(&data, 0, sizeof(data));
    data.cbSize=sizeof(data);
    if (!SetupDiGetDeviceInstallParams(handle, &diData, &data)) {
        myprintf("Couldn't check %s success, code 0x%08X\n", enable?"enable":"disable", GetLastError());
    } else {
        if (data.Flags & (DI_NEEDREBOOT|DI_NEEDRESTART)) {
            myprintf("%s did not take effect - requires system restart.\n", enable?"enable":"disable");
            return false;
        }
    }

    return true;
}

// Input MUST be a drive letter!!
// the steps in this function succeed, but a USB drive isn't actually removed
// at the end of it. I think for now I'll just disable this.
bool EjectDrive(CString pStr) {
    // very likely this is the right answer, thansk to Andreas Magnusson at
    // https://stackoverflow.com/questions/58670/windows-cdrom-eject
    // Further notes by James Johnston
    // They in turn link to 
    // https://support.microsoft.com/en-us/help/165721/how-to-ejecting-removable-media-in-windows-nt-windows-2000-windows-xp
    TCHAR tmp[10];
    _stprintf_s(tmp, _T("\\\\.\\%c:"), pStr[0]);
    myprintf("Attempting to eject %S\n", tmp);

    // open the volume like "\\.\C:"
    HANDLE handle = CreateFile(tmp, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (INVALID_HANDLE_VALUE == handle) {
        myprintf("Failed to open volume, code %d\n", GetLastError());
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
                myprintf("Failed to lock volume (in use?) Code %d\n", GetLastError());
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
        myprintf("Failed to dismount volume... Code %d\n", GetLastError());
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
        myprintf("Media can not be ejected, but it is safe to remove... code %d\n", GetLastError());
    } else {
        // Eject the media with the IOCTL_STORAGE_EJECT_MEDIA IOCTL. This is skipped if the
        // previous IOCTL indicated that ejection is not possible.
        if (!DeviceIoControl(handle, IOCTL_STORAGE_EJECT_MEDIA, 0, 0, 0, 0, &bytes, 0)) {
            myprintf("Failed to eject volume, but it is safe to remove... code %d\n", GetLastError());
        }
    }

    // Close the volume handle obtained in the first step or issue the 
    // FSCTL_UNLOCK_VOLUME IOCTL. This allows the drive to be used by other processes.
    // (For devices like CDROMs which are still attached).
    CloseHandle(handle);
    return true;
}
