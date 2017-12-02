// tursicopy - hardware.cpp : Defines the entry point for the console application.
// some hardware support functions for the monitor/disable features

#include <Windows.h>
#include <atlstr.h>
#include <setupapi.h>
#include <stdio.h>
#include <vector>

void myprintf(char *fmt, ...);
extern int errs;

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

    // Get the handle to a device information set for all devices matching classGuid that are present on the 
    // system.
    diSetHandle = SetupDiGetClassDevs(&diskGuid, NULL, NULL, DIGCF_PRESENT );
    if (INVALID_HANDLE_VALUE == diSetHandle) {
        myprintf("Was not able to get class device for %s\n", instanceId.GetString());
        return false;
    }
    // Verify that we got one and only one device
    std::vector<SP_DEVINFO_DATA> diData = GetDeviceInfoData(diSetHandle);

#ifdef _DEBUG
    myprintf("Got %d devices in class...\n", diData.size());
#endif

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
    if (!EnableDevice(diSetHandle, diData[index], enable)) {
        myprintf("Device action failed!\n");
        SetupDiDestroyDeviceInfoList(diSetHandle);
        return false;
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
