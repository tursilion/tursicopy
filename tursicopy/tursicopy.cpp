// tursicopy.cpp : Defines the entry point for the console application.
// A quick little tool to do backups with history
// It's meant to be comparable to robocopy <src> <dest> /MIR /SL /MT /R:3 /W:1
// And I don't care that it's not configurable yet. ;) I need it working now.
//
// Oh, license. Right. "Mine. If you want to use it, have fun. If you want to
// exploit it, contact me." Thanks. tursi - harmlesslion.com

// almost big enough to start breaking up... ;)

#include "stdafx.h"
#include <iostream>
#include <Shellapi.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <winerror.h>
#include <vector>

CString src, dest, logfile;
CString workingFolder;
CString enableDevice, baseDest;
bool unmountDevice, rotateOld, doBackup, deleteOld;
ULARGE_INTEGER freeUser;
ULARGE_INTEGER reserve;
int saveFolders;
int timeSlack;
int lastBackup = -1;
CString fmtStr = "%s~~[%d]";
int errs = 0;

struct _srcs {
    CString srcPath;
    CString destFolder;

    _srcs(CString src, CString dest) {
        srcPath = src;
        destFolder = dest;
    }
};
std::vector<_srcs> srcList;

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
void RotateOldBackups();
void MoveOneFile(CString &path, WIN32_FIND_DATA &findDat);
void ConfirmOneFile(CString& path, WIN32_FIND_DATA &findDat);
void ConfirmOneFolder(CString& path, WIN32_FIND_DATA &findDat);
void RecursivePath(CString &path, CString subPath, HANDLE hFind, WIN32_FIND_DATA& findDat, bool backingup);
void DoNewBackup();
void DeleteOrphans();

/////////////////////////////////////////////////////////////////////////

void print_usage() {
    printf_s("tursicopy - backs up a folder with historical backups.\n");
    printf_s("  tursicopy <src_path> <dest_path>\n    - backs up from src_path to dest_path with default values\n");
    printf_s("  tursicopy /now profile.txt\n    - backs up using a profile configuration\n");
    printf_s("  tursicopy /watch\n    - wait for a removable drive with a 'tursicopy_profile.txt' to be attached\n");
    printf_s("\nUsing a profile for timed backups is suggested to be a good idea - it MAY help\n");
    printf_s("protect against ransomware? (If the tool can't read the profile, it won't overwrite the backup!\n");
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
    rotateOld = true;
    doBackup = true;
    deleteOld = true;
    errs = 0;
}

