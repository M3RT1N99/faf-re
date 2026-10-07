#pragma once

// Win32 file services for the non-Windows targets of the port, over POSIX:
// CreateFile/ReadFile/WriteFile and the file pointer, attributes,
// FindFirstFile enumeration, directories, copy/move/replace/delete, file
// mappings, the current directory, and FormatMessage for the error codes the
// shim sets. Included by faf_win_compat.h (<windows.h>).
//
// Paths go through faf_win_path.h first: '\' and '/' both separate, and
// components that do not exist as spelled are matched ignoring case, as on
// NTFS. The engine needs that: its VFS opens canonical, lower-cased,
// backslash-separated paths (gpg::STR_CanonizeFilename). Wide (W) functions
// take wchar_t text, encode it as UTF-8 and do what the A function does.
//
// Semantics kept from Win32 where POSIX differs:
// - CreateFile fails on a directory (ERROR_ACCESS_DENIED) unless
//   FILE_FLAG_BACKUP_SEMANTICS is given; CREATE_ALWAYS/OPEN_ALWAYS report
//   ERROR_ALREADY_EXISTS when the file was there; FILE_FLAG_DELETE_ON_CLOSE
//   removes the file when its handle is closed (gpg::FileStream maps its 0x20
//   attribute onto it, gpg/core/streams/FileStream.cpp:154); a new file
//   created with FILE_ATTRIBUTE_READONLY is read-only.
// - ReadFile reads until the count or the end of the file (a short count only
//   at the end), WriteFile writes everything or fails.
// - A file pointer may not move before the start (ERROR_NEGATIVE_SEEK).
// - ENOENT tells ERROR_FILE_NOT_FOUND from ERROR_PATH_NOT_FOUND by whether the
//   parent directory exists.
// - DeleteFile refuses directories and read-only files (ERROR_ACCESS_DENIED);
//   MoveFile refuses an existing target (ERROR_ALREADY_EXISTS) and moves files
//   across file systems by copying.
// - Attributes: FILE_ATTRIBUTE_DIRECTORY for directories, FILE_ATTRIBUTE_ARCHIVE
//   for files (what NTFS gives a file nobody changed), FILE_ATTRIBUTE_READONLY
//   when the owner cannot write. Times: POSIX keeps no creation time, so the
//   creation time is the last write time.
// - CreateFileMapping of an empty file fails (ERROR_FILE_INVALID); a mapping
//   keeps its own reference to the file, so the file handle may be closed
//   while views exist (moho/misc/FileWaitHandleSet.cpp:1740 does); views must
//   start at a multiple of the 64 KB allocation granularity.
// Not here: sharing modes (POSIX has no mandatory sharing; accepted and
// ignored), overlapped I/O (the OVERLAPPED argument must be null), security
// attributes, alternate streams, short (8.3) names, named file mappings
// (the name is ignored: nothing shares them across processes here).

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

