encfsy
====

EncFS implementation for Windows with Dokany and Crypto++.
For 64bit environment only.

## Usage
	encfs.exe [options] rootdir mountPoint
	  rootdir (ex. c:\test)                  Directory source to EncFS.
	  mountPoint (ex. m)                     Mount point. Can be M:\ (drive letter) or empty NTFS folder C:\mount\dokan .

	Options:
	  -u mountPoint                          Unmount.
	  -l                                     List mount pounts.
	  -v                                     Enable debug output to an attached debugger.
	  -i Timeout (Milliseconds ex. 30000)    Timeout until a running operation is aborted and the device is unmounted. Default to 30000.
	  -t ThreadCount (ex. 5)                 Number of threads to be used internally by Dokan library.
	                                         More threads will handle more event at the same time. Default to 5.
	  --dokan-network UNC (ex. \host\myfs)   UNC name used for network volume.
	  --dokan-removable                      Show device as removable media.
	  --dokan-write-protect                  Read only filesystem.
	  --dokan-mount-manager                  Register device to Windows mount manager.
	                                         This enables advanced Windows features like recycle bin and more...
	  --dokan-current-session                Device only visible for current user session.
	  --dokan-filelock-user-mode             Enable Lockfile/Unlockfile operations. Otherwise Dokan will take care of it.
	  --public                               Impersonate Caller User when getting the handle in CreateFile for operations.
	                                         This option requires administrator right to work properly.
	  --allocation-unit-size Bytes (ex. 512) Allocation Unit Size of the volume. This will behave on the disk file size.
	  --sector-size Bytes (ex. 512)          Sector Size of the volume. This will behave on the disk file size.
	  --paranoia AES-256bit / changed name IV / external IV chaining
	  --reverse Encrypt rootdir to mountPoint.
	Examples:
	        encfs.exe C:\Users M:                                    # EncFS C:\Users as RootDirectory into a drive of letter M:\.
	        encfs.exe C:\Users C:\mount\dokan                        # EncFS C:\Users as RootDirectory into NTFS folder C:\mount\dokan.
	        encfs.exe C:\Users M: --dokan-network \myfs\myfs1        # EncFS C:\Users as RootDirectory into a network drive M:\. with UNC \\myfs\myfs1

	Unmount the drive with CTRL + C in the console or alternatively via "encfs.exe -u MountPoint".
	
## Install
[Download installer](https://github.com/miyabe/encfsy).

Please install [Dokany](https://github.com/dokan-dev/dokany/releases) before installation.

## Licence

[LGPL](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## Author

[Miyabe Tatsuhiko](https://github.com/miyabe/)