// doubles as a reference for what goes in there.
// Seems there is no way for a profile to backup TO the root folder...
// I think I'm okay with that...
void PrintProfile() {
    printf_s("Profile settings:\n");

    printf_s("\n[Setup]\n");
    printf_s("DestPath=%S\n", baseDest.GetString());
    printf_s("LogFile=%S\n", logfile.GetString());

    printf_s("\n[Source]\n");
    for (unsigned int idx = 0; idx < srcList.size(); ++idx) {
        printf_s("%S=%S\n", srcList[idx].destFolder.GetString(), srcList[idx].srcPath.GetString());
    }

    printf_s("\n[Paranoid]\n");
    printf_s("EnableDevice=%S\n", enableDevice.GetString());
    printf_s("UnmountDevice=%d\n", unmountDevice?1:0);

    printf_s("\n[Tuning]\n");
    printf_s("Reserve=%llu\n", reserve.QuadPart/1000000);
    printf_s("SaveFolders=%d\n", saveFolders);
    printf_s("TimeSlack=%d\n", timeSlack);
    printf_s("RotateOld=%d\n", rotateOld?1:0);
    printf_s("DoBackup=%d\n", doBackup?1:0);
    printf_s("DeleteOld=%d\n", deleteOld?1:0);
    
    printf_s("\n");
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
        printf("Can't open profile '%S'\n", profile.GetString());
        return false;
    }

    // originally I was using GetPrivateProfileString, but the open nature of the sources
    // block means it's just easier to parse the darn thing myself...
    // Kind of wishing I didn't do sections now. Oh well ;)
    FILE *fp;
    errno_t ferr;
    ferr = _wfopen_s(&fp, profile, _T("r"));
    if (ferr) {
        printf("Failed to open profile '%S', code %d\n", profile.GetString(), ferr);
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
        p=strlen(string);
        while ((p>0)&&(string[p-1]<' ')) {
            --p;
            string[p]='\0';
        }
        if (p <= 0) continue;
    
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
                printf("Ignoring unknown section '%s'\n", string);
            }
            strcpy_s(sectionstr, string);
        } else if (section != NONE) {
            // no it's not, and we care, so assume key/value pair
            if (NULL == strchr(string, '=')) {
                printf("Non-key/value pair in section %s: %s\n", sectionstr, string);
                fclose(fp);
                return false;
            }
            CString key, val;
            SplitString(string, key, val);
            if (key.GetLength() < 1) {
                printf("Failed to parse key/value pair in section %s: %s\n", sectionstr, string);
                fclose(fp);
                return false;
            }

            switch (section) {
                case SETUP:
                    if (key.CompareNoCase(_T("DestPath")) == 0) {
                        if ((val.GetLength() < 2) || (val[1] != ':')) {
                            printf("DestPath must be a DOS-style absolute path [SETUP]: %s\n", string);
                            return false;
                        }
                        baseDest = val;
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("LogFile")) == 0) {
                        // this one is allowed to be empty
                        if ((val.GetLength() > 1) && (val[1] != ':')) {
                            printf("LogFile must be a DOS-style absolute path [SETUP]: %s\n", string);
                            return false;
                        }
                        logfile = val;
                        gotSomething = true;
                    } else {
                        printf("Unknown key in [SETUP]: %s\n", string);
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
                            printf("Couldn't parse value for unmount (0/1) [PARANOID]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else {
                        printf("Unknown key in [PARANOID]: %s\n", string);
                    }
                    break;

                case TUNING:
                    if (key.CompareNoCase(_T("Reserve")) == 0) {
                        reserve.QuadPart = _wtoi(val)*1000000ULL;
                        if (reserve.QuadPart == 0) {
                            printf("Invalid value for reserve [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("SaveFolders")) == 0) {
                        saveFolders = _wtoi(val);
                        if (saveFolders == 0) {
                            printf("Invalid value for saveFolders [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                        gotSomething = true;
                    } else if (key.CompareNoCase(_T("TimeSlack")) == 0) {
                        timeSlack = _wtoi(val);
                        if (timeSlack == 0) {
                            printf("Invalid value for timeSlack [TUNING]: %s\n", string);
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
                            printf("Couldn't parse value for rotateOld [TUNING]: %s\n", string);
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
                            printf("Couldn't parse value for doBackup [TUNING]: %s\n", string);
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
                            printf("Couldn't parse value for deleteOld [TUNING]: %s\n", string);
                            fclose(fp);
                            return false;
                        }
                    } else {
                        printf("Unknown key in [PARANOID]: %s\n", string);
                    }
                    break;
            }
        }
    }
    fclose(fp);

    if (!gotSomething) {
        printf("Didn't find any valid settings in profile file.\n");
        return false;
    }

    return true;
}

// this function's got a little magic to it... it waits for removable media
// then checks if it has a backup profile on it
void WatchAndWait() {
    // create a tray icon
    // close our console
    // wait for a removable media device
    // see if it's for us...
    // open console
    // spawn the copy
    // close the console again
    // and go back to waiting...
}

// read the configuration file into the evil, nasty globals
// return false if something went wrong enough that we could tell
bool LoadConfig(int argc, char *argv[]) {
    setDefaults();

    // now based on how we were called, update those values
    if ((argc < 2)||(argv[1][0]=='?')) {
        ++errs;
        return false;
	}

    // check for classic argument mode
    if ((argc == 3) && (argv[1][0] != '/')) {
        src = argv[1];
        baseDest = argv[2];
        if (src.Right(1) != "\\") src+='\\';
        if (baseDest.Right(1) != "\\") baseDest+='\\';
        if (src.GetLength() < 3) {
            printf("Please specify a full path for source.\n");
            ++errs;
            return false;
	    }
        if (baseDest.GetLength() < 3) {
            printf("Please specify a full path for dest.\n");
            ++errs;
            return false;
	    }
        srcList.emplace_back(src, "");  // one source, from src to root folder of dest
        return true;
    }

    // no? then it must be a switch controlled mode
    if (strcmp(argv[1], "/now") == 0) {
        if (argc != 3) {
            printf("/now requires just one parameter, the profile to load.\n");
            ++errs;
            return false;
	    }
        CString profile = argv[2];
        if (!ReadProfile(profile)) {
            ++errs;
            return false;
	    }
        // we're happy then, go for it!
        return true;
    }

    if (strcmp(argv[1], "/watch") == 0) {
        // We go wait for removable devices. This never returns.
        WatchAndWait();
        printf("Tursicopy /watch unexpectedly exitted...\n");
        ++errs;
        return false;    // but just in case it does ;)
    }

    // At this point, we have no idea what the user wants
    printf("Unknown command mode.\n");
    ++errs;
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
        printf("Error locating attributes on %S, file treated as not present. Code %d\n", in.GetString(), err);
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
                printf_s("Failed to create folder %S, code %d\n", tmp.GetString(), GetLastError());
                ++errs;
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
    printf_s("\n-- DONE -- %d errs.\n", errs);
#ifdef _DEBUG
    if (errs) {
        printf_s("Press a key...\n");
        while (!_kbhit()) {}
    }
#endif
    exit(-1);
}

/////////////////////////////////////////////////////////////////////////
// startup rotation

void RotateOldBackups(CString dest) {
    printf_s("Scanning for old backup folders...\n");

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

    printf_s("%d folders found.\n", lastBackup+1);

    // rename them all
    for (int idx = lastBackup; idx>=0; --idx) {
        CString oldFolder, newFolder;
        oldFolder.Format(fmtStr, dest, idx);
        newFolder.Format(fmtStr, dest, idx+1);
#ifdef _DEBUG
        printf_s("%S -> %S\n", oldFolder.GetString(), newFolder.GetString());
#endif
        if (!MoveFileEx(formatPath(oldFolder), formatPath(newFolder), MOVEFILE_WRITE_THROUGH)) {
            printf_s("- MoveFile failed, code %d\n", GetLastError());
        }
    }
    ++lastBackup;

    // create the new 0
    CString newFolder; 
    newFolder.Format(fmtStr, dest, 0);
    if (!CreateDirectory(formatPath(newFolder), NULL)) {
        printf_s("Failed to create folder 0, code %d\n", GetLastError());
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

    if (PathFileExists(destFile)) {
        // get the file information and see if it's stale
        HANDLE hFile = CreateFile(formatPath(destFile), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (INVALID_HANDLE_VALUE == hFile) {
            printf_s("Failed to open old dest file, though it exists. Code %d\n", GetLastError());
            ++errs;
            return;
        }
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(hFile, &info)) {
            printf_s("Failed to get old dest file information, skipping. Code %d\n", GetLastError());
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
#ifdef _DEBUG
            printf_s("SAME: %S\n", destFile.GetString());
#endif
            return;
        }

        printf_s("BACK: %S -> %S\n", destFile.GetString(), backupFile.GetString());
        if (!MoveToFolder(destFile, backupFile)) {
            printf_s("** Failed to move file -- not copied! Code %d\n", GetLastError());
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

        printf_s("* Freeing disk space, deleting backup folder %d... ", lastBackup);

        // remove the oldest folder, but keep a minimum count
        if (lastBackup <= saveFolders) {
            printf_s("\n** Not enough backup folders left to free space - aborting.\n");
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
            printf_s("\n* Deletion error (special) code %d\n", ret);
            if (oldFolder.GetLength() >= MAX_PATH) {
                printf_s("(The filepath may be too long - you can try deleting the folder by hand and then restarting.\n");
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
                printf_s("\nFailed. Error %d\n", GetLastError());
                ++errs;
                goodbye();
            }
        } while (old.QuadPart != freeUser.QuadPart);
        --lastBackup;
        printf_s("Free space now %llu\n", freeUser.QuadPart);
    }

    // finally do the copy
    printf_s("COPY: %S -> %S\n", srcFile.GetString(), destFile.GetString());
    BOOL cancel = FALSE;
    // CopyFile2 can preserve attributes!
    COPYFILE2_EXTENDED_PARAMETERS param;
    param.dwSize=sizeof(param);
    param.dwCopyFlags = COPY_FILE_COPY_SYMLINK | COPY_FILE_FAIL_IF_EXISTS;
    param.pfCancel = FALSE;
    param.pProgressRoutine = NULL;
    param.pvCallbackContext = NULL;
    if (!SUCCEEDED(CopyFile2(formatPath(srcFile), formatPath(destFile), &param))) {
        printf_s("** Failed to copy file -- Code %d\n", GetLastError());
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

#ifdef _DEBUG
            printf_s("%S%S%S\n", path.GetString(), subPath.GetString(), findDat.cFileName);
#endif

            // make sure this folder exists on the target
            if (backingup) {
                CString newFolder = dest + subPath + findDat.cFileName;
                if (!CreateDirectory(formatPath(newFolder), NULL)) {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) {
                        printf_s("Failed trying to create folder %S, code %d\n", newFolder.GetString(), GetLastError());
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
                printf_s("Failed to start subdir search. Code %d\n", GetLastError());
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

void DoNewBackup() {
    // recursively copy any changed files (by size or timestamp) from the old folder to the new.
    CString search = src + "*";
    WIN32_FIND_DATA findDat;

    printf_s("Searching for new or changed files...\n");

    HANDLE hFind = FindFirstFile(formatPath(search), &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        printf_s("Failed to open search: code %d\n", GetLastError());
        return;
    }
    RecursivePath(src, "", hFind, findDat, true);
    FindClose(hFind);
}

/////////////////////////////////////////////////////////////////////////
// Deal with deleting (well, backing up) orphaned files

void DeleteOrphans() {
    // similar to backup, but runs backwards and moves any files
    // that were no longer in the src folder
    printf_s("Remove orphaned files...\n");

    CString search = dest + "*";
    WIN32_FIND_DATA findDat;
    HANDLE hFind = FindFirstFile(formatPath(search), &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        printf_s("Failed to open dest search: code %d\n", GetLastError());
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
    CString fn = findDat.cFileName;
    CString srcFile = src + path + fn;
    CString destFile = dest + path + fn;
    CString backupFile; backupFile.Format(fmtStr, baseDest, 0); backupFile+='\\'; backupFile+=workingFolder; backupFile += path; backupFile+=fn;

    if (!CheckExists(srcFile)) {
        printf_s("NUKE: %S -> %S\n", destFile.GetString(), backupFile.GetString());
        if (!MoveToFolder(destFile, backupFile)) {
            printf_s("** Failed to move file -- not copied! Code %d\n", GetLastError());
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
        printf_s("NUKE: %S\n", destFile.GetString());
        if (!RemoveDirectory(formatPath(destFile))) {
            printf_s("** Failed to remove orphan folder! Code %d\n", GetLastError());
            ++errs;
            return;
        }
    }
}


/////////////////////////////////////////////////////////////////////////
// startup

int main(int argc, char *argv[])
{
    ULARGE_INTEGER totalBytes, freeBytes;

	if (!LoadConfig(argc, argv)) {
        print_usage();
        return -1;
	}
    if (baseDest.Right(1) != '\\') baseDest+='\\';
    PrintProfile();

    printf_s("Preparing backup folder %S\n", baseDest.GetString());
    
    // make sure the destination folder exists - need this before we check disk space
    if (!MoveToFolder(_T(""), baseDest)) {
        printf_s("Failed to create target folder. Can't continue.\n");
        return -1;
    }

    printf_s("Checking destination free disk space... ");
    if (!GetDiskFreeSpaceEx(baseDest, &freeUser, &totalBytes, &freeBytes)) {
        printf_s("Failed. Error %d\n", GetLastError());
        return -1;
    }

    printf_s("Got %llu bytes.\n", freeUser.QuadPart);
    if (freeUser.QuadPart != freeBytes.QuadPart) {
        printf_s("* Warning: user may have quotas. Disk free is %llu\n", freeBytes.QuadPart);
    }

    if (totalBytes.QuadPart == 0) {
        printf_s("* Something went wrong - total disk size is zero bytes. Aborting.\n");
        return -1;
    }

    // do each step as requested
    if ((!rotateOld)&&(!doBackup)&&(!deleteOld)) {
        printf("No actual actions were set to be done!\n");
        ++errs;
    }

    if (rotateOld) {
        // rotate old backup folders just once per run
        RotateOldBackups(baseDest);
    }

    if ((!doBackup)&&(!deleteOld)) {
        printf("No backup or orphan check requested, ignoring source list.\n");
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

            printf_s("Going to work from %S to %S\n", src.GetString(), dest.GetString());

            // make sure this destination folder exists
            if (!MoveToFolder(_T(""), dest)) {
                printf_s("Failed to create target folder. Can't continue.\n");
                return -1;
            }

            if (doBackup) { 
                DoNewBackup();
            }
            if (deleteOld) {
                DeleteOrphans();
            }
        }
    }

    goodbye();

    return 0;
}