extern "C++" {

// ---------------------------------------------------------------------------
// Constants (winnt.h, fileapi.h, winbase.h, memoryapi.h)
// ---------------------------------------------------------------------------
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_EXECUTE 0x20000000u
#define GENERIC_ALL 0x10000000u
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004

#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000

#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004
#define MOVEFILE_WRITE_THROUGH 0x00000008

#define FILE_MAP_COPY 0x0001
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define FILE_MAP_EXECUTE 0x0020
#define FILE_MAP_ALL_ACCESS 0x000F001F

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000FF

#define ERROR_MAPPED_ALIGNMENT 1132L
#define ERROR_UNABLE_TO_REMOVE_REPLACED 1175L
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT 1176L

typedef struct _OVERLAPPED {
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    PVOID Pointer;
  };
  HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef enum _GET_FILEEX_INFO_LEVELS {
  GetFileExInfoStandard,
  GetFileExMaxInfoLevel
} GET_FILEEX_INFO_LEVELS;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;

typedef struct _WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef struct _WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[MAX_PATH];
  WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

namespace faf_compat
{
  struct FileObject : KernelObject {
    int fd;
    char* deleteOnClose;  // resolved path, FILE_FLAG_DELETE_ON_CLOSE only
  };

  struct FileMappingObject : KernelObject {
    int fd;  // its own descriptor (dup), so the file handle may close first
    ULONGLONG size;
    bool writable;
  };

  struct FindFileObject : KernelObject {
    DirectoryListing* listing;
  };

  inline void DestroyFileObject(KernelObject* object) noexcept
  {
    FileObject* const file = static_cast<FileObject*>(object);
    close(file->fd);
    if (file->deleteOnClose != nullptr) {
      (void)unlink(file->deleteOnClose);
      free(file->deleteOnClose);
    }
  }

  inline void DestroyFileMappingObject(KernelObject* object) noexcept
  {
    close(static_cast<FileMappingObject*>(object)->fd);
  }

  inline void DestroyFindFileObject(KernelObject* object) noexcept
  {
    CloseDirectoryListing(static_cast<FindFileObject*>(object)->listing);
  }

  // Lock not held; the object stays valid while the handle is open.
  inline KernelObject* KernelObjectOfKind(HANDLE handle, const KernelKind kind) noexcept
  {
    KernelLockGuard guard;
    KernelObject* const object = ObjectFromHandle(handle);
    return (object != nullptr && object->kind == kind) ? object : nullptr;
  }

  inline int FileDescriptor(HANDLE handle) noexcept
  {
    KernelObject* const object = KernelObjectOfKind(handle, KernelKind::File);
    if (object == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return -1;
    }
    return static_cast<FileObject*>(object)->fd;
  }

  inline bool ParentDirectoryExists(const char* path) noexcept
  {
    char parent[kMaxNativePath];
    const size_t length = strlen(path);
    if (length + 1 > sizeof(parent)) {
      return false;
    }
    memcpy(parent, path, length + 1);
    char* const slash = strrchr(parent, '/');
    if (slash == nullptr) {
      return true;  // relative to the current directory
    }
    if (slash == parent) {
      slash[1] = '\0';
    } else {
      *slash = '\0';
    }
    struct stat status;
    return stat(parent, &status) == 0 && S_ISDIR(status.st_mode);
  }

  // The Win32 error for a failed POSIX call on `path` (null: no path involved).
  inline DWORD Win32ErrorFromErrno(const int error, const char* path) noexcept
  {
    switch (error) {
      case 0:
        return ERROR_SUCCESS;
      case ENOENT:
        return (path == nullptr || ParentDirectoryExists(path)) ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
      case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
      case EACCES:
      case EPERM:
      case EISDIR:
        return ERROR_ACCESS_DENIED;
      case EEXIST:
        return ERROR_FILE_EXISTS;
      case EBADF:
        return ERROR_INVALID_HANDLE;
      case EMFILE:
      case ENFILE:
        return ERROR_TOO_MANY_OPEN_FILES;
      case ENOMEM:
        return ERROR_NOT_ENOUGH_MEMORY;
      case ENOSPC:
      case EDQUOT:
        return ERROR_DISK_FULL;
      case EROFS:
        return ERROR_WRITE_PROTECT;
      case ENAMETOOLONG:
        return ERROR_FILENAME_EXCED_RANGE;
      case ENOTEMPTY:
        return ERROR_DIR_NOT_EMPTY;
      case EXDEV:
        return ERROR_NOT_SAME_DEVICE;
      case EBUSY:
        return ERROR_BUSY;
      case EINVAL:
        return ERROR_INVALID_PARAMETER;
      case EPIPE:
        return ERROR_BROKEN_PIPE;
      case ENOTSUP:
        return ERROR_NOT_SUPPORTED;
      default:
        return ERROR_GEN_FAILURE;
    }
  }

  inline void SetLastErrorFromErrno(const int error, const char* path = nullptr) noexcept
  {
    SetLastError(Win32ErrorFromErrno(error, path));
  }

  // A Win32 path (UTF-8) resolved for the file system; false with the error set.
  inline bool NativePathA(LPCSTR path, char* out, const size_t size) noexcept
  {
    if (path == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return false;
    }
    if (path[0] == '\0') {
      SetLastError(ERROR_PATH_NOT_FOUND);
      return false;
    }
    if (!ResolveWin32Path(path, out, size)) {
      SetLastError(ERROR_FILENAME_EXCED_RANGE);
      return false;
    }
    return true;
  }

  // A wide path as UTF-8 (not yet resolved); false with the error set.
  inline bool WidePathToUtf8(LPCWSTR path, char* out, const size_t size) noexcept
  {
    if (path == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return false;
    }
    if (!WideToUtf8(path, out, size)) {
      SetLastError(ERROR_INVALID_NAME);
      return false;
    }
    return true;
  }

  inline FILETIME FileTimeFromTimespec(const timespec& time) noexcept
  {
    if (time.tv_sec < 0) {
      return TicksToFileTime(kFileTimeUnixEpoch);
    }
    return TicksToFileTime(
      kFileTimeUnixEpoch + static_cast<ULONGLONG>(time.tv_sec) * kFileTimeTicksPerSecond +
      static_cast<ULONGLONG>(time.tv_nsec) / 100u
    );
  }

  inline DWORD AttributesFromStat(const struct stat& status) noexcept
  {
    DWORD attributes = S_ISDIR(status.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!S_ISDIR(status.st_mode) && (status.st_mode & S_IWUSR) == 0) {
      attributes |= FILE_ATTRIBUTE_READONLY;
    }
    return attributes;
  }

  inline void FillAttributeData(const struct stat& status, WIN32_FILE_ATTRIBUTE_DATA* data) noexcept
  {
    data->dwFileAttributes = AttributesFromStat(status);
    data->ftLastWriteTime = FileTimeFromTimespec(status.st_mtim);
    data->ftLastAccessTime = FileTimeFromTimespec(status.st_atim);
    data->ftCreationTime = data->ftLastWriteTime;
    const ULONGLONG size = S_ISDIR(status.st_mode) ? 0u : static_cast<ULONGLONG>(status.st_size);
    data->nFileSizeHigh = static_cast<DWORD>(size >> 32);
    data->nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFu);
  }

  // stat of a Win32 path; false with the error set.
  inline bool StatWin32Path(LPCSTR path, struct stat* status, char* native, const size_t nativeSize) noexcept
  {
    if (!NativePathA(path, native, nativeSize)) {
      return false;
    }
    if (stat(native, status) != 0) {
      SetLastErrorFromErrno(errno, native);
      return false;
    }
    return true;
  }

  // The fields of a find-data record both widths share, from one listed name.
  template <class FindData>
  inline void FillFindHeader(const DirectoryListing& listing, const char* name, FindData* data) noexcept
  {
    memset(data, 0, sizeof(*data));
    struct stat status;
    if (StatListedName(listing, name, &status)) {
      WIN32_FILE_ATTRIBUTE_DATA attributes;
      FillAttributeData(status, &attributes);
      data->dwFileAttributes = attributes.dwFileAttributes;
      data->ftCreationTime = attributes.ftCreationTime;
      data->ftLastAccessTime = attributes.ftLastAccessTime;
      data->ftLastWriteTime = attributes.ftLastWriteTime;
      data->nFileSizeHigh = attributes.nFileSizeHigh;
      data->nFileSizeLow = attributes.nFileSizeLow;
    } else {
      data->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;  // a dangling link
    }
  }

  inline void FillFindData(const DirectoryListing& listing, const char* name, WIN32_FIND_DATAA* data) noexcept
  {
    FillFindHeader(listing, name, data);
    const size_t length = strnlen(name, MAX_PATH - 1);
    memcpy(data->cFileName, name, length);
    data->cFileName[length] = '\0';
  }

  inline void FillFindData(const DirectoryListing& listing, const char* name, WIN32_FIND_DATAW* data) noexcept
  {
    FillFindHeader(listing, name, data);
    (void)Utf8ToWide(name, data->cFileName, MAX_PATH);
  }

  template <class FindData>
  inline HANDLE FindFirstFileImpl(LPCSTR fileName, FindData* data) noexcept
  {
    if (fileName == nullptr || data == nullptr) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return INVALID_HANDLE_VALUE;
    }
    ListingStatus status = ListingStatus::Ok;
    DirectoryListing* const listing = OpenDirectoryListing(fileName, &status);
    if (listing == nullptr) {
      switch (status) {
        case ListingStatus::NoDirectory:
          SetLastError(ERROR_PATH_NOT_FOUND);
          break;
        case ListingStatus::OutOfMemory:
          SetLastError(ERROR_NOT_ENOUGH_MEMORY);
          break;
        case ListingStatus::NameTooLong:
          SetLastError(ERROR_FILENAME_EXCED_RANGE);
          break;
        default:
          SetLastError(ERROR_FILE_NOT_FOUND);
          break;
      }
      return INVALID_HANDLE_VALUE;
    }
    FindFileObject* const find = AllocateKernelObject<FindFileObject>(KernelKind::FindFile);
    if (find == nullptr) {
      CloseDirectoryListing(listing);
      return INVALID_HANDLE_VALUE;
    }
    find->listing = listing;
    find->destroy = &DestroyFindFileObject;
    FillFindData(*listing, listing->names[listing->next++], data);
    SetLastError(ERROR_SUCCESS);
    return find;
  }

  template <class FindData>
  inline BOOL FindNextFileImpl(HANDLE handle, FindData* data) noexcept
  {
    KernelObject* const object = KernelObjectOfKind(handle, KernelKind::FindFile);
    if (object == nullptr || data == nullptr) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    DirectoryListing& listing = *static_cast<FindFileObject*>(object)->listing;
    if (listing.next >= listing.count) {
      SetLastError(ERROR_NO_MORE_FILES);
      return FALSE;
    }
    FillFindData(listing, listing.names[listing.next++], data);
    return TRUE;
  }

  // Copies src to dst (both resolved) with src's mode and times; dst must not
  // exist when `failIfExists`.
  inline bool CopyFileContents(const char* source, const char* destination, const bool failIfExists) noexcept
  {
    const int in = open(source, O_RDONLY | O_CLOEXEC);
    if (in < 0) {
      SetLastErrorFromErrno(errno, source);
      return false;
    }
    struct stat status;
    if (fstat(in, &status) != 0 || S_ISDIR(status.st_mode)) {
      close(in);
      SetLastError(ERROR_ACCESS_DENIED);
      return false;
    }
    const int out = open(
      destination, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | (failIfExists ? O_EXCL : 0), status.st_mode & 0777
    );
    if (out < 0) {
      const int error = errno;
      close(in);
      SetLastErrorFromErrno(error, destination);
      return false;
    }
    char buffer[65536];
    bool ok = true;
    for (;;) {
      const ssize_t got = read(in, buffer, sizeof(buffer));
      if (got < 0 && errno == EINTR) {
        continue;
      }
      if (got <= 0) {
        ok = got == 0;
        break;
      }
      for (ssize_t done = 0; done < got && ok;) {
        const ssize_t put = write(out, buffer + done, static_cast<size_t>(got - done));
        if (put < 0 && errno == EINTR) {
          continue;
        }
        ok = put > 0;
        done += put > 0 ? put : 0;
      }
      if (!ok) {
        break;
      }
    }
    const int error = errno;
    if (ok) {
      const timespec times[2] = {status.st_atim, status.st_mtim};
      (void)futimens(out, times);
    }
    close(in);
    if (close(out) != 0 && ok) {
      ok = false;
    }
    if (!ok) {
      SetLastErrorFromErrno(error != 0 ? error : EIO, destination);
    }
    return ok;
  }

  inline bool MoveFileImpl(const char* source, const char* destination, const DWORD flags) noexcept
  {
    struct stat status;
    if (lstat(source, &status) != 0) {
      SetLastErrorFromErrno(errno, source);
      return false;
    }
    struct stat target;
    if ((flags & MOVEFILE_REPLACE_EXISTING) == 0 && lstat(destination, &target) == 0) {
      SetLastError(ERROR_ALREADY_EXISTS);
      return false;
    }
    if (rename(source, destination) == 0) {
      return true;
    }
    if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED) != 0 && !S_ISDIR(status.st_mode)) {
      if (!CopyFileContents(source, destination, (flags & MOVEFILE_REPLACE_EXISTING) == 0)) {
        return false;
      }
      (void)unlink(source);
      return true;
    }
    SetLastErrorFromErrno(errno, destination);
    return false;
  }

  struct SystemMessage {
    DWORD code;
    const char* text;
  };

  // English system messages, as FormatMessage returns them (with the CRLF),
  // for the codes this shim sets.
  inline const char* SystemMessageText(const DWORD code) noexcept
  {
    static const SystemMessage kMessages[] = {
      {ERROR_SUCCESS, "The operation completed successfully.\r\n"},
      {ERROR_INVALID_FUNCTION, "Incorrect function.\r\n"},
      {ERROR_FILE_NOT_FOUND, "The system cannot find the file specified.\r\n"},
      {ERROR_PATH_NOT_FOUND, "The system cannot find the path specified.\r\n"},
      {ERROR_TOO_MANY_OPEN_FILES, "The system cannot open the file.\r\n"},
      {ERROR_ACCESS_DENIED, "Access is denied.\r\n"},
      {ERROR_INVALID_HANDLE, "The handle is invalid.\r\n"},
      {ERROR_NOT_ENOUGH_MEMORY, "Not enough memory resources are available to process this command.\r\n"},
      {ERROR_OUTOFMEMORY, "Not enough memory resources are available to complete this operation.\r\n"},
      {ERROR_NOT_SAME_DEVICE, "The system cannot move the file to a different disk drive.\r\n"},
      {ERROR_NO_MORE_FILES, "There are no more files.\r\n"},
      {ERROR_WRITE_PROTECT, "The media is write protected.\r\n"},
      {ERROR_BAD_LENGTH, "The program issued a command but the command length is incorrect.\r\n"},
      {ERROR_GEN_FAILURE, "A device attached to the system is not functioning.\r\n"},
      {ERROR_SHARING_VIOLATION,
       "The process cannot access the file because it is being used by another process.\r\n"},
      {ERROR_HANDLE_EOF, "Reached the end of the file.\r\n"},
      {ERROR_NOT_SUPPORTED, "The request is not supported.\r\n"},
      {ERROR_FILE_EXISTS, "The file exists.\r\n"},
      {ERROR_INVALID_PARAMETER, "The parameter is incorrect.\r\n"},
      {ERROR_BROKEN_PIPE, "The pipe has been ended.\r\n"},
      {ERROR_DISK_FULL, "There is not enough space on the disk.\r\n"},
      {ERROR_INSUFFICIENT_BUFFER, "The data area passed to a system call is too small.\r\n"},
      {ERROR_INVALID_NAME, "The filename, directory name, or volume label syntax is incorrect.\r\n"},
      {ERROR_MOD_NOT_FOUND, "The specified module could not be found.\r\n"},
      {ERROR_PROC_NOT_FOUND, "The specified procedure could not be found.\r\n"},
      {ERROR_NEGATIVE_SEEK,
       "An attempt was made to move the file pointer before the beginning of the file.\r\n"},
      {ERROR_DIR_NOT_EMPTY, "The directory is not empty.\r\n"},
      {ERROR_BUSY, "The requested resource is in use.\r\n"},
      {ERROR_ALREADY_EXISTS, "Cannot create a file when that file already exists.\r\n"},
      {ERROR_FILENAME_EXCED_RANGE, "The filename or extension is too long.\r\n"},
      {ERROR_DIRECTORY, "The directory name is invalid.\r\n"},
      {ERROR_NOT_OWNER, "Attempt to release mutex not owned by caller.\r\n"},
      {ERROR_TOO_MANY_POSTS, "Too many posts were made to a semaphore.\r\n"},
      {ERROR_INVALID_ADDRESS, "Attempt to access invalid address.\r\n"},
      {ERROR_FILE_INVALID,
       "The volume for a file has been externally altered so that the opened file is no longer valid.\r\n"},
      {ERROR_MAPPED_ALIGNMENT, "The base address or the file offset specified does not have the proper alignment.\r\n"},
      {ERROR_TIMEOUT, "This operation returned because the timeout period expired.\r\n"},
    };
    for (const SystemMessage& message : kMessages) {
      if (message.code == code) {
        return message.text;
      }
    }
    return nullptr;
  }
} // namespace faf_compat

