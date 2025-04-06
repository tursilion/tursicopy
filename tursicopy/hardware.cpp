// tursicopy - hardware.cpp : Defines the entry point for the console application.
// some hardware support functions for the monitor/disable features

#include <Windows.h>
#include <atlstr.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <vector>

void myprintf(char *fmt, ...);
extern int errs;
extern bool verbose;

// Enable/disable based on https://stackoverflow.com/questions/1438371/win32-api-function-to-programmatically-enable-disable-device
bool EnableDisk(const CString& instanceId, bool enable, bool &wasAlready);
std::vector<SP_DEVINFO_DATA> GetDeviceInfoData(HDEVINFO handle);
int GetIndexOfInstance(HDEVINFO handle, std::vector<SP_DEVINFO_DATA>& diData, const CString& instanceId);
bool EnableDevice(HDEVINFO handle, SP_DEVINFO_DATA& diData, bool enable, bool &wasalready);

/// <summary>
/// Enable or disable a disk device.
/// </summary>
/// <param name="instanceId">The device instance id of the device. Available in the device manager.</param>
/// <param name="enable">True to enable, False to disable.</param>
/// <remarks>returns false if the device is not Disableable.</remarks>
bool EnableDisk(const CString& instanceId, bool enable, bool &wasAlready)
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
        myprintf("Failed to locate device with instance %S\n", instanceId);
        SetupDiDestroyDeviceInfoList(diSetHandle);
        return false;
    }
    if (verbose) {
        myprintf("Matched index %d...\n", index);
    }

    if (!EnableDevice(diSetHandle, diData[index], enable, wasAlready)) {
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
bool EnableDevice(HDEVINFO handle, SP_DEVINFO_DATA& diData, bool enable, bool &wasAlready)
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

    // see if we really need to do anything
    ULONG devStatus(0), devProblemCode(0);
    DWORD ret = CM_Get_DevNode_Status(&devStatus, &devProblemCode, diData.DevInst, 0);
    if (ret != CR_SUCCESS) {
        myprintf("Attempting to read device status returned error %ld, will try anyway.\n", ret);
    } else {
        if (verbose) {
            myprintf("Status: 0x%llX\n", devStatus);
            if (devStatus&DN_ROOT_ENUMERATED) myprintf("  Root enumerated\n");
            if (devStatus&DN_DRIVER_LOADED) myprintf("  Driver loaded\n");
            if (devStatus&DN_ENUM_LOADED) myprintf("  Enumerator loaded\n");
            if (devStatus&DN_STARTED) myprintf("  Is started\n");
            if (devStatus&DN_MANUAL) myprintf("  Manually installed\n");
            if (devStatus&DN_NEED_TO_ENUM) myprintf("  Re-enumeration required\n");
            if (devStatus&DN_NOT_FIRST_TIME) myprintf("  Already configured\n");
            if (devStatus&DN_HARDWARE_ENUM) myprintf("  Has Hardware ID\n");
            if (devStatus&DN_LIAR) myprintf("  Lied about reconfig (seriously!)\n");
            if (devStatus&DN_HAS_MARK) myprintf("  Not created lately\n");
            if (devStatus&DN_HAS_PROBLEM) myprintf("  Has problem (details follow)\n");
            if (devStatus&DN_FILTERED) myprintf("  Filtered\n");
            if (devStatus&DN_MOVED) myprintf("  Has moved\n");
            if (devStatus&DN_DISABLEABLE) myprintf("  Can be disabled\n");
            if (devStatus&DN_REMOVABLE) myprintf("  Can be removed\n");
            if (devStatus&DN_PRIVATE_PROBLEM) myprintf("  Has a private problem\n");
            if (devStatus&DN_MF_PARENT) myprintf("  Multi-function parent\n");
            if (devStatus&DN_MF_CHILD) myprintf("  Multi-function child\n");
            if (devStatus&DN_WILL_BE_REMOVED) myprintf("  Is being removed\n");
            if (devStatus&DN_NOT_FIRST_TIMEE) myprintf("  Received config enumerate\n");
            if (devStatus&DN_STOP_FREE_RES) myprintf("  Free resources on child stop\n");
            if (devStatus&DN_REBAL_CANDIDATE) myprintf("  Don't skip during rebalance\n");
            if (devStatus&DN_BAD_PARTIAL) myprintf("  Log_confs do not have the same resource\n");
            if (devStatus&DN_NT_ENUMERATOR) myprintf("  NT Enumerator\n");
            if (devStatus&DN_NT_DRIVER) myprintf("  NT Driver\n");
            if (devStatus&DN_NEEDS_LOCKING) myprintf("  Needs lock resume\n");
            if (devStatus&DN_ARM_WAKEUP) myprintf("  Available as wakeup device\n");
            if (devStatus&DN_APM_ENUMERATOR) myprintf("  APM aware enumerator\n");
            if (devStatus&DN_APM_DRIVER) myprintf("  APM aware driver\n");
            if (devStatus&DN_SILENT_INSTALL) myprintf("  Silent install\n");
            if (devStatus&DN_NO_SHOW_IN_DM) myprintf("  No show in device manager\n");
            if (devStatus&DN_BOOT_LOG_PROB) myprintf("  Problem during boot log configuration\n");
        }
        if (devStatus & DN_HAS_PROBLEM) {
            myprintf("Problem code: 0x%llX\n", devProblemCode);
            switch (devProblemCode) {
                case CM_PROB_NOT_CONFIGURED             : myprintf("  No configuration\n"); break;
                case CM_PROB_DEVLOADER_FAILED           : myprintf("  Service load failed\n"); break;
                case CM_PROB_OUT_OF_MEMORY              : myprintf("  Out of memory\n"); break;
                case CM_PROB_ENTRY_IS_WRONG_TYPE        : myprintf("  Entry is wrong type\n"); break;
                case CM_PROB_LACKED_ARBITRATOR          : myprintf("  Lacked arbitrator\n"); break;
                case CM_PROB_BOOT_CONFIG_CONFLICT       : myprintf("  Boot config conflict\n"); break;
                case CM_PROB_FAILED_FILTER              : myprintf("  Failed filter\n"); break;
                case CM_PROB_DEVLOADER_NOT_FOUND        : myprintf("  Devloader not found\n"); break;
                case CM_PROB_INVALID_DATA               : myprintf("  Invalid ID\n"); break;
                case CM_PROB_FAILED_START               : myprintf("  Failed start\n"); break;
                case CM_PROB_LIAR                       : myprintf("  Liar (yes, seriously)\n"); break;
                case CM_PROB_NORMAL_CONFLICT            : myprintf("  Config conflict\n"); break;
                case CM_PROB_NOT_VERIFIED               : myprintf("  Not verified\n"); break;
                case CM_PROB_NEED_RESTART               : myprintf("  Requires restart\n"); break;
                case CM_PROB_REENUMERATION              : myprintf("  Re-enumeration required\n"); break;
                case CM_PROB_PARTIAL_LOG_CONF           : myprintf("  Partial log configuration\n"); break;
                case CM_PROB_UNKNOWN_RESOURCE           : myprintf("  Unknown resource type\n"); break;
                case CM_PROB_REINSTALL                  : myprintf("  Reinstall required\n"); break;
                case CM_PROB_REGISTRY                   : myprintf("  Registry problem\n"); break;
                case CM_PROB_VXDLDR                     : myprintf("  Windows 95 VXD Loader issue\n"); break;
                case CM_PROB_WILL_BE_REMOVED            : myprintf("  Will be removed\n"); break;
                case CM_PROB_DISABLED                   : myprintf("  Devinst is disabled\n"); break;
                case CM_PROB_DEVLOADER_NOT_READY        : myprintf("  Devloader is not ready\n"); break;
                case CM_PROB_DEVICE_NOT_THERE           : myprintf("  Device doesn't exist\n"); break;
                case CM_PROB_MOVED                      : myprintf("  Moved\n"); break;
                case CM_PROB_TOO_EARLY                  : myprintf("  Too early\n"); break;
                case CM_PROB_NO_VALID_LOG_CONF          : myprintf("  No valid log config\n"); break;
                case CM_PROB_FAILED_INSTALL             : myprintf("  Install failed\n"); break;
                case CM_PROB_HARDWARE_DISABLED          : myprintf("  Device disabled\n"); break;
                case CM_PROB_CANT_SHARE_IRQ             : myprintf("  Can't share IRQ\n"); break;
                case CM_PROB_FAILED_ADD                 : myprintf("  Driver add failed\n"); break;
                case CM_PROB_DISABLED_SERVICE           : myprintf("  Service start disabled\n"); break;
                case CM_PROB_TRANSLATION_FAILED         : myprintf("  Resource translation failed\n"); break;
                case CM_PROB_NO_SOFTCONFIG              : myprintf("  No soft config\n"); break;
                case CM_PROB_BIOS_TABLE                 : myprintf("  Device missing in BIOS table\n"); break;
                case CM_PROB_IRQ_TRANSLATION_FAILED     : myprintf("  IRQ translator failed\n"); break;
                case CM_PROB_FAILED_DRIVER_ENTRY        : myprintf("  DriverEntry() failed\n"); break;
                case CM_PROB_DRIVER_FAILED_PRIOR_UNLOAD : myprintf("  Driver should have unloaded\n"); break;
                case CM_PROB_DRIVER_FAILED_LOAD         : myprintf("  Driver load unsuccessful\n"); break;
                case CM_PROB_DRIVER_SERVICE_KEY_INVALID : myprintf("  Error accessing driver's service key\n"); break;
                case CM_PROB_LEGACY_SERVICE_NO_DEVICES  : myprintf("  Legacy service created no devices\n"); break;
                case CM_PROB_DUPLICATE_DEVICE           : myprintf("  Two devices discovered with the same name\n"); break;
                case CM_PROB_FAILED_POST_START          : myprintf("  Driver set its state to failed\n"); break;
                case CM_PROB_HALTED                     : myprintf("  Device failed post start via usermode\n"); break;
                case CM_PROB_PHANTOM                    : myprintf("  Devinst exists only in the registry\n"); break;
                case CM_PROB_SYSTEM_SHUTDOWN            : myprintf("  System is shutting down\n"); break;
                case CM_PROB_HELD_FOR_EJECT             : myprintf("  Offline awaiting removal\n"); break;
                case CM_PROB_DRIVER_BLOCKED             : myprintf("  One or more drivers blocked from loading\n"); break;
                case CM_PROB_REGISTRY_TOO_LARGE         : myprintf("  System hive has grown too large\n"); break;
                case CM_PROB_SETPROPERTIES_FAILED       : myprintf("  Failed to apply registry properties\n"); break;
                case CM_PROB_WAITING_ON_DEPENDENCY      : myprintf("  Device stalled waiting for dependency to start\n"); break;
                case CM_PROB_UNSIGNED_DRIVER            : myprintf("  Failed to load due to unsigned driver\n"); break;
                case CM_PROB_USED_BY_DEBUGGER           : myprintf("  Used by kernel debugger\n"); break;
                case CM_PROB_DEVICE_RESET               : myprintf("  Device is being reset\n"); break;
                case CM_PROB_CONSOLE_LOCKED             : myprintf("  Console is locked\n"); break;
                case CM_PROB_NEED_CLASS_CONFIG          : myprintf("  Device needs extended class configuration\n"); break;
            }
            // if it's disabled and we're enabling, that's not a problem ;)
            if (devProblemCode == CM_PROB_DISABLED) {
                if (enable == false) {
                    // again, that's what we wanted
                    myprintf("Device was already disabled...\n");
                    return true;
                }
            }
            // else that's what we aim to change
            if (devProblemCode != CM_PROB_DISABLED) {
                // no sense continuing on problem device, it won't work anyway
                return false;
            }
        }

        // check if the device state meets requirements
        wasAlready = false;
        if (enable) {
            // then it must be disabled ;)
            if (devStatus&DN_STARTED) {
                // it's already running
                if (verbose) {
                    myprintf("Device is already running.\n");
                }
                wasAlready = true;
                return true;
            }
        } else {
            // then it must be started and disableable
            if (!(devStatus&DN_STARTED)) {
                // it's already disabled
                if (verbose) {
                    myprintf("Device is already disabled.\n");
                }
                wasAlready = true;
                return true;
            }
            // maybe we can loop on this if it's not?
            if (!(devStatus&DN_DISABLEABLE)) {
                
                for (int idx=10; idx>=0; --idx) {
                    if (verbose) {
                        myprintf("Not disablable... retrying...\n");
                    }
                    Sleep(500);

                    DWORD ret = CM_Get_DevNode_Status(&devStatus, &devProblemCode, diData.DevInst, 0);
                    if (ret != CR_SUCCESS) {
                        myprintf("Error occurred on subsequent status check - code %d\n", ret);
                        return false;
                    }

                    if (devStatus&DN_DISABLEABLE) break;

                    if (idx <= 0) {
                        myprintf("Timed out waiting to become disablable. Giving up.\n");
                        return false;
                    }
                }
            }
        }
    }

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
            myprintf("Class Installer failed with code 0x%08X (enable:%d)\nA reboot may be required.\n", err, enable);
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

    myprintf("Operation succeeded.\n");
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

// Input MUST be a drive letter!!
// attempts to flush all pending data on a drive
// this requires administrative access too
bool FlushDrive(CString pStr) {
    TCHAR tmp[10];
    _stprintf_s(tmp, _T("\\\\.\\%c:"), pStr[0]);
    if (verbose) {
        myprintf("Flushing %S\n", tmp);
    }

    // open the volume like "\\.\C:"
    HANDLE handle = CreateFile(tmp, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (INVALID_HANDLE_VALUE == handle) {
        myprintf("Failed to open volume, code %d\n", GetLastError());
        return false;
    }

    if (!FlushFileBuffers(handle)) {
        myprintf("Device flush %S failed, code %d\n", tmp, GetLastError());
        CloseHandle(handle);
        return false;
    }

    myprintf("Device flush %S succeeded.\n", tmp);
    CloseHandle(handle);
    return true;
}

// searches disk volumes for the passed in name and returns the
// drive letter if found
CString FindDriveNamed(CString &volName) {
    // get a bitmask of A-Z
    DWORD dwDrives = GetLogicalDrives();

    // skip A and B, these are hardcoded floppy drives
    for (int i=2; i<26; ++i) {
        if (dwDrives & (1<<i)) {
            // this drive exists
            wchar_t buf[1024];
            DWORD size = 1024;

            CString path;
            path+=(char)('A'+i);
            path+=_T(":\\");
            // now query the volume
            if (!GetVolumeInformation(path, buf, size, NULL, NULL, NULL, NULL, 0)) {
                // skip errors, we just can't find this drive
                if (verbose) {
                    myprintf("Failed to get volume information for '%S', code %d\n", path.GetString(), GetLastError());
                }
            } else {
                // does it match?
                if (volName.CompareNoCase(buf) == 0) {
                    // yes!
                    return path;
                } else if (verbose) {
                    myprintf("'%S' did not match drive %s - '%S'\n", volName.GetString(), path.GetString(), buf);
                }
            }
        }
    }

    myprintf("No drive name matched '%S', failing.\n", volName.GetString());
    return "";
}

// Execute a program and wait for it to exit (copied from spawn)
int RunAndWait(CString &cmd, CString &args) {
    // NOTE: docs recommend initializing COM before this, in case the action requires
    // COM. Since I'm intending this for applications, I'm not going to bother.
    
    // Since I want spawn to wait for the app to exit in order to capture and return
    // an exit code, I need to use ShellExecuteEx. This is new behavior.
    SHELLEXECUTEINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_UNICODE;
    info.lpFile = cmd.GetString();
    info.lpParameters = args.GetString();
    info.nShow = SW_MINIMIZE;
    if (!ShellExecuteEx(&info)) {
        DWORD err = GetLastError();
        wprintf(L"Error %d occurred trying to execute %s.\n", err, cmd.GetString());

        // just in case, should not be true
        if (info.hProcess != NULL) {
            CloseHandle(info.hProcess);
        }

        return (int)err;
    }

    // with ShellExecute, I needed this to avoid dying too quickly. With the wait and no async and
    // all it's probably unnecessary now, but why trust Windows to be faster than you are? ;)
    Sleep(500);

    DWORD returnCode = 0;   // by default, we'll return ok if we don't know better

    if (info.hProcess != NULL) {
        // we launched, so wait on the handle (if we got one) to know when the app exits
        // inifinite waits are terrible, but it has to exit eventually. However,
        // in this case eventually could be hours or even days... (ie: backups)
        wprintf(L"Waiting...");

        WaitForSingleObject(info.hProcess, INFINITE);
        if (!GetExitCodeProcess(info.hProcess, &returnCode)) {
            returnCode = 0; // we couldn't read it for some reason... this might be bad to mask?
        }

        wprintf(L"done. Return code %d\n", returnCode);
        CloseHandle(info.hProcess);
    } else {
        wprintf(L"Did not receive a process handle\n");
    }

    return returnCode;
}
