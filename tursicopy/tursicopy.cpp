// tursicopy.cpp : Defines the entry point for the console application.
// A quick little tool to do backups with history
// It's meant to be comparable to robocopy <src> <dest> /MIR /SL /MT /R:3 /W:1
// And I don't care that it's not configurable yet. ;) I need it working now.
//
// Oh, license. Right. "Mine. If you want to use it, have fun. If you want to
// exploit it, contact me. All problems are yours, even if I made them." 
// Thanks. tursi - harmlesslion.com

#include "stdafx.h"
#include <iostream>
#include <Shellapi.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <winerror.h>
#include <vector>
#include <atlbase.h>
#include <atlconv.h>

#define MYVERSION "102"

CString src, dest, logfile;
CString workingFolder;
CString enableDevice, baseDest;
CString csApp;
bool unmountDevice, rotateOld, doBackup, deleteOld;
ULARGE_INTEGER freeUser;
ULARGE_INTEGER reserve;
int saveFolders;
int timeSlack;
int mountDelay, unmountDelay;
int lastBackup = -1;
CString fmtStr = "%s~~[%d]";
int errs = 0;
bool bAutoMode = false;
bool pauseOnErrs = true;
bool pauseAlways = false;
bool mountOk = false;
bool verbose = false;
HANDLE hLog = INVALID_HANDLE_VALUE;

// window thread
extern bool quitflag;
extern NOTIFYICONDATA icon;

struct _srcs {
    CString srcPath;
    CString destFolder;

    _srcs(CString src, CString dest) {
        srcPath = src;
        destFolder = dest;
    }
};
std::vector<_srcs> srcList;

bool CreateMessageWindow();
void WindowLoop();
void CreateTrayIcon();
void RemoveTrayIcon();

void myprintf(char *fmt, ...);
void setDefaults();
void WatchAndWait();
void SplitString(const char *inStr, CString &key, CString &val);
bool ReadProfile(const CString&);
void print_usage();
bool LoadConfig(int argc, char *argv[]);
CString formatPath(const CString& in);
bool CheckExists(const CString &in);
bool MoveToFolder(CString src, CString &dest);
void goodbye();
void RotateOldBackups(CString dest);
void MoveOneFile(CString &path, WIN32_FIND_DATA &findDat);
void ConfirmOneFile(CString& path, WIN32_FIND_DATA &findDat);
void ConfirmOneFolder(CString& path, WIN32_FIND_DATA &findDat);
void RecursivePath(CString &path, CString subPath, HANDLE hFind, WIN32_FIND_DATA& findDat, bool backingup);
bool DoNewBackup();
void DeleteOrphans();
// hardware.ccp
bool EnableDisk(const CString& instanceId, bool enable);
bool EjectDrive(CString pStr);
bool FlushDrive(CString pStr);

/////////////////////////////////////////////////////////////////////////

// helper to copy output to the log file
void myprintf(char *fmt, ...) {
    char buf[2048];
    va_list args;

    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), fmt, args);
    buf[sizeof(buf)-1]='\0';

    printf_s("%s", buf);

    if (INVALID_HANDLE_VALUE != hLog) {
        // we need to find the \n and change them to \r\n
        // WriteFile doesn't do text translation
        size_t sz=strlen(buf);
        // if there's no room left in the buffer, just give up
        char *s = buf;
        while (sz < sizeof(buf)-2) {
            char *p = strchr(s, '\n');
            if (NULL == p) break;
            memmove(p+1, p, sz-(p-buf)+1);
            *p='\r';
            ++sz;
            s=p+2;
        }
        DWORD out;
        WriteFile(hLog, buf, (DWORD)strlen(buf), &out, NULL);
    }

    // flush the console, but we let the log buffer
    fflush(stdout);

}

// from https://stackoverflow.com/questions/8046097/how-to-check-if-a-process-has-the-administrative-rights
// by Beached
BOOL IsElevated( ) {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if( OpenProcessToken( GetCurrentProcess( ),TOKEN_QUERY,&hToken ) ) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof( TOKEN_ELEVATION );
        if( GetTokenInformation( hToken, TokenElevation, &Elevation, sizeof( Elevation ), &cbSize ) ) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if( hToken ) {
        CloseHandle( hToken );
    }
    return fRet;
}

void print_usage() {
    myprintf("\ntursicopy - backs up a folder with historical backups.\n");
    myprintf("  tursicopy <src_path> <dest_path>\n    - backs up from src_path to dest_path with default values\n");
    myprintf("  tursicopy /now profile.txt\n    - backs up using a profile configuration\n");
    myprintf("  tursicopy /watch\n    - wait for a removable drive with a 'tursicopy_profile.txt' to be attached\n");
    myprintf("  tursicopy /default\n    - print the default profile.txt (copy and paste to create a new one)\n");
}