extern "C" {

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
inline HANDLE CreateFileA(
  LPCSTR fileName, DWORD desiredAccess, DWORD, LPSECURITY_ATTRIBUTES, DWORD creationDisposition,
  DWORD flagsAndAttributes, HANDLE
) noexcept
{
  char native[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(fileName, native, sizeof(native))) {
    return INVALID_HANDLE_VALUE;
  }
  const bool read = (desiredAccess & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0;
  const bool write = (desiredAccess & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
  int flags = O_CLOEXEC | ((read && write) ? O_RDWR : (write ? O_WRONLY : O_RDONLY));
  switch (creationDisposition) {
    case CREATE_NEW:
      flags |= O_CREAT | O_EXCL;
      break;
    case CREATE_ALWAYS:
      flags |= O_CREAT | O_TRUNC;
      break;
    case OPEN_EXISTING:
      break;
    case OPEN_ALWAYS:
      flags |= O_CREAT;
      break;
    case TRUNCATE_EXISTING:
      if (!write) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
      }
      flags |= O_TRUNC;
      break;
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return INVALID_HANDLE_VALUE;
  }
  struct stat status;
  const bool existed = (creationDisposition == CREATE_ALWAYS || creationDisposition == OPEN_ALWAYS) &&
    lstat(native, &status) == 0;
  const mode_t mode = (flagsAndAttributes & FILE_ATTRIBUTE_READONLY) != 0 ? 0444 : 0666;
  int fd = -1;
  do {
    fd = open(native, flags, mode);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    faf_compat::SetLastErrorFromErrno(errno, native);
    return INVALID_HANDLE_VALUE;
  }
  if (fstat(fd, &status) == 0 && S_ISDIR(status.st_mode) && (flagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) == 0) {
    close(fd);
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_HANDLE_VALUE;
  }
  faf_compat::FileObject* const file =
    faf_compat::AllocateKernelObject<faf_compat::FileObject>(faf_compat::KernelKind::File);
  if (file == nullptr) {
    close(fd);
    return INVALID_HANDLE_VALUE;
  }
  file->fd = fd;
  file->destroy = &faf_compat::DestroyFileObject;
  if ((flagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0) {
    file->deleteOnClose = strdup(native);
  }
  SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
  return file;
}

inline HANDLE CreateFileW(
  LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode, LPSECURITY_ATTRIBUTES securityAttributes,
  DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile
) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return INVALID_HANDLE_VALUE;
  }
  return CreateFileA(
    narrow, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile
  );
}

inline BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped) noexcept
{
  if (bytesRead != nullptr) {
    *bytesRead = 0;
  }
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return FALSE;
  }
  if (overlapped != nullptr) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  DWORD total = 0;
  while (total < bytesToRead) {
    const ssize_t got = read(fd, static_cast<char*>(buffer) + total, bytesToRead - total);
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got < 0) {
      faf_compat::SetLastErrorFromErrno(errno);
      if (bytesRead != nullptr) {
        *bytesRead = total;
      }
      return FALSE;
    }
    if (got == 0) {
      break;
    }
    total += static_cast<DWORD>(got);
  }
  if (bytesRead != nullptr) {
    *bytesRead = total;
  }
  return TRUE;
}

