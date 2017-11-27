// tursicopy.cpp : Defines the entry point for the console application.
// A quick little tool to do backups with history
// It's meant to be comparable to robocopy <src> <dest> /MIR /SL /MT /R:3 /W:1
// And I don't care that it's not configurable yet. ;) I need it working now.
//
// Oh, license. Right. "Mine. If you want to use it, have fun. If you want to
// exploit it, contact me." Thanks. tursi - harmlesslion.com

#include "stdafx.h"
#include <iostream>
#include <Shellapi.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <winerror.h>

CString src, dest;
ULARGE_INTEGER freeUser;
int lastBackup = -1;
CString fmtStr = "%s~~[%d]";
int errs = 0;

/////////////////////////////////////////////////////////////////////////
 
bool MoveToFolder(CString src, CString &dest) {
    // move a file to the target and ensure the path exists
    // used for the backup folder
    int pos = 0;
    
    for (;;) {
        CString tmp;
        tmp = dest;
        int z = tmp.Find('\\', pos);
        if (z == -1) break;
        pos = z+1;
        tmp = tmp.Left(pos);
        if (tmp.Right(2) == ":\\") continue;    // We aren't allowed to create the root folder ;)
        if (false == CreateDirectory(tmp, NULL)) {
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

    if (!MoveFileEx(src, dest, MOVEFILE_WRITE_THROUGH)) {
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

void RotateOldBackups() {
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
        if (!MoveFileEx(oldFolder, newFolder, MOVEFILE_WRITE_THROUGH)) {
            printf_s("- MoveFile failed, code %d\n", GetLastError());
        }
    }
    ++lastBackup;

    // create the new 0
    CString newFolder; 
    newFolder.Format(fmtStr, dest, 0);
    if (!CreateDirectory(newFolder, NULL)) {
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
    CString backupFile; backupFile.Format(fmtStr, dest, 0); backupFile+="\\"; backupFile += path; backupFile+=fn;

    if (PathFileExists(destFile)) {
        // get the file information and see if it's stale
        HANDLE hFile = CreateFile(destFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
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
        if ((diffInTicks > -50000000) &&
            (diffInTicks < 50000000) &&
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
    // keep 10MB of slack
    while (filesize.QuadPart+10000000 >= freeUser.QuadPart) {
        ULARGE_INTEGER totalBytes, freeBytes;

        // remove the oldest folder, but keep at least 5
        // keeps 10MB free
        if (lastBackup <= 5) {
            printf_s("** Not enough backup folders left to free space - aborting.\n");
            ++errs;
            goodbye();
        }
        printf_s("* Freeing disk space, deleting backup folder %d... ", lastBackup);

        CString oldFolder;
        oldFolder.Format(fmtStr, dest, lastBackup);
        oldFolder+='\0';
        SHFILEOPSTRUCT op;
        op.hwnd = NULL;
        op.wFunc = FO_DELETE;
        op.pFrom = oldFolder.GetString();
        op.pTo = NULL;
        op.fFlags = FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_NO_UI|FOF_SILENT;
        op.fAnyOperationsAborted = FALSE;
        op.hNameMappings = 0;
        op.lpszProgressTitle = _T("");
        int ret = SHFileOperation(&op);
        if (ret) {
            printf_s("\n* Deletion error (special) code %d\n", ret);
            ++errs;
            goodbye();
        }
        // a little loop to watch for the free disk to stop changing
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
    if (!SUCCEEDED(CopyFile2(srcFile, destFile, &param))) {
        printf_s("** Failed to copy file -- Code %d\n", GetLastError());
        ++errs;
        return;
    }
    freeUser.QuadPart -= filesize.QuadPart;
}

// if not backing up, then we're deleting orphans
void ConfirmOneFile(CString& path, WIN32_FIND_DATA &findDat);
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
                if (!CreateDirectory(newFolder, NULL)) {
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
            HANDLE hFind2 = FindFirstFile(srchPath, &newFind);
            if (INVALID_HANDLE_VALUE == hFind2) {
                printf_s("Failed to start subdir search. Code %d\n", GetLastError());
                ++errs;
                continue;
            }
            RecursivePath(path, newSubPath, hFind2, newFind, backingup);
            FindClose(hFind2);
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

    HANDLE hFind = FindFirstFile(search, &findDat);
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
    HANDLE hFind = FindFirstFile(search, &findDat);
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
    CString backupFile; backupFile.Format(fmtStr, dest, 0); backupFile+="\\"; backupFile += path; backupFile+=fn;

    if (!PathFileExists(srcFile)) {
        printf_s("NUKE: %S -> %S\n", destFile.GetString(), backupFile.GetString());
        if (!MoveToFolder(destFile, backupFile)) {
            printf_s("** Failed to move file -- not copied! Code %d\n", GetLastError());
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

	if (argc < 3) {
        printf_s("tursicopy <src> <dest>\nBacks up a folder with historical backups.\n");
        return -1;
	}

    src = argv[1];
    dest = argv[2];
    if (src.Right(1) != "\\") src+='\\';
    if (dest.Right(1) != "\\") dest+='\\';

    printf_s("Going to copy from %S to %S\n", src.GetString(), dest.GetString());
    
    // make sure the destination folder exists - need this before we check disk space
    if (!MoveToFolder(_T(""), dest)) {
        printf_s("Failed to create target folder. Can't continue.\n");
        return -1;
    }

    printf_s("Checking destination free disk space... ");
    if (!GetDiskFreeSpaceEx(dest, &freeUser, &totalBytes, &freeBytes)) {
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

    RotateOldBackups();
    DoNewBackup();
    DeleteOrphans();
    goodbye();

    return 0;
}