// set default values
void setDefaults() {
    // set the default values for everything
    dest = "";
    baseDest = "";
    logfile = "";
    srcList.clear();
    src = "";
    enableDevice = "";
    unmountDevice = false;
    reserve.QuadPart = 10000000;
    saveFolders = 5;
    timeSlack = 5;
    mountDelay = 5;
    unmountDelay = 30;
    rotateOld = true;
    doBackup = true;
    deleteOld = true;
    errs = 0;
    icon.cbSize = 0;
}

// doubles as a reference for what goes in there.
// Seems there is no way for a profile to backup TO the root folder...
// I think I'm okay with that...
void PrintProfile() {
    myprintf("Profile settings:\n");

    myprintf("\n[Setup]\n");
    myprintf("DestPath=%S\n", baseDest.GetString());
    myprintf("LogFile=%S\n", logfile.GetString());

    myprintf("\n[Source]\n");
    for (unsigned int idx = 0; idx < srcList.size(); ++idx) {
        myprintf("%S=%S\n", srcList[idx].destFolder.GetString(), srcList[idx].srcPath.GetString());
    }

    myprintf("\n[Paranoid]\n");
    myprintf("EnableDevice=%S\n", enableDevice.GetString());
    myprintf("UnmountDevice=%d\n", unmountDevice?1:0);
    myprintf("PauseOnErrors=%d\n", pauseOnErrs?1:0);
    myprintf("PauseAlways=%d\n", pauseAlways?1:0);
    myprintf("Verbose=%d\n", verbose?1:0);

    myprintf("\n[Tuning]\n");
    myprintf("Reserve=%llu\n", reserve.QuadPart/1000000);
    myprintf("SaveFolders=%d\n", saveFolders);
    myprintf("TimeSlack=%d\n", timeSlack);
    myprintf("MountDelay=%d\n", mountDelay);
    myprintf("UnmountDelay=%d\n", unmountDelay);
    myprintf("RotateOld=%d\n", rotateOld?1:0);
    myprintf("DoBackup=%d\n", doBackup?1:0);
    myprintf("DeleteOld=%d\n", deleteOld?1:0);
    
    myprintf("\n");
}

void SplitString(const char *inStr, CString &key, CString &val) {
    CString in = inStr;
    int p = in.Find('=');
    if (-1 == p) {
        return;
    }
    key = in.Left(p);
    val = in.Mid(p+1);
    key.Trim();
    val.Trim();
}