inline BOOL WriteFile(
  HANDLE file, LPCVOID buffer, DWORD bytesToWrite, LPDWORD bytesWritten, LPOVERLAPPED overlapped
) noexcept
{
  if (bytesWritten != nullptr) {
    *bytesWritten = 0;
  }
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return FALSE;
  }
  if (overlapped != nullptr) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  DWORD total = 0;
  while (total < bytesToWrite) {
    const ssize_t put = write(fd, static_cast<const char*>(buffer) + total, bytesToWrite - total);
    if (put < 0 && errno == EINTR) {
      continue;
    }
    if (put <= 0) {
      faf_compat::SetLastErrorFromErrno(put < 0 ? errno : ENOSPC);
      if (bytesWritten != nullptr) {
        *bytesWritten = total;
      }
      return FALSE;
    }
    total += static_cast<DWORD>(put);
  }
  if (bytesWritten != nullptr) {
    *bytesWritten = total;
  }
  return TRUE;
}

inline BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPosition, DWORD moveMethod) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return FALSE;
  }
  int whence = SEEK_SET;
  switch (moveMethod) {
    case FILE_BEGIN:
      whence = SEEK_SET;
      break;
    case FILE_CURRENT:
      whence = SEEK_CUR;
      break;
    case FILE_END:
      whence = SEEK_END;
      break;
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
  }
  const off64_t position = lseek64(fd, static_cast<off64_t>(distance.QuadPart), whence);
  if (position < 0) {
    SetLastError(errno == EINVAL ? ERROR_NEGATIVE_SEEK : faf_compat::Win32ErrorFromErrno(errno, nullptr));
    return FALSE;
  }
  if (newPosition != nullptr) {
    newPosition->QuadPart = static_cast<LONGLONG>(position);
  }
  return TRUE;
}

