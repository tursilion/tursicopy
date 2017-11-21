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
                std::wcout << "Failed to create folder " << tmp.GetString() << ", code " << GetLastError() << std::endl;
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
    std::wcout << std::endl << "-- DONE -- " << errs << " errs." << std::endl;
#ifdef _DEBUG
    if (errs) {
        std::wcout << "Press a key..." << std::endl;
        while (!_kbhit()) {}
    }
#endif
    exit(-1);
}

/////////////////////////////////////////////////////////////////////////
// startup rotation

void RotateOldBackups() {
    std::wcout << "Scanning for old backup folders... " << std::endl;

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

    std::wcout << lastBackup+1 << " folders found." << std::endl;

    // rename them all
    for (int idx = lastBackup; idx>=0; --idx) {
        CString oldFolder, newFolder;
        oldFolder.Format(fmtStr, dest, idx);
        newFolder.Format(fmtStr, dest, idx+1);
#ifdef _DEBUG
        std::wcout << oldFolder.GetString() << " -> " << newFolder.GetString() << std::endl;
#endif
        if (!MoveFileEx(oldFolder, newFolder, MOVEFILE_WRITE_THROUGH)) {
            std::wcout << "- MoveFile failed, code " << GetLastError() << std::endl;
        }
    }
    ++lastBackup;

    // create the new 0
    CString newFolder; 
    newFolder.Format(fmtStr, dest, 0);
    if (!CreateDirectory(newFolder, NULL)) {
        std::wcout << "Failed to create folder 0, code " << GetLastError() << std::endl;
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
            std::wcout << "Failed to open old dest file, though it exists. Code " << GetLastError() << std::endl;
            ++errs;
            return;
        }
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(hFile, &info)) {
            std::wcout << "Failed to get old dest file information, skipping. Code " << GetLastError() << std::endl;
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
            std::wcout << "SAME: " << destFile.GetString() << std::endl;
#endif
            return;
        }

        std::wcout << "BACK: " << destFile.GetString() << " -> " << backupFile.GetString() << std::endl;
        if (!MoveToFolder(destFile, backupFile)) {
            std::wcout << "** Failed to move file -- not copied! Code " << GetLastError() << std::endl;
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
            std::wcout << "** Not enough backup folders left to free space - aborting." << std::endl;
            ++errs;
            goodbye();
        }
        std::wcout << "* Freeing disk space, deleting backup folder " << lastBackup << "... ";

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
            std::wcout << std::endl << "* Deletion error (special) code " << ret << std::endl;
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
                std::wcout << "Failed. Error " << GetLastError() << std::endl;
                ++errs;
                goodbye();
            }
        } while (old.QuadPart != freeUser.QuadPart);
        --lastBackup;
        std::wcout << "Free space now " << freeUser.QuadPart << std::endl;
    }

    // finally do the copy
    std::wcout << "COPY: " << srcFile.GetString() << " -> " << destFile.GetString() << std::endl;
    BOOL cancel = FALSE;
    // CopyFile2 can preserve attributes!
    COPYFILE2_EXTENDED_PARAMETERS param;
    param.dwSize=sizeof(param);
    param.dwCopyFlags = COPY_FILE_COPY_SYMLINK | COPY_FILE_FAIL_IF_EXISTS;
    param.pfCancel = FALSE;
    param.pProgressRoutine = NULL;
    param.pvCallbackContext = NULL;
    if (!SUCCEEDED(CopyFile2(srcFile, destFile, &param))) {
        std::wcout << "** Failed to copy file -- Code " << GetLastError() << std::endl;
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
            // skip backup folders
            wchar_t* p=wcschr(findDat.cFileName, _T('~'));
            if (p != NULL) {
                int d;
                if (1 == swscanf_s(p, fmtStr.Mid(2).GetString(), &d)) {
                    continue;
                }
            }

#ifdef _DEBUG
            std::wcout << path.GetString() << subPath.GetString() << findDat.cFileName << std::endl;
#endif

            // make sure this folder exists on the target
            if (backingup) {
                CString newFolder = dest + subPath + findDat.cFileName;
                if (!CreateDirectory(newFolder, NULL)) {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) {
                        std::wcout << "Warning to create folder " << newFolder.GetString() << ", code " << GetLastError() << std::endl;
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
                std::wcout << "Failed to search subdir search. Code " << GetLastError() << std::endl;
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

    std::wcout << "Searching for new or changed files..." << std::endl;

    HANDLE hFind = FindFirstFile(search, &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        std::wcout << "Failed to open search: code " << GetLastError() << std::endl;
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
    std::wcout << "Remove orphaned files..." << std::endl;

    CString search = dest + "*";
    WIN32_FIND_DATA findDat;
    HANDLE hFind = FindFirstFile(search, &findDat);
    if (INVALID_HANDLE_VALUE == hFind) {
        std::wcout << "Failed to open dest search: code " << GetLastError() << std::endl;
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
        std::wcout << "NUKE: " << destFile.GetString() << " -> " << backupFile.GetString() << std::endl;
        if (!MoveToFolder(destFile, backupFile)) {
            std::wcout << "** Failed to move file -- not copied! Code " << GetLastError() << std::endl;
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
        printf("tursicopy <src> <dest>\nBacks up a folder with historical backups.\n");
        return -1;
	}

    src = argv[1];
    dest = argv[2];
    if (src.Right(1) != "\\") src+='\\';
    if (dest.Right(1) != "\\") dest+='\\';

    std::wcout << "Going to copy from " << src.GetString() << " to " << dest.GetString() << std::endl;
    
    // make sure the destination folder exists - need this before we check disk space
    if (!MoveToFolder(_T(""), dest)) {
        std::wcout << "Failed to create target folder." << std::endl;
        return -1;
    }

    std::wcout << "Checking destination free disk space...";
    if (!GetDiskFreeSpaceEx(dest, &freeUser, &totalBytes, &freeBytes)) {
        std::wcout << "Failed. Error " << GetLastError() << std::endl;
        return -1;
    }

    std::wcout << "Got " << freeUser.QuadPart << " bytes." << std::endl;
    if (freeUser.QuadPart != freeBytes.QuadPart) {
        std::wcout << "* Warning: user may have quotas. Disk free is " << freeBytes.QuadPart << std::endl;
    }

    if (totalBytes.QuadPart == 0) {
        std::wcout << "* Something went wrong - total disk size is zero bytes. Aborting." << std::endl;
        return -1;
    }

    RotateOldBackups();
    DoNewBackup();
    DeleteOrphans();
    goodbye();

    return 0;
}

