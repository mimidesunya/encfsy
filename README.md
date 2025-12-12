encfsy
======

🌐 **Language**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## About encfsy
encfsy is a Windows implementation of EncFS powered by **Dokany** and **Crypto++**.
It runs **exclusively on 64‑bit systems**.

The program encrypts file names and contents while leaving the directory tree intact.
This makes it ideal for syncing encrypted data with Dropbox, Google Drive, rsync, or other
remote storage: the files stay encrypted end‑to‑end, so storage administrators cannot see
their contents.

## Supported Languages

The encfsy console application automatically detects your system language and displays messages in the appropriate language.

| Language | Code | Auto-detect |
|----------|------|-------------|
| English | `en` | Default |
| 日本語 (Japanese) | `ja` | Windows Japanese |
| 한국어 (Korean) | `ko` | Windows Korean |
| 简体中文 (Simplified Chinese) | `zh` | Windows Chinese (PRC) |
| 繁體中文 (Traditional Chinese) | `zh-tw` | Windows Chinese (Taiwan/HK) |
| Русский (Russian) | `ru` | Windows Russian |
| العربية (Arabic) | `ar` | Windows Arabic |
| Deutsch (German) | `de` | Windows German |

To manually override the language, use the `--lang` option:
```bash
encfs.exe --lang ja              # Japanese
encfs.exe --lang zh-tw           # Traditional Chinese
encfs.exe --lang ru              # Russian
```

## Security Features
encfsy uses **Windows Credential Manager** for secure password management.

- Passwords are encrypted with **DPAPI** (Data Protection API) and tied to the current user account
- Eliminates the need to pass passwords via stdin between GUI and encfs.exe, removing interception risks
- "Remember Password" option saves passwords for automatic entry on next launch
- Passwords are **stored separately for each encrypted directory (rootDir)**

### Where Passwords Are Stored
Saved passwords can be viewed in Control Panel → Credential Manager → Windows Credentials.
They appear with names like `EncFSy:c:\path\to\encrypted`.

## GUI Usage
Use **encfsw.exe** to easily mount and unmount volumes without the command line.

1. Select the encrypted directory (rootDir)
2. Choose a drive letter for mounting
3. Enter your password (check "Remember Password" to save it)
4. Click "Mount"

"Show Advanced Options" provides access to the same detailed settings as the command-line version.

## Using Credential Manager from Command Line
When you mount with "Remember Password" checked in the GUI, the password is saved to Windows Credential Manager.
You can then mount from the command line without entering a password using the `--use-credential` option.

```bash
# 1. First, mount via GUI with "Remember Password" checked
#    → Password is saved to Credential Manager

# 2. Subsequently, mount from command line without password prompt
encfs.exe C:\Data M: --use-credential
```

## Filename Length Limit
encfsy uses the modern *long‑path* API, so the traditional 260‑character **MAX_PATH**
limit on full paths does **not** apply.

NTFS still caps each path component (folder or file name) at **255 UTF‑16 characters**.
Because encryption inflates names by roughly 30 %, keep **each filename under
175 characters** to stay within that per‑component limit and remain compatible with
tools that are not long‑path aware.

## Usage

```
Usage: encfs.exe [options] <rootDir> <mountPoint>

Arguments:
  rootDir      (e.g., C:\test)                Directory to be encrypted and mounted.
  mountPoint   (e.g., M: or C:\mount\dokan)   Mount location - either a drive letter
                                               such as M:\ or an empty NTFS folder.

Options:
  -u <mountPoint>                              Unmount the specified volume.
  -l                                           List currently mounted Dokan volumes.
  -v                                           Send debug output to an attached debugger.
  -s                                           Send debug output to stderr.
  -i <ms>              (default: 120000)       Timeout (in milliseconds) before a running
                                               operation is aborted and the volume unmounted.
  --use-credential                             Read password from Windows Credential Manager
                                               (password is kept stored).
                                               Note: Password must be saved first via GUI
                                               with "Remember Password" checked.
  --use-credential-once                        Read password from Windows Credential Manager
                                               and delete it after reading (one-time use).
  --dokan-debug                                Enable Dokan debug output.
  --dokan-network <UNC>                        UNC path for a network volume (e.g., \\host\myfs).
  --dokan-removable                            Present the volume as removable media.
  --dokan-write-protect                        Mount the filesystem read-only.
  --dokan-mount-manager                        Register the volume with the Windows Mount Manager
                                               (enables Recycle Bin support, etc.).
  --dokan-current-session                      Make the volume visible only in the current session.
  --dokan-filelock-user-mode                   Handle LockFile/UnlockFile in user mode; otherwise
                                               Dokan manages them automatically.
  --dokan-enable-unmount-network-drive         Allow unmounting network drive via Explorer.
  --dokan-dispatch-driver-logs                 Forward kernel driver logs to userland (slow).
  --dokan-allow-ipc-batching                   Enable IPC batching for slow filesystems
                                               (e.g., remote storage).
  --public                                     Impersonate the calling user when opening handles
                                               in CreateFile. Requires administrator privileges.
  --allocation-unit-size <bytes>               Allocation-unit size reported by the volume.
  --sector-size <bytes>                        Sector size reported by the volume.
  --volume-name <name>                         Volume name shown in Explorer (default: EncFS).
  --volume-serial <hex>                        Volume serial number in hex (default: from underlying).
  --paranoia                                   Enable AES-256 encryption, renamed IVs, and external
                                               IV chaining.
  --alt-stream                                 Enable NTFS alternate data streams.
  --case-insensitive                           Perform case-insensitive filename matching.
  --reverse                                    Reverse mode: show plaintext rootDir as encrypted
                                               at mountPoint.

Examples:
  encfs.exe C:\Users M:                                    # Mount C:\Users as drive M:\
  encfs.exe C:\Users C:\mount\dokan                        # Mount C:\Users at NTFS folder C:\mount\dokan
  encfs.exe C:\Users M: --dokan-network \\myfs\share       # Mount as network drive with UNC \\myfs\share
  encfs.exe C:\Data M: --volume-name "My Secure Drive"     # Mount with custom volume name
  encfs.exe C:\Data M: --use-credential                    # Use stored password from Credential Manager

To unmount, press Ctrl+C in this console or run:
  encfs.exe -u <mountPoint>
```

## Installation
1. Install **Dokany** (≥ 2.0) — download from the [official releases](https://github.com/dokan-dev/dokany/releases).
2. Download the latest **encfsy installer** from the [Releases page](https://github.com/mimidesunya/encfsy/releases) and follow the setup wizard.

## License
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## Author
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