// The low 32 bits of the new position; with `distanceHigh` a 64-bit move whose
// high half comes back there. INVALID_SET_FILE_POINTER is ambiguous, so the
// last error is cleared on success, as Windows does.
inline DWORD SetFilePointer(HANDLE file, LONG distanceLow, PLONG distanceHigh, DWORD moveMethod) noexcept
{
  LARGE_INTEGER distance;
  distance.QuadPart = distanceHigh != nullptr
    ? static_cast<LONGLONG>((static_cast<ULONGLONG>(static_cast<DWORD>(*distanceHigh)) << 32) |
                            static_cast<DWORD>(distanceLow))
    : static_cast<LONGLONG>(distanceLow);
  LARGE_INTEGER position;
  if (!SetFilePointerEx(file, distance, &position, moveMethod)) {
    return INVALID_SET_FILE_POINTER;
  }
  if (distanceHigh != nullptr) {
    *distanceHigh = static_cast<LONG>(position.QuadPart >> 32);
  }
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(position.QuadPart & 0xFFFFFFFF);
}

inline BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  struct stat status;
  if (fd < 0) {
    return FALSE;
  }
  if (fstat(fd, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return FALSE;
  }
  size->QuadPart = static_cast<LONGLONG>(status.st_size);
  return TRUE;
}

// INVALID_FILE_SIZE with an error on failure; the last error is cleared on
// success, since INVALID_FILE_SIZE is also a valid low half.
inline DWORD GetFileSize(HANDLE file, LPDWORD sizeHigh) noexcept
{
  LARGE_INTEGER size;
  if (!GetFileSizeEx(file, &size)) {
    return INVALID_FILE_SIZE;
  }
  if (sizeHigh != nullptr) {
    *sizeHigh = static_cast<DWORD>(static_cast<ULONGLONG>(size.QuadPart) >> 32);
  }
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(size.QuadPart & 0xFFFFFFFF);
}

inline BOOL FlushFileBuffers(HANDLE file) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return FALSE;
  }
  if (fsync(fd) != 0 && errno != EINVAL) {
    faf_compat::SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

inline BOOL SetEndOfFile(HANDLE file) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return FALSE;
  }
  const off64_t position = lseek64(fd, 0, SEEK_CUR);
  if (position < 0 || ftruncate64(fd, position) != 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

inline BOOL GetFileTime(HANDLE file, LPFILETIME creationTime, LPFILETIME lastAccessTime, LPFILETIME lastWriteTime) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  struct stat status;
  if (fd < 0) {
    return FALSE;
  }
  if (fstat(fd, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return FALSE;
  }
  WIN32_FILE_ATTRIBUTE_DATA data;
  faf_compat::FillAttributeData(status, &data);
  if (creationTime != nullptr) {
    *creationTime = data.ftCreationTime;
  }
  if (lastAccessTime != nullptr) {
    *lastAccessTime = data.ftLastAccessTime;
  }
  if (lastWriteTime != nullptr) {
    *lastWriteTime = data.ftLastWriteTime;
  }
  return TRUE;
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
inline DWORD GetFileAttributesA(LPCSTR fileName) noexcept
{
  char native[faf_compat::kMaxNativePath];
  struct stat status;
  if (!faf_compat::StatWin32Path(fileName, &status, native, sizeof(native))) {
    return INVALID_FILE_ATTRIBUTES;
  }
  return faf_compat::AttributesFromStat(status);
}

inline DWORD GetFileAttributesW(LPCWSTR fileName) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return INVALID_FILE_ATTRIBUTES;
  }
  return GetFileAttributesA(narrow);
}

inline BOOL GetFileAttributesExA(LPCSTR fileName, GET_FILEEX_INFO_LEVELS infoLevel, LPVOID information) noexcept
{
  if (infoLevel != GetFileExInfoStandard || information == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  char native[faf_compat::kMaxNativePath];
  struct stat status;
  if (!faf_compat::StatWin32Path(fileName, &status, native, sizeof(native))) {
    return FALSE;
  }
  faf_compat::FillAttributeData(status, static_cast<LPWIN32_FILE_ATTRIBUTE_DATA>(information));
  return TRUE;
}

inline BOOL GetFileAttributesExW(LPCWSTR fileName, GET_FILEEX_INFO_LEVELS infoLevel, LPVOID information) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return GetFileAttributesExA(narrow, infoLevel, information);
}

// Only FILE_ATTRIBUTE_READONLY has a POSIX counterpart (the write bits).
inline BOOL SetFileAttributesA(LPCSTR fileName, DWORD attributes) noexcept
{
  char native[faf_compat::kMaxNativePath];
  struct stat status;
  if (!faf_compat::StatWin32Path(fileName, &status, native, sizeof(native))) {
    return FALSE;
  }
  if (S_ISDIR(status.st_mode)) {
    return TRUE;
  }
  const mode_t mode = (attributes & FILE_ATTRIBUTE_READONLY) != 0 ? (status.st_mode & ~static_cast<mode_t>(0222))
                                                                  : (status.st_mode | S_IWUSR);
  if (chmod(native, mode & 07777) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, native);
    return FALSE;
  }
  return TRUE;
}