bool ReadProfile(const CString &profile) {
    char string[1024];
    char sectionstr[128]="NONE";
    enum {
        NONE, SETUP, SOURCE, PARANOID, TUNING
    };  // sections for easy switching
    int section = NONE;
    bool gotSomething = false;

    // go ahead and open the file - it's an INI style profile
    if (!CheckExists(profile)) {
        myprintf("Can't open profile '%S'\n", profile.GetString());
        return false;
    }

    // originally I was using GetPrivateProfileString, but the open nature of the sources
    // block means it's just easier to parse the darn thing myself...
    // Kind of wishing I didn't do sections now. Oh well ;)
    FILE *fp;
    errno_t ferr;
    ferr = _wfopen_s(&fp, profile, _T("r"));
    if (ferr) {
        myprintf("Failed to open profile '%S', code %d\n", profile.GetString(), ferr);
        return false;
    }
    while (!feof(fp)) {
        // get a line
        if (NULL == fgets(string, sizeof(string), fp)) {
            break;
        }
        // strip whitespace and skip empty
        int p = 0;
        while ((string[p])&&(string[p] < ' ')) ++p;
        if (p > 0) memmove(string, &string[p], sizeof(string)-p);
        p=(int)strlen(string);
        while ((p>0)&&(string[p-1]<' ')) {
            --p;
            string[p]='\0';
        }
        if (p <= 0) continue;
        // skip comments
        if ((string[0]==';')||(string[0]=='#')) continue;
    
        // is it a section header?
        if (string[0]=='[') {
            // yes, it is
            p=1;
            while ((p)&&(string[p]<' ')) ++p;
            if (0 == _strnicmp(&string[p], "setup", 5)) {
                section = SETUP;
            } else if (0 == _strnicmp(&string[p], "source", 6)) {
                section = SOURCE;
            } else if (0 == _strnicmp(&string[p], "paranoid", 8)) {
                section = PARANOID;
            } else if (0 == _strnicmp(&string[p], "tuning", 6)) {
                section = TUNING;
            } else {
                section = NONE;
                myprintf("Ignoring unknown section '%s'\n", string);
            }
            strcpy_s(sectionstr, string);
        } else if (section != NONE) {
            // no it's not, and we care, so assume key/value pair
            if (NULL == strchr(string, '=')) {
                myprintf("Non-key/value pair in section %s: %s\n", sectionstr, string);
                fclose(fp);
                return false;
            }
            CString key, val;
            SplitString(string, key, val);
            if (key.GetLength() < 1) {
                myprintf("Failed to parse key/value pair in section %s: %s\n", sectionstr, string);
                fclose(fp);
                return false;
            }

            switch (section) {
                case SETUP:
                    if (key.CompareNoCase(_T("DestPath")) == 0) {
                        if ((val.GetLength() < 2) || (val[1] != ':')) {
                            myprintf("DestPath must be a DOS-style absolute path [SETUP]: %s\n", string);
                            return false;
                        }
                        baseDest = val;
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("LogFile")) == 0) {
                        // this one is allowed to be empty
                        if ((val.GetLength() > 1) && (val[1] != ':')) {
                            myprintf("LogFile must be a DOS-style absolute path [SETUP]: %s\n", string);
                            return false;
                        }
                        logfile = val;
                        gotSomething = true;
                    } else {
                        myprintf("Unknown key in [SETUP]: %s\n", string);
                    }
                    break;

                case SOURCE:
                    // everything in this section is a path
                    srcList.emplace_back(val, key);
                    gotSomething = true;
                    break;

                case PARANOID:
                    if (key.CompareNoCase(_T("EnableDevice")) == 0) {
                        enableDevice = val;
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("UnmountDevice")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            unmountDevice = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            unmountDevice = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for unmount (0/1) [PARANOID]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else if (key.CompareNoCase(_T("PauseOnErrors")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            pauseOnErrs = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            pauseOnErrs = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for PauseOnErrors (0/1) [PARANOID]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else if (key.CompareNoCase(_T("PauseAlways")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            pauseAlways = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            pauseAlways = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for PauseAlways (0/1) [PARANOID]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else if (key.CompareNoCase(_T("Verbose")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            verbose = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            verbose = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for Verbose (0/1) [PARANOID]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else {
                        myprintf("Unknown key in [PARANOID]: %s\n", string);
                    }
                    break;

                case TUNING:
                    if (key.CompareNoCase(_T("Reserve")) == 0) {
                        reserve.QuadPart = _wtoi(val)*1000000ULL;
                        if (reserve.QuadPart == 0) {
                            myprintf("Invalid value for reserve [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("SaveFolders")) == 0) {
                        saveFolders = _wtoi(val);
                        if (saveFolders == 0) {
                            myprintf("Invalid value for saveFolders [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("TimeSlack")) == 0) {
                        timeSlack = _wtoi(val);
                        if (timeSlack == 0) {
                            myprintf("Invalid value for timeSlack [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("MountDelay")) == 0) {
                        mountDelay = _wtoi(val);
                        if (mountDelay == 0) {
                            myprintf("Invalid value for MountDelay [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("UnmountDelay")) == 0) {
                        unmountDelay = _wtoi(val);
                        if (unmountDelay == 0) {
                            myprintf("Invalid value for unmountDelay [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("RotateOld")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            rotateOld = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            rotateOld = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for rotateOld [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else if (key.CompareNoCase(_T("DoBackup")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            doBackup = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            doBackup = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for doBackup [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else if (key.CompareNoCase(_T("DeleteOld")) == 0) {
                        if (val.Compare(_T("0")) == 0) {
                            deleteOld = false;
                            gotSomething = true;
                        } else if (val.Compare(_T("1")) == 0) {
                            deleteOld = true;
                            gotSomething = true;
                        } else {
                            myprintf("Couldn't parse value for deleteOld [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else {
                        myprintf("Unknown key in [PARANOID]: %s\n", string);
                    }
                    break;
            }
        }
    }
    fclose(fp);

    if (!gotSomething) {
        myprintf("Didn't find any valid settings in profile file.\n");
        return false;
    }

    if ((enableDevice.GetLength() > 0) || (unmountDevice)) {
        // we need administrative powers for these options...
        if (!IsElevated()) {
            myprintf("Enabling and ejecting devices requires administrative permissions - update your startup shortcut.\n");
            return false;
        }
    }
    return true;
}

// this function's got a little magic to it... it waits for removable media
// then checks if it has a backup profile on it
void WatchAndWait() {
    // create our listen window
    if (!CreateMessageWindow()) {
        myprintf("Failed to create message window, code %d\n", GetLastError());
        ++errs;
        return;
    }

    // create a tray icon
    CreateTrayIcon();

#ifndef _DEBUG
    // close our console - we're ready to go
    FreeConsole();
#endif

    // Now we'll just loop on the message loop until it's time to exit
    while (!quitflag) {
        WindowLoop();
    }
}

// read the configuration file into the evil, nasty globals
// return false if something went wrong enough that we could tell
bool LoadConfig(int argc, char *argv[]) {
    setDefaults();
    csApp = argv[0];

    // report information
    myprintf("Tursicopy " MYVERSION ". Command line:\n");
    for (int idx=0; idx<argc; ++idx) {
        myprintf("'%s' ", argv[idx]);
    }
    myprintf("\n");

    // now based on how we were called, update those values
    if ((argc < 2)||(argv[1][0]=='?')) {
        ++errs;
        print_usage();
        return false;
	}

    // check for classic argument mode (ie: just src: dest:)
    if ((argc == 3) && (argv[1][0] != '/')) {
        src = argv[1];
        baseDest = argv[2];
        if (src.Right(1) != "\\") src+='\\';
        if (baseDest.Right(1) != "\\") baseDest+='\\';
        if (src.GetLength() < 3) {
            myprintf("Please specify a full path for source.\n");
            ++errs;
            print_usage();
            return false;
	    }
        if (baseDest.GetLength() < 3) {
            myprintf("Please specify a full path for dest.\n");
            ++errs;
            print_usage();
            return false;
	    }
        srcList.emplace_back(src, "");  // one source, from src to root folder of dest
        return true;
    }

    // no? then it must be a switch controlled mode

    // just a helper mode used to get a default profile
    if (strcmp(argv[1], "/default") == 0) {
        PrintProfile();
        return false;
    }

    // this is an internal mode used by the USB detection - it works the
    // same as 'now' but in this mode ONLY we will honor the UnmountDevice flag
    bAutoMode = false;
    if (strcmp(argv[1], "/auto") == 0) {
        bAutoMode = true;
    }

    // immediately read the profile file and execute it
    if ((strcmp(argv[1], "/now") == 0) || (strcmp(argv[1], "/auto") == 0)) {
        if (argc != 3) {
            myprintf("/now requires just one parameter, the profile to load.\n");
            ++errs;
            return false;
	    }
        CString profile = argv[2];
        if (!ReadProfile(profile)) {
            ++errs;
            return false;
	    }

        if (bAutoMode) {
            // update the dest path with the letter of the USB device
            baseDest.SetAt(0, profile[0]);
            // if there's a logfile, then update that too
            if (logfile.GetLength() > 0) {
                logfile.SetAt(0, profile[0]);
            }
        }

        if (bAutoMode) {
            // overwrite the enableDevice with the detected drive
            // the location is the drive containing the profile file
            enableDevice = profile.Left(2) + " (detected)";
        }
        if (enableDevice.GetLength() == 0) {
            // to avoid confusion later, don't unmount if we aren't mounting
            unmountDevice = false;
        }

        // we're happy then, go for it!
        return true;
    }

    // watch for a USB device to be inserted that contains a profile
    if (strcmp(argv[1], "/watch") == 0) {
        // We go wait for removable devices. This never returns.
        WatchAndWait();
        myprintf("Tursicopy /watch exitted...\n");
        return false;    // but just in case it does ;)
    }

    // At this point, we have no idea what the user wants
    myprintf("Unknown command mode.\n");
    ++errs;
    print_usage();
    return false;
}

// format a path for use
CString formatPath(const CString& in) {
    // for now, this is just adding the long filename extension
    // we check if it's already there since we pass these things around
    if (in.Left(4)=="\\\\?\\") {
        return in;
    }
    CString out = "\\\\?\\";
    out += in;
    return out;
}

// check if a file exists - PathFileExists is limited to 260 chars, this is not
bool CheckExists(const CString &in) {
    DWORD ret = GetFileAttributes(formatPath(in));
    if (ret == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if ((err == ERROR_FILE_NOT_FOUND)||(err == ERROR_PATH_NOT_FOUND)) {
            return false;
        }
        myprintf("Error locating attributes on %S, file treated as not present. Code %d\n", in.GetString(), err);
        return false;
    }
    return true;
}

// move a file to the target and ensure the path exists
// used for the backup folder
bool MoveToFolder(CString src, CString &dest) {
    int pos = 0;
    
    for (;;) {
        CString tmp;
        tmp = dest;
        int z = tmp.Find('\\', pos);
        if (z == -1) break;
        pos = z+1;
        tmp = tmp.Left(pos);
        if (tmp.Right(2) == ":\\") continue;    // We aren't allowed to create the root folder ;)
        if (false == CreateDirectory(formatPath(tmp), NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                myprintf("Failed to create folder %S, code %d\n", tmp.GetString(), GetLastError());
                ++errs;
                return false;
            }
        }
    }

    // pass empty string as src to just create the path
    if (src.GetLength() == 0) {
        return true;
    }

    if (!MoveFileEx(formatPath(src), formatPath(dest), MOVEFILE_WRITE_THROUGH)) {
        return false;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////

// always called at exit in order to check error count
void goodbye() {
    quitflag = true;
    RemoveTrayIcon();
    Sleep(100);

    // do we need to unmount? (this may cause extra errors, but we can't log them)
    if ((mountOk) && (unmountDevice)) {
        // now is it a USB unmount or a device disable?
        if (bAutoMode) {
            // USB unmount - this does not guarantee inaccessibility
            // TODO: this is not actually working - the code succeeds but the
            // drive is not removed. For now, nobody's asking for that.
//            if (!EjectDrive(enableDevice)) ++errs;
        } else {
            // flush file buffers (this does assume that the destPath points to the device with a drive letter)
            if (baseDest[1] != ':') {
                myprintf("DestPath is not a DOS device, can't request flush.\n");
            } else {
                FlushDrive(baseDest);
                myprintf("Waiting %d seconds for OS to finish flush...\n", unmountDelay);
                Sleep(unmountDelay*1000);
                EjectDrive(baseDest);
            }
            // the flush doesn't really help, though it claims success...?
            myprintf("Waiting %d seconds for OS to finish whatever...\n", unmountDelay);
            Sleep(unmountDelay*1000);
            // hardware disable - this may require reboot in ?? cases?
            // I think it's pending buffer data - the FlushDrive should help,
            // the sleep definitely does. Keeping both.
            if (!EnableDisk(enableDevice, false)) ++errs;
        }
    }

    // report
    myprintf("\n-- DONE -- %d errs.\n", errs);
    if (INVALID_HANDLE_VALUE != hLog) {
        CloseHandle(hLog);
    }

    if (((errs)&&(pauseOnErrs)) || (pauseAlways)) {
        myprintf("Press a key...\n");
        while (!_kbhit()) {}
    }

    exit(-1);
}

/////////////////////////////////////////////////////////////////////////
// startup rotation

void RotateOldBackups(CString dest) {
    myprintf("Scanning for old backup folders...\n");

    // we are just renaming things, so we know there's enough disk space here
    // backup folder names are "dest~~[x]", where 'x' is a number. Higher numbers are older. 0 is today.
    int cnt = 0;
    for (;;) {
        CString fn;
        fn.Format(fmtStr,dest,cnt);
        if (!PathFileExists(fn)) {
            break;
        }
        lastBackup = cnt;
        ++cnt;
    }

    myprintf("%d folders found.\n", lastBackup+1);

    // rename them all
    for (int idx = lastBackup; idx>=0; --idx) {
        CString oldFolder, newFolder;
        oldFolder.Format(fmtStr, dest, idx);
        newFolder.Format(fmtStr, dest, idx+1);
        if (verbose) {
            myprintf("%S -> %S\n", oldFolder.GetString(), newFolder.GetString());
        }
        if (!MoveFileEx(formatPath(oldFolder), formatPath(newFolder), MOVEFILE_WRITE_THROUGH)) {
            myprintf("- MoveFile failed, code %d\n", GetLastError());
        }
    }
    ++lastBackup;

    // create the new 0
    CString newFolder; 
    newFolder.Format(fmtStr, dest, 0);
    if (!CreateDirectory(formatPath(newFolder), NULL)) {
        myprintf("Failed to create folder 0, code %d\n", GetLastError());
        ++errs;
        goodbye();
    }
}

/////////////////////////////////////////////////////////////////////////
// Deal with backing up files

void MoveOneFile(CString &path, WIN32_FIND_DATA &findDat) {
    // copy file from src to dest - we can use the dat to check whether to do it
    // any old file is moved to the backup folder~~[0] before being replaced
    // free space is expected to be present
    CString fn = findDat.cFileName;
    CString srcFile = src + path + fn;
    CString destFile = dest + path + fn;
    CString backupFile; backupFile.Format(fmtStr, baseDest, 0); backupFile+='\\'; backupFile+=workingFolder; backupFile += path; backupFile+=fn;
    USES_CONVERSION;

    if (PathFileExists(destFile)) {
        // get the file information and see if it's stale
        HANDLE hFile = CreateFile(formatPath(destFile), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (INVALID_HANDLE_VALUE == hFile) {
            myprintf("Failed to open old dest file, though it exists. Code %d\n", GetLastError());
            ++errs;
            return;
        }
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(hFile, &info)) {
            myprintf("Failed to get old dest file information, skipping. Code %d\n", GetLastError());
            ++errs;
            return;
        }
        CloseHandle(hFile);

        // got the file, check if the size is different or the date is different
        // copies can reduce the modify time to a 2 second quantum, we'll use 5s
        LONGLONG diffInTicks =
            reinterpret_cast<LARGE_INTEGER*>(&info.ftLastWriteTime)->QuadPart -
            reinterpret_cast<LARGE_INTEGER*>(&findDat.ftLastWriteTime)->QuadPart;
        bool timeOk;
        if (timeSlack < 0) {
            // ignore time difference
            timeOk = true;
        } else {
            timeOk = (diffInTicks > -timeSlack*10000000) &&
            (diffInTicks < timeSlack*10000000);
        }

        if ((timeOk) && 
            (info.nFileSizeHigh == findDat.nFileSizeHigh) && 
            (info.nFileSizeLow == findDat.nFileSizeLow)) {
            if (verbose) {
                myprintf("SAME: %s\n", W2A(destFile.GetString()));
            }
            return;
        }

        myprintf("BACK: %s -> %s\n", W2A(destFile.GetString()), W2A(backupFile.GetString()));
        if (!MoveToFolder(destFile, backupFile)) {
            myprintf("** Failed to move file -- not copied! Code %d\n", GetLastError());
            ++errs;
            return;
        }
    }

    // now check if we have enough disk space
    ULARGE_INTEGER filesize;
    filesize.HighPart = findDat.nFileSizeHigh;
    filesize.LowPart = findDat.nFileSizeLow;
    // keep configurable slack
    while (filesize.QuadPart+reserve.QuadPart >= freeUser.QuadPart) {
        ULARGE_INTEGER totalBytes, freeBytes;

        myprintf("* Freeing disk space, deleting backup folder %d... ", lastBackup);

        // remove the oldest folder, but keep a minimum count
        if (lastBackup <= saveFolders) {
            myprintf("\n** Not enough backup folders left to free space - aborting.\n");
            ++errs;
            goodbye();
        }

        CString oldFolder;
        oldFolder.Format(fmtStr, baseDest, lastBackup);
        oldFolder+='\0';
        SHFILEOPSTRUCT op;
        op.hwnd = NULL;
        op.wFunc = FO_DELETE;
        op.pFrom = oldFolder.GetString();   // does not support \\?\ for long filenames
        op.pTo = NULL;
        op.fFlags = FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_NO_UI|FOF_SILENT;
        op.fAnyOperationsAborted = FALSE;
        op.hNameMappings = 0;
        op.lpszProgressTitle = _T("");
        int ret = SHFileOperation(&op);
        if (ret) {
            myprintf("\n* Deletion error (special) code %d\n", ret);
            if (oldFolder.GetLength() >= MAX_PATH) {
                myprintf("(The filepath may be too long - you can try deleting the folder by hand and then restarting.\n");
            }
            ++errs;
            goodbye();
        }

        // a little loop to watch for the free disk to stop changing - can take a little time
        ULARGE_INTEGER old;
        do {
            old.QuadPart = freeUser.QuadPart;
            Sleep(100);
            // update the free disk space
            if (!GetDiskFreeSpaceEx(dest, &freeUser, &totalBytes, &freeBytes)) {
                myprintf("\nFailed. Error %d\n", GetLastError());
                ++errs;
                goodbye();
            }
        } while (old.QuadPart != freeUser.QuadPart);
        --lastBackup;
        myprintf("Free space now %llu\n", freeUser.QuadPart);
    }

    // finally do the copy
    myprintf("COPY: %s -> %s\n", W2A(srcFile.GetString()), W2A(destFile.GetString()));
    BOOL cancel = FALSE;
#if 0
    // CopyFile2 can preserve attributes (like symlinks)! But requires Windows 8.
    COPYFILE2_EXTENDED_PARAMETERS param;
    param.dwSize=sizeof(param);
    param.dwCopyFlags = COPY_FILE_COPY_SYMLINK | COPY_FILE_FAIL_IF_EXISTS;
    param.pfCancel = FALSE;
    param.pProgressRoutine = NULL;
    param.pvCallbackContext = NULL;
    if (!SUCCEEDED(CopyFile2(formatPath(srcFile), formatPath(destFile), &param))) {
#else
    if (!CopyFile(formatPath(srcFile), formatPath(destFile), TRUE)) {
#endif
        myprintf("** Failed to copy file -- Code %d\n", GetLastError());
        ++errs;
        return;
    }
    freeUser.QuadPart -= filesize.QuadPart;
}

// if not backing up, then we're deleting orphans
void ConfirmOneFile(CString& path, WIN32_FIND_DATA &findDat);
void ConfirmOneFolder(CString& path, WIN32_FIND_DATA &findDat);
void RecursivePath(CString &path, CString subPath, HANDLE hFind, WIN32_FIND_DATA& findDat, bool backingup) {
    // run the current path into the ground ;)
    if (path.Right(1) != "\\") path+='\\';
    if ((subPath.GetLength() > 0) && (subPath.Right(1) != "\\")) subPath+='\\';

    do {
        if (findDat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // skip . and ..
            if ((findDat.cFileName[0] == '.')&&(findDat.cFileName[1]=='\0')) continue;
            if ((findDat.cFileName[0] == '.')&&(findDat.cFileName[1]=='.')&&(findDat.cFileName[2]=='\0')) continue;
            // skip system volume information
            if (0 == wcscmp(findDat.cFileName, _T("System Volume Information"))) continue;
            // skip recycle bins
            if (0 == wcscmp(findDat.cFileName, _T("$RECYCLE.BIN"))) continue;
            // skip backup folders
            wchar_t* p=wcschr(findDat.cFileName, _T('~'));
            if (p != NULL) {
                int d;
                if (1 == swscanf_s(p, fmtStr.Mid(2).GetString(), &d)) {
                    continue;
                }
            }
            if (findDat.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
            if (findDat.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (findDat.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) continue;

            if (verbose) {
                myprintf("%S%S%S\n", path.GetString(), subPath.GetString(), findDat.cFileName);
            }

            // make sure this folder exists on the target
            if (backingup) {
                CString newFolder = dest + subPath + findDat.cFileName;
                if (!CreateDirectory(formatPath(newFolder), NULL)) {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) {
                        myprintf("Failed trying to create folder %S, code %d\n", newFolder.GetString(), GetLastError());
                        ++errs;
                    }
                }
            }

            // start a new search
            CString newSubPath = subPath + findDat.cFileName;
            CString srchPath = path + newSubPath + "\\*";
            WIN32_FIND_DATA newFind;
            HANDLE hFind2 = FindFirstFile(formatPath(srchPath), &newFind);
            if (INVALID_HANDLE_VALUE == hFind2) {
                myprintf("Failed to start subdir search. Code %d\n", GetLastError());
                ++errs;
                continue;
            }
            RecursivePath(path, newSubPath, hFind2, newFind, backingup);
            FindClose(hFind2);

            // if not backing up, we might need to remove this folder
            if (!backingup) {
                ConfirmOneFolder(subPath, findDat);
            }

            continue;
        }

        if (backingup) {
            MoveOneFile(subPath, findDat);
        } else {
            ConfirmOneFile(subPath, findDat);
        }
    } while (FindNextFile(hFind, &findDat));
}

bool DoNewBackup() {
    // recursively copy any changed files (by size or timestamp) from the old folder to the new.
    // returns false if the copy could not start -- this prevents an orphan purge from running
    CString search = src + "*";
    WIN32_FIND_DATA findDat;

    myprintf("Searching for new or changed files...\n");
    
    HANDLE hFind = FindFirstFile(formatPath(search), &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        myprintf("Failed to open search: code %d\n", GetLastError());
        return false;
    }
    RecursivePath(src, "", hFind, findDat, true);
    FindClose(hFind);
    return true;
}

/////////////////////////////////////////////////////////////////////////
// Deal with deleting (well, backing up) orphaned files

void DeleteOrphans() {
    // similar to backup, but runs backwards and moves any files
    // that were no longer in the src folder
    myprintf("Remove orphaned files...\n");

    // first let's just make sure the source folder exists...
    // if it doesn't on purpose, delete the files by hand
    {
        CString search = src + "*";
        WIN32_FIND_DATA findDat;

        if (verbose) {
            myprintf("Checking source folder exists...\n");
        }
    
        HANDLE hFind = FindFirstFile(formatPath(search), &findDat);
        if (INVALID_HANDLE_VALUE == hFind) {
            myprintf("Failed to open source folder: code %d\n", GetLastError());
            return;
        }
        FindClose(hFind);
    }

    CString search = dest + "*";
    WIN32_FIND_DATA findDat;
    HANDLE hFind = FindFirstFile(formatPath(search), &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        myprintf("Failed to open dest search: code %d\n", GetLastError());
        return;
    }
    RecursivePath(dest, "", hFind, findDat, false);
    FindClose(hFind);
}

// check if the passed in file exists in src. If it does not, move it.
void ConfirmOneFile(CString &path, WIN32_FIND_DATA &findDat) {
    // copy file from src to dest - we can use the dat to check whether to do it
    // any old file is moved to the backup folder~~[0] before being replaced
    // free space is expected to be present
    USES_CONVERSION;
    CString fn = findDat.cFileName;
    CString srcFile = src + path + fn;
    CString destFile = dest + path + fn;
    CString backupFile; backupFile.Format(fmtStr, baseDest, 0); backupFile+='\\'; backupFile+=workingFolder; backupFile += path; backupFile+=fn;

    if (!CheckExists(srcFile)) {
        myprintf("NUKE: %s -> %s\n", W2A(destFile.GetString()), W2A(backupFile.GetString()));
        if (!MoveToFolder(destFile, backupFile)) {
            myprintf("** Failed to move file -- not copied! Code %d\n", GetLastError());
            ++errs;
            return;
        }
    }
}

// check if the passed in folder exists in src. If it does not, delete it (if there were any files, they were moved)
// note that this (among other things) means we do not preserve empty folders in the backup.
void ConfirmOneFolder(CString &path, WIN32_FIND_DATA &findDat) {
    // remove folder src - we can use the dat to check whether to do it
    CString fn = findDat.cFileName;
    CString srcFile = src + path + fn;
    CString destFile = dest + path + fn;

    if (!CheckExists(srcFile)) {
        myprintf("NUKE: %S\n", destFile.GetString());
        if (!RemoveDirectory(formatPath(destFile))) {
            myprintf("** Failed to remove orphan folder! Code %d\n", GetLastError());
            ++errs;
            return;
        }
    }
}

void ProcessInsert(wchar_t letter) {
    // check whether drive letter has a tursicopy.txt profile on it
    CString csPath = letter;
    csPath += ":\\tursicopy.txt";

    if (!CheckExists(csPath)) {
        // nope, just ignore it
        if (verbose) {
            myprintf("%S is not present, skipping\n", csPath);
        }
        return;
    }
    
    // yes, it does exist! So we need to launch the tool.
    CString csCmd = "\"";
    csCmd += csApp;
    csCmd += "\" /auto ";
    csCmd += csPath;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    wchar_t runCmd[1024];

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );

    wcscpy_s(runCmd, csCmd.GetString());
    myprintf("Executing %S\n", runCmd);

    if (!CreateProcess(NULL, runCmd, NULL, NULL, TRUE, CREATE_NEW_CONSOLE|CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CString msg;
        msg.Format(_T("Failed to create process. Code %d"), GetLastError());
        myprintf("%S\n", msg.GetString());
        // we probably don't have a console to report to
        MessageBox(NULL, msg, _T("Failed to launch Tursicopy"), MB_OK);
        return;
    }
}

/////////////////////////////////////////////////////////////////////////
// startup

int main(int argc, char *argv[])
{
    ULARGE_INTEGER totalBytes, freeBytes;
    USES_CONVERSION;

	if (!LoadConfig(argc, argv)) {
        goodbye();
        return -1;
	}
    if (baseDest.Right(1) != '\\') baseDest+='\\';

    // before we do anything else, redirect output (if there is a logFile)
    if (logfile.GetLength() > 0) {
        myprintf("Logging to: %S\n", logfile.GetString());
        hLog = CreateFile(logfile, FILE_GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hLog == INVALID_HANDLE_VALUE) {
            myprintf("Failed to open log, code %d\n", GetLastError);
            ++errs;
        }
    }
    PrintProfile();

    // check whether we need to mount the backup device
    // (in auto mode, enableDevice is the inserted memory stick)
    if ((enableDevice.GetLength() > 0) && (!bAutoMode)) {
        if (!EnableDisk(enableDevice, true)) {
            myprintf("Failed to enable device, can not proceed.\n");
            ++errs;
            goodbye();
        }
        // we should pause to let things sync up
        myprintf("Waiting %d seconds for OS to finish setup...\n", mountDelay);
        Sleep(mountDelay*1000);
    }
    mountOk = true;

    // Get started
    myprintf("Preparing backup folder %s\n", W2A(baseDest.GetString()));
    
    // make sure the destination folder exists - need this before we check disk space
    // if we just enabled, we may need a few seconds before this works, so we'll loop
    // on error 3 only
    for (int errCnt = 10; errCnt >= 0; --errCnt) {
        myprintf("Verifying target folder...\n");
        if (!MoveToFolder(_T(""), baseDest)) {
            DWORD err = GetLastError();
            if (((err != ERROR_PATH_NOT_FOUND) &&
                 (err != ERROR_FILE_NOT_FOUND) && 
                 (err != ERROR_NOT_READY) ) || (errCnt <= 0)) {
                myprintf("Failed to create target folder. Can't continue.\n");
                ++errs;
                goodbye();
                return -1;
            }

            if (verbose) {
                myprintf("Retrying, code %d\n", err);
            }
            Sleep(1000);
            continue;
        }

        myprintf("Checking destination free disk space... ");
        if (!GetDiskFreeSpaceEx(baseDest, &freeUser, &totalBytes, &freeBytes)) {
            DWORD err = GetLastError();
            if (((err != ERROR_PATH_NOT_FOUND) &&
                 (err != ERROR_NOT_READY) ) || (errCnt <= 0)) {
                myprintf("Failed. Error %d\n", GetLastError());
                ++errs;
                goodbye();
                return -1;
            }
            if (verbose) {
                myprintf("Retrying, code %d\n", err);
            }
            Sleep(1000);
            continue;
        }

        break;
    }

    myprintf("Got %llu bytes.\n", freeUser.QuadPart);
    if (freeUser.QuadPart != freeBytes.QuadPart) {
        myprintf("* Warning: user may have quotas. Disk free is %llu\n", freeBytes.QuadPart);
    }

    if (totalBytes.QuadPart == 0) {
        myprintf("* Something went wrong - total disk size is zero bytes. Aborting.\n");
        ++errs;
        goodbye();
        return -1;
    }

    // do each step as requested
    if ((!rotateOld)&&(!doBackup)&&(!deleteOld)) {
        myprintf("No actual actions were set to be done!\n");
        ++errs;
    }

    if (rotateOld) {
        // rotate old backup folders just once per run
        RotateOldBackups(baseDest);
    }

    if ((!doBackup)&&(!deleteOld)) {
        myprintf("No backup or orphan check requested, ignoring source list.\n");
    } else {
        // this section rolls through the list of source folders
        for (unsigned int idx = 0; idx < srcList.size(); ++idx) {
            // set up src and dest for this instance
            src = srcList[idx].srcPath;
            if (src.Right(1) != '\\') src+='\\';
            dest = baseDest + srcList[idx].destFolder;
            if (dest.Right(1) != '\\') dest+='\\';
            workingFolder = srcList[idx].destFolder;
            if (workingFolder.Right(1) != '\\') workingFolder+='\\';

            myprintf("Going to work from %s to %s\n", W2A(src.GetString()), W2A(dest.GetString()));

            // make sure this destination folder exists
            if (!MoveToFolder(_T(""), dest)) {
                myprintf("Failed to create target folder. Can't continue.\n");
                ++errs;
                goodbye();
                return -1;
            }

            if (doBackup) { 
                if (!DoNewBackup()) {
                    if (deleteOld) {
                        myprintf("Failed to start backup, skipping purge of orphans!\n");
                        deleteOld = false;
                    }
                }
            }
            if (deleteOld) {
                DeleteOrphans();
            }
        }
    }

    goodbye();

    return 0;
}