inline BOOL SetFileAttributesW(LPCWSTR fileName, DWORD attributes) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return SetFileAttributesA(narrow, attributes);
}

// ---------------------------------------------------------------------------
// Directory enumeration: NTFS order, "." and ".." first (faf_win_path.h).
// Find handles are closed with FindClose, not CloseHandle.
// ---------------------------------------------------------------------------
inline HANDLE FindFirstFileA(LPCSTR fileName, LPWIN32_FIND_DATAA findData) noexcept
{
  return faf_compat::FindFirstFileImpl(fileName, findData);
}

inline HANDLE FindFirstFileW(LPCWSTR fileName, LPWIN32_FIND_DATAW findData) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return INVALID_HANDLE_VALUE;
  }
  return faf_compat::FindFirstFileImpl(narrow, findData);
}

inline BOOL FindNextFileA(HANDLE findFile, LPWIN32_FIND_DATAA findData) noexcept
{
  return faf_compat::FindNextFileImpl(findFile, findData);
}

inline BOOL FindNextFileW(HANDLE findFile, LPWIN32_FIND_DATAW findData) noexcept
{
  return faf_compat::FindNextFileImpl(findFile, findData);
}

inline BOOL FindClose(HANDLE findFile) noexcept
{
  faf_compat::KernelObject* object = nullptr;
  {
    faf_compat::KernelLockGuard guard;
    object = faf_compat::ObjectFromHandle(findFile);
    if (object == nullptr || object->kind != faf_compat::KernelKind::FindFile) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    if (!faf_compat::ReleaseKernelReference(object)) {
      return TRUE;
    }
  }
  faf_compat::DestroyKernelObject(object);
  return TRUE;
}

// ---------------------------------------------------------------------------
// Directories and whole files
// ---------------------------------------------------------------------------
inline BOOL CreateDirectoryA(LPCSTR pathName, LPSECURITY_ATTRIBUTES) noexcept
{
  char native[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(pathName, native, sizeof(native))) {
    return FALSE;
  }
  if (mkdir(native, 0777) != 0) {
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS
                                 : (errno == ENOENT ? ERROR_PATH_NOT_FOUND : faf_compat::Win32ErrorFromErrno(errno, native)));
    return FALSE;
  }
  return TRUE;
}

inline BOOL CreateDirectoryW(LPCWSTR pathName, LPSECURITY_ATTRIBUTES attributes) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(pathName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return CreateDirectoryA(narrow, attributes);
}

inline BOOL RemoveDirectoryA(LPCSTR pathName) noexcept
{
  char native[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(pathName, native, sizeof(native))) {
    return FALSE;
  }
  if (rmdir(native) != 0) {
    SetLastError(errno == ENOTDIR ? ERROR_DIRECTORY : faf_compat::Win32ErrorFromErrno(errno, native));
    return FALSE;
  }
  return TRUE;
}

inline BOOL RemoveDirectoryW(LPCWSTR pathName) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(pathName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return RemoveDirectoryA(narrow);
}

inline BOOL DeleteFileA(LPCSTR fileName) noexcept
{
  char native[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(fileName, native, sizeof(native))) {
    return FALSE;
  }
  struct stat status;
  if (lstat(native, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, native);
    return FALSE;
  }
  if (S_ISDIR(status.st_mode) || (!S_ISLNK(status.st_mode) && (status.st_mode & S_IWUSR) == 0)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  if (unlink(native) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, native);
    return FALSE;
  }
  return TRUE;
}

inline BOOL DeleteFileW(LPCWSTR fileName) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(fileName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return DeleteFileA(narrow);
}

inline BOOL CopyFileA(LPCSTR existingFileName, LPCSTR newFileName, BOOL failIfExists) noexcept
{
  char source[faf_compat::kMaxNativePath];
  char destination[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(existingFileName, source, sizeof(source)) ||
      !faf_compat::NativePathA(newFileName, destination, sizeof(destination))) {
    return FALSE;
  }
  return faf_compat::CopyFileContents(source, destination, failIfExists != FALSE) ? TRUE : FALSE;
}

inline BOOL CopyFileW(LPCWSTR existingFileName, LPCWSTR newFileName, BOOL failIfExists) noexcept
{
  char source[faf_compat::kMaxNativePath];
  char destination[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(existingFileName, source, sizeof(source)) ||
      !faf_compat::WidePathToUtf8(newFileName, destination, sizeof(destination))) {
    return FALSE;
  }
  return CopyFileA(source, destination, failIfExists);
}

inline BOOL MoveFileExA(LPCSTR existingFileName, LPCSTR newFileName, DWORD flags) noexcept
{
  char source[faf_compat::kMaxNativePath];
  char destination[faf_compat::kMaxNativePath];
  if (newFileName == nullptr || (flags & MOVEFILE_DELAY_UNTIL_REBOOT) != 0) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  if (!faf_compat::NativePathA(existingFileName, source, sizeof(source)) ||
      !faf_compat::NativePathA(newFileName, destination, sizeof(destination))) {
    return FALSE;
  }
  return faf_compat::MoveFileImpl(source, destination, flags) ? TRUE : FALSE;
}

inline BOOL MoveFileExW(LPCWSTR existingFileName, LPCWSTR newFileName, DWORD flags) noexcept
{
  char source[faf_compat::kMaxNativePath];
  char destination[faf_compat::kMaxNativePath];
  if (newFileName == nullptr) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
  }
  if (!faf_compat::WidePathToUtf8(existingFileName, source, sizeof(source)) ||
      !faf_compat::WidePathToUtf8(newFileName, destination, sizeof(destination))) {
    return FALSE;
  }
  return MoveFileExA(source, destination, flags);
}

// MoveFile: no replace, copying allowed across file systems (files only).
inline BOOL MoveFileA(LPCSTR existingFileName, LPCSTR newFileName) noexcept
{
  return MoveFileExA(existingFileName, newFileName, MOVEFILE_COPY_ALLOWED);
}

inline BOOL MoveFileW(LPCWSTR existingFileName, LPCWSTR newFileName) noexcept
{
  return MoveFileExW(existingFileName, newFileName, MOVEFILE_COPY_ALLOWED);
}

// Replaces `replaced` (which must exist) with `replacement`, keeping the old
// file as `backup` when one is named. Flags are accepted and ignored.
inline BOOL ReplaceFileA(
  LPCSTR replacedFileName, LPCSTR replacementFileName, LPCSTR backupFileName, DWORD, LPVOID, LPVOID
) noexcept
{
  char replaced[faf_compat::kMaxNativePath];
  char replacement[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(replacedFileName, replaced, sizeof(replaced)) ||
      !faf_compat::NativePathA(replacementFileName, replacement, sizeof(replacement))) {
    return FALSE;
  }
  struct stat status;
  if (lstat(replaced, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, replaced);
    return FALSE;
  }
  if (lstat(replacement, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, replacement);
    return FALSE;
  }
  if (backupFileName != nullptr) {
    char backup[faf_compat::kMaxNativePath];
    if (!faf_compat::NativePathA(backupFileName, backup, sizeof(backup))) {
      return FALSE;
    }
    if (rename(replaced, backup) != 0) {
      SetLastError(ERROR_UNABLE_TO_REMOVE_REPLACED);
      return FALSE;
    }
  }
  if (rename(replacement, replaced) != 0) {
    SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT);
    return FALSE;
  }
  return TRUE;
}

inline BOOL ReplaceFileW(
  LPCWSTR replacedFileName, LPCWSTR replacementFileName, LPCWSTR backupFileName, DWORD flags, LPVOID exclude,
  LPVOID reserved
) noexcept
{
  char replaced[faf_compat::kMaxNativePath];
  char replacement[faf_compat::kMaxNativePath];
  char backup[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(replacedFileName, replaced, sizeof(replaced)) ||
      !faf_compat::WidePathToUtf8(replacementFileName, replacement, sizeof(replacement)) ||
      (backupFileName != nullptr && !faf_compat::WidePathToUtf8(backupFileName, backup, sizeof(backup)))) {
    return FALSE;
  }
  return ReplaceFileA(replaced, replacement, backupFileName != nullptr ? backup : nullptr, flags, exclude, reserved);
}

// Returns the length without the terminator, or the size needed with it when
// the buffer is too small.
inline DWORD GetCurrentDirectoryA(DWORD bufferLength, LPSTR buffer) noexcept
{
  char path[faf_compat::kMaxNativePath];
  if (getcwd(path, sizeof(path)) == nullptr) {
    faf_compat::SetLastErrorFromErrno(errno);
    return 0;
  }
  const size_t length = strlen(path);
  if (buffer == nullptr || length + 1 > bufferLength) {
    return static_cast<DWORD>(length + 1);
  }
  memcpy(buffer, path, length + 1);
  return static_cast<DWORD>(length);
}

inline DWORD GetCurrentDirectoryW(DWORD bufferLength, LPWSTR buffer) noexcept
{
  char path[faf_compat::kMaxNativePath];
  if (GetCurrentDirectoryA(sizeof(path), path) == 0) {
    return 0;
  }
  wchar_t wide[faf_compat::kMaxNativePath];
  const size_t length = faf_compat::Utf8ToWide(path, wide, faf_compat::kMaxNativePath);
  if (buffer == nullptr || length + 1 > bufferLength) {
    return static_cast<DWORD>(length + 1);
  }
  memcpy(buffer, wide, (length + 1) * sizeof(wchar_t));
  return static_cast<DWORD>(length);
}

inline BOOL SetCurrentDirectoryA(LPCSTR pathName) noexcept
{
  char native[faf_compat::kMaxNativePath];
  if (!faf_compat::NativePathA(pathName, native, sizeof(native))) {
    return FALSE;
  }
  if (chdir(native) != 0) {
    faf_compat::SetLastErrorFromErrno(errno, native);
    return FALSE;
  }
  return TRUE;
}

inline BOOL SetCurrentDirectoryW(LPCWSTR pathName) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WidePathToUtf8(pathName, narrow, sizeof(narrow))) {
    return FALSE;
  }
  return SetCurrentDirectoryA(narrow);
}

// ---------------------------------------------------------------------------
// File mappings
// ---------------------------------------------------------------------------
inline HANDLE CreateFileMappingA(
  HANDLE file, LPSECURITY_ATTRIBUTES, DWORD protect, DWORD maximumSizeHigh, DWORD maximumSizeLow, LPCSTR
) noexcept
{
  const int fd = faf_compat::FileDescriptor(file);
  if (fd < 0) {
    return nullptr;  // INVALID_HANDLE_VALUE (paging-file sections) is not supported either
  }
  const DWORD access = protect & 0xFFu;
  if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
      access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  struct stat status;
  if (fstat(fd, &status) != 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return nullptr;
  }
  const bool writable = access == PAGE_READWRITE || access == PAGE_EXECUTE_READWRITE;
  ULONGLONG size = (static_cast<ULONGLONG>(maximumSizeHigh) << 32) | maximumSizeLow;
  if (size == 0) {
    size = static_cast<ULONGLONG>(status.st_size);
    if (size == 0) {
      SetLastError(ERROR_FILE_INVALID);
      return nullptr;
    }
  } else if (size > static_cast<ULONGLONG>(status.st_size)) {
    // A writable mapping grows the file, as on Windows; a read-only one cannot.
    if (!writable || ftruncate64(fd, static_cast<off64_t>(size)) != 0) {
      SetLastError(writable ? ERROR_DISK_FULL : ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
  }
  const int mappingFd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (mappingFd < 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return nullptr;
  }
  faf_compat::FileMappingObject* const mapping =
    faf_compat::AllocateKernelObject<faf_compat::FileMappingObject>(faf_compat::KernelKind::FileMapping);
  if (mapping == nullptr) {
    close(mappingFd);
    return nullptr;
  }
  mapping->fd = mappingFd;
  mapping->size = size;
  mapping->writable = writable;
  mapping->destroy = &faf_compat::DestroyFileMappingObject;
  SetLastError(ERROR_SUCCESS);
  return mapping;
}

inline HANDLE CreateFileMappingW(
  HANDLE file, LPSECURITY_ATTRIBUTES attributes, DWORD protect, DWORD maximumSizeHigh, DWORD maximumSizeLow, LPCWSTR
) noexcept
{
  return CreateFileMappingA(file, attributes, protect, maximumSizeHigh, maximumSizeLow, nullptr);
}

inline LPVOID MapViewOfFile(
  HANDLE fileMapping, DWORD desiredAccess, DWORD fileOffsetHigh, DWORD fileOffsetLow, SIZE_T numberOfBytesToMap
) noexcept
{
  faf_compat::KernelObject* const object = faf_compat::KernelObjectOfKind(fileMapping, faf_compat::KernelKind::FileMapping);
  if (object == nullptr) {
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
  }
  const faf_compat::FileMappingObject* const mapping = static_cast<faf_compat::FileMappingObject*>(object);
  const ULONGLONG offset = (static_cast<ULONGLONG>(fileOffsetHigh) << 32) | fileOffsetLow;
  if ((offset % faf_compat::kAllocationGranularity) != 0) {
    SetLastError(ERROR_MAPPED_ALIGNMENT);
    return nullptr;
  }
  if (offset >= mapping->size || (numberOfBytesToMap != 0 && numberOfBytesToMap > mapping->size - offset)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return nullptr;
  }
  const size_t length = numberOfBytesToMap != 0 ? numberOfBytesToMap : static_cast<size_t>(mapping->size - offset);
  int protection = PROT_READ;
  int flags = MAP_SHARED;
  DWORD protect = PAGE_READONLY;
  if (desiredAccess == FILE_MAP_ALL_ACCESS || (desiredAccess & FILE_MAP_WRITE) != 0) {
    if (!mapping->writable) {
      SetLastError(ERROR_ACCESS_DENIED);
      return nullptr;
    }
    protection |= PROT_WRITE;
    protect = PAGE_READWRITE;
  } else if ((desiredAccess & FILE_MAP_COPY) != 0) {
    protection |= PROT_WRITE;
    flags = MAP_PRIVATE;
    protect = PAGE_WRITECOPY;
  }
  void* const view = faf_compat::MapFileView(length, protection, flags, mapping->fd, static_cast<off_t>(offset));
  if (view == nullptr) {
    faf_compat::SetLastErrorFromErrno(errno);
    return nullptr;
  }
  if (!faf_compat::AddRegion(reinterpret_cast<uintptr_t>(view), length, protect, faf_compat::RegionKind::FileView)) {
    munmap(view, length);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  return view;
}

inline BOOL UnmapViewOfFile(LPCVOID baseAddress) noexcept
{
  faf_compat::MemoryRegion region{};
  if (!faf_compat::RemoveRegion(reinterpret_cast<uintptr_t>(baseAddress), faf_compat::RegionKind::FileView, &region)) {
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  munmap(reinterpret_cast<void*>(region.base), region.length);
  return TRUE;
}

inline BOOL FlushViewOfFile(LPCVOID baseAddress, SIZE_T numberOfBytesToFlush) noexcept
{
  faf_compat::MemoryRegion region{};
  const uintptr_t address = reinterpret_cast<uintptr_t>(baseAddress);
  if (!faf_compat::FindRegion(address, &region) || region.kind != faf_compat::RegionKind::FileView) {
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  const uintptr_t start = address & ~(faf_compat::PageSize() - 1u);
  const uintptr_t end = numberOfBytesToFlush != 0 ? address + numberOfBytesToFlush : region.base + region.length;
  if (msync(reinterpret_cast<void*>(start), end - start, MS_SYNC) != 0) {
    faf_compat::SetLastErrorFromErrno(errno);
    return FALSE;
  }
  return TRUE;
}

// ---------------------------------------------------------------------------
// FormatMessage: FORMAT_MESSAGE_FROM_SYSTEM for the codes the shim sets (see
// SystemMessageText), with or without FORMAT_MESSAGE_ALLOCATE_BUFFER (the
// buffer is released with LocalFree). Inserts, other sources and languages
// are not supported (0, ERROR_MR_MID_NOT_FOUND / ERROR_NOT_SUPPORTED).
// ---------------------------------------------------------------------------
inline DWORD FormatMessageA(DWORD flags, LPCVOID, DWORD messageId, DWORD, LPSTR buffer, DWORD size, va_list*) noexcept
{
  if ((flags & FORMAT_MESSAGE_FROM_SYSTEM) == 0 || buffer == nullptr) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
  }
  const char* const text = faf_compat::SystemMessageText(messageId);
  if (text == nullptr) {
    SetLastError(ERROR_MR_MID_NOT_FOUND);
    return 0;
  }
  const size_t length = strlen(text);
  char* target = buffer;
  if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0) {
    target = static_cast<char*>(LocalAlloc(LMEM_FIXED, length + 1));
    if (target == nullptr) {
      return 0;
    }
    *reinterpret_cast<LPSTR*>(buffer) = target;
  } else if (length + 1 > size) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(target, text, length + 1);
  return static_cast<DWORD>(length);
}

inline DWORD FormatMessageW(DWORD flags, LPCVOID, DWORD messageId, DWORD, LPWSTR buffer, DWORD size, va_list*) noexcept
{
  if ((flags & FORMAT_MESSAGE_FROM_SYSTEM) == 0 || buffer == nullptr) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
  }
  const char* const text = faf_compat::SystemMessageText(messageId);
  if (text == nullptr) {
    SetLastError(ERROR_MR_MID_NOT_FOUND);
    return 0;
  }
  const size_t length = strlen(text);  // ASCII: one wchar_t per byte
  wchar_t* target = buffer;
  if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0) {
    target = static_cast<wchar_t*>(LocalAlloc(LMEM_FIXED, (length + 1) * sizeof(wchar_t)));
    if (target == nullptr) {
      return 0;
    }
    *reinterpret_cast<LPWSTR*>(buffer) = target;
  } else if (length + 1 > size) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  for (size_t i = 0; i <= length; ++i) {
    target[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
  }
  return static_cast<DWORD>(length);
}

} // extern "C"

} // extern "C++"
