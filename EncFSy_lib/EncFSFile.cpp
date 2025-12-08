#include "EncFSFile.h"
#include <winioctl.h> 
#include <osrng.h> // For AutoSeededX917RNG
#include <algorithm>

using namespace std;
using namespace CryptoPP;

// Thread-local random number generator for cryptographic operations
static thread_local AutoSeededX917RNG<AES> random;

namespace EncFS {

    namespace {
        // Define constants to avoid magic numbers
        const size_t SPARSE_FILE_THRESHOLD = 100 * 1024 * 1024; // 100MB
        const int MAX_WRITE_RETRIES = 3;
        const DWORD WRITE_RETRY_DELAY_MS = 10;
        const size_t BLOCK_BUFFER_REUSE_THRESHOLD = 64 * 1024; // Keep buffer if <= 64KB
    }

    // Initialize atomic counter
    std::atomic<int64_t> EncFSFile::counter(0);

    //=========================================================================
    // FileLockManager Implementation
    //=========================================================================
    
    std::shared_ptr<FileLockManager::FileLockEntry> FileLockManager::getLock(const wchar_t* filePath) {
        std::lock_guard<std::mutex> guard(mapLock);
        
        // Use temporary wstring for map lookup (unavoidable for unordered_map)
        std::wstring key(filePath);
        auto it = fileLocks.find(key);
        if (it != fileLocks.end()) {
            it->second->refCount.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
        
        // Create new lock entry
        auto entry = std::make_shared<FileLockEntry>();
        entry->refCount.store(1, std::memory_order_relaxed);
        fileLocks.emplace(std::move(key), entry);
        return entry;
    }
    
    void FileLockManager::releaseLock(const wchar_t* filePath) {
        std::lock_guard<std::mutex> guard(mapLock);
        
        auto it = fileLocks.find(filePath);
        if (it != fileLocks.end()) {
            int oldCount = it->second->refCount.fetch_sub(1, std::memory_order_acq_rel);
            if (oldCount <= 1) {
                // No more references, remove from map
                fileLocks.erase(it);
            }
        }
    }
    
    //=========================================================================
    // FileScopedLock Implementation
    //=========================================================================
    
    FileScopedLock::FileScopedLock(const wchar_t* path) 
        : lockEntry(FileLockManager::getInstance().getLock(path))
        , lock(lockEntry->lock)
        , filePathPtr(path)
    {
    }
    
    FileScopedLock::~FileScopedLock() {
        // First release the lock to allow other threads to proceed
        lock.unlock();
        // Then release our reference to the lock entry
        FileLockManager::getInstance().releaseLock(filePathPtr);
    }

    //=========================================================================
    // EncFSFile Implementation
    //=========================================================================

    /**
     * @brief Constructor implementation
     */
    EncFSFile::EncFSFile(HANDLE handle, bool canRead) 
        : handle(handle)
        , canRead(canRead)
        , fileIv(0L)
        , fileIvAvailable(false)  // atomic<bool> initialization
        , lastBlockNum(-1)
        , fileNameCached(false)
    {
        if (!handle || handle == INVALID_HANDLE_VALUE) {
            throw EncFSIllegalStateException();
        }
        
        // Buffers are now lazily allocated on first use
        // This saves memory for files that are only opened but not accessed
        
        // Increment counter (atomic operation)
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Destructor implementation
     */
    EncFSFile::~EncFSFile() {
        // Close handle if valid
        if (this->handle && this->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(this->handle);
        }
        
        // Decrement counter (atomic operation)
        counter.fetch_sub(1, std::memory_order_relaxed);
    }

    /**
     * @brief Gets cached UTF-8 file name or converts and caches it
     */
    const string& EncFSFile::getCachedUtf8FileName(const LPCWSTR wstr) {
        if (!this->fileNameCached) {
            WideToUtf8(wstr, this->cachedUtf8FileName);
            this->cachedWideFileName = wstr;
            this->fileNameCached = true;
        }
        return this->cachedUtf8FileName;
    }
    
    /**
     * @brief Gets cached wide file name for file-level locking
     */
    const std::wstring& EncFSFile::getCachedWideFileName(const LPCWSTR wstr) {
        if (!this->fileNameCached) {
            WideToUtf8(wstr, this->cachedUtf8FileName);
            this->cachedWideFileName = wstr;
            this->fileNameCached = true;
        }
        return this->cachedWideFileName;
    }

    /**
     * @brief Converts wide string to UTF-8 using Win32 API (optimized)
     */
    bool EncFSFile::WideToUtf8(const wchar_t* wstr, string& output) {
        if (!wstr || *wstr == L'\0') {
            output.clear();
            return true;
        }

        // Get required buffer size
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
        if (sizeNeeded <= 0) {
            output.clear();
            return false;
        }

        // Resize output buffer and convert (SSO-friendly)
        output.resize(sizeNeeded - 1); // -1 to exclude null terminator
        int result = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &output[0], sizeNeeded, NULL, NULL);
        return result > 0;
    }

    /**
     * @brief Converts UTF-8 string to wide string using Win32 API (optimized)
     */
    bool EncFSFile::Utf8ToWide(const string& utf8, std::wstring& output) {
        if (utf8.empty()) {
            output.clear();
            return true;
        }

        // Get required buffer size
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
        if (sizeNeeded <= 0) {
            output.clear();
            return false;
        }

        // Resize output buffer and convert (SSO-friendly)
        output.resize(sizeNeeded - 1); // -1 to exclude null terminator
        int result = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &output[0], sizeNeeded);
        return result > 0;
    }

    /**
     * @brief Helper function for partial write with retry logic
     * @param handle File handle
     * @param data Data to write
     * @param size Size of data to write
     * @return True on success, false on failure
     */
    static bool WriteWithRetry(HANDLE handle, const char* data, size_t size) {
        size_t totalWritten = 0;
        int consecutiveZeroWrites = 0;
        
        while (totalWritten < size) {
            DWORD writtenLen = 0;
            DWORD request = (DWORD)(size - totalWritten);
            
            if (!WriteFile(handle, data + totalWritten, request, &writtenLen, NULL)) {
                return false;
            }
            
            if (writtenLen == 0) {
                if (++consecutiveZeroWrites > MAX_WRITE_RETRIES) {
                    SetLastError(ERROR_WRITE_FAULT);
                    return false;
                }
                Sleep(WRITE_RETRY_DELAY_MS);
                continue;
            }
            
            consecutiveZeroWrites = 0;
            totalWritten += writtenLen;
        }
        return true;
    }

    /**
     * @brief Invalidates all cached data
     */
    static void InvalidateCache(int64_t* lastBlockNum, string* decodeBuffer, string* encodeBuffer) {
        *lastBlockNum = -1;
        decodeBuffer->clear();
        encodeBuffer->clear();
    }

    /**
     * @brief Retrieves or creates the file initialization vector (IV)
     * @param FileName File path used for IV computation
     * @param fileIv Output parameter receiving the file IV
     * @param create If true, creates a new IV if one doesn't exist
     * @return Result code indicating success or error type
     */
    EncFSGetFileIVResult EncFSFile::getFileIV(const LPCWSTR FileName, int64_t* fileIv, bool create) {
        // Return cached IV if already available (atomic read for thread safety)
        if (this->fileIvAvailable.load(std::memory_order_acquire)) {
            *fileIv = this->fileIv;
            return EXISTS;
        }
        
        // If unique IV is not enabled, use 0 as the IV
        if (!encfs.isUniqueIV()) {
            this->fileIv = *fileIv = 0L;
            this->fileIvAvailable.store(true, std::memory_order_release);
            return EXISTS;
        }
        
        // Get current file size first to detect empty files or files without IV
        LARGE_INTEGER currentFileSize;
        if (!GetFileSizeEx(this->handle, &currentFileSize)) {
            return READ_ERROR;
        }
        
        // If file is empty and we're not creating, return EMPTY
        if (currentFileSize.QuadPart == 0 && !create) {
            return EMPTY;
        }
        
        // If file size is less than header size and we're not creating, it's incomplete
        // NOTE: We avoid Sleep() here to prevent blocking while holding the lock
        // Instead, we return READ_ERROR and let the caller retry if needed
        if (currentFileSize.QuadPart > 0 && currentFileSize.QuadPart < (LONGLONG)EncFSVolume::HEADER_SIZE && !create) {
            SetLastError(ERROR_READ_FAULT);
            return READ_ERROR;
        }
        
        // Read file header containing the IV
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return READ_ERROR;
        }
        
        string fileHeader;
        fileHeader.resize(EncFSVolume::HEADER_SIZE);
        DWORD ReadLength = 0;
        if (!ReadFile(this->handle, &fileHeader[0], (DWORD)fileHeader.size(), &ReadLength, NULL)) {
            return READ_ERROR;
        }
        
        // Check if we read a complete header
        if (ReadLength != (DWORD)fileHeader.size()) {
            if (!create) {
                if (ReadLength == 0 && currentFileSize.QuadPart == 0) {
                    return EMPTY;
                }
                // Partial header read - file may be incomplete or being written concurrently
                SetLastError(ERROR_READ_FAULT);
                return READ_ERROR;
            }
            
            // Create new file header with random IV
            fileHeader.resize(EncFSVolume::HEADER_SIZE);
            random.GenerateBlock((byte*)&fileHeader[0], EncFSVolume::HEADER_SIZE);
            
            // Seek back to beginning before writing
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return READ_ERROR;
            }
            
            // Write header with retry logic
            if (!WriteWithRetry(this->handle, fileHeader.data(), fileHeader.size())) {
                return READ_ERROR;
            }
            
            // Flush to ensure header is persisted before any subsequent operations
            FlushFileBuffers(this->handle);
        }

        // Decode the IV from the header (use cached conversion)
        const string& cFileName = getCachedUtf8FileName(FileName);
        this->fileIv = *fileIv = encfs.decodeFileIv(cFileName, fileHeader);
        this->fileIvAvailable.store(true, std::memory_order_release);
        return EXISTS;
    }

    /**
     * @brief Reads and decrypts data from the encrypted file
     * @param FileName File path for IV computation
     * @param buff Output buffer for decrypted data
     * @param off Offset in the logical (decrypted) file
     * @param len Number of bytes to read
     * @return Number of bytes read, or -1 on error
     */
    int32_t EncFSFile::read(const LPCWSTR FileName, char* buff, size_t off, DWORD len) {
        // Acquire instance lock only - each Dokan handle is independent
        // Cross-handle synchronization is managed by Windows/Dokan
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        
        // Validate handle before any operation
        if (!this->handle || this->handle == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        
        if (!this->canRead) {
            SetLastError(ERROR_READ_FAULT);
            return -1;
        }

        // Handle zero-length read request
        if (len == 0) {
            return 0;
        }

        try {
            // Lazy allocate buffers if needed
            this->ensureBuffersAllocated();

            // Get file IV with retry logic for concurrent write scenarios
            int64_t fileIv;
            EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, false);
            
            if (ivResult == READ_ERROR) {
                // Check if this might be a timing issue with concurrent write
                LARGE_INTEGER actualFileSize;
                if (GetFileSizeEx(this->handle, &actualFileSize) && 
                    actualFileSize.QuadPart >= (LONGLONG)EncFSVolume::HEADER_SIZE) {
                    // File has enough data for header, try again
                    this->fileIvAvailable.store(false, std::memory_order_release);
                    ivResult = this->getFileIV(FileName, &fileIv, false);
                }
                if (ivResult == READ_ERROR) {
                    return -1;
                }
            }
            
            if (ivResult == EMPTY) {
                // Double-check file size - file might have been written by another handle
                LARGE_INTEGER actualFileSize;
                if (GetFileSizeEx(this->handle, &actualFileSize) && actualFileSize.QuadPart > 0) {
                    // File has data but we got EMPTY - try again
                    this->fileIvAvailable.store(false, std::memory_order_release);
                    ivResult = this->getFileIV(FileName, &fileIv, false);
                    if (ivResult == READ_ERROR) {
                        return -1;
                    }
                    if (ivResult == EMPTY) {
                        // Still empty - something wrong
                        SetLastError(ERROR_FILE_CORRUPT);
                        return -1;
                    }
                } else {
                    return 0;  // File is truly empty
                }
            }

            // Calculate block positions
            const size_t blockSize = encfs.getBlockSize();
            const size_t blockHeaderSize = encfs.getHeaderSize();
            const size_t blockDataSize = blockSize - blockHeaderSize;
            
            // Safeguard against invalid block configuration
            if (blockDataSize == 0) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return -1;
            }
            
            size_t shift = off % blockDataSize;
            size_t blockNum = off / blockDataSize;
            const size_t lastBlockNum = (off + len - 1) / blockDataSize;

            int32_t copiedLen = 0;

            // Calculate how many encrypted blocks to read
            size_t blocksOffset = blockNum * blockSize;
            const size_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }

            if (blocksLength) {
                // Seek to read position
                LARGE_INTEGER distanceToMove;
                distanceToMove.QuadPart = blocksOffset;
                if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                    DWORD err = GetLastError();
                    SetLastError(err);
                    return -1;
                }

                // Prepare buffer for encrypted data
                this->blockBuffer.resize(blocksLength);

                // Read encrypted data
                DWORD totalRead = 0;
                while (totalRead < blocksLength) {
                    DWORD readLen = 0;
                    DWORD toRead = (DWORD)(blocksLength - totalRead);
                    if (!ReadFile(this->handle, &this->blockBuffer[0] + totalRead, toRead, &readLen, NULL)) {
                        DWORD err = GetLastError();
                        this->blockBuffer.clear();  // Keep capacity
                        SetLastError(err);
                        return -1;
                    }
                    if (readLen == 0) {
                        // End of file reached
                        break;
                    }
                    totalRead += readLen;
                }

                // If nothing was read, return 0 (end of file or reading beyond file)
                if (totalRead == 0) {
                    // Keep capacity for reuse, just clear size
                    this->blockBuffer.clear();
                    return 0;
                }

                // Decrypt the read blocks
                size_t readPos = 0;
                while (readPos < totalRead && len > 0) {
                    size_t remain = totalRead - readPos;
                    size_t blockLen = (remain > blockSize) ? blockSize : remain;

                    // Decrypt current block
                    this->encodeBuffer.assign((const char*)&this->blockBuffer[readPos], blockLen);
                    this->decodeBuffer.clear();
                    encfs.decodeBlock(fileIv, this->lastBlockNum = blockNum, this->encodeBuffer, this->decodeBuffer);

                    // Copy decrypted data to output buffer
                    size_t decLen = this->decodeBuffer.size();
                    if (decLen > shift) {
                        decLen -= shift;
                    } else {
                        decLen = 0;
                    }
                    if (decLen > len) {
                        decLen = len;
                    }
                    if (decLen > 0) {
                        memcpy(buff + copiedLen, this->decodeBuffer.data() + shift, decLen);
                        shift = 0;
                        len -= (DWORD)decLen;
                        copiedLen += (int32_t)decLen;
                    }
                    blockNum++;
                    readPos += blockLen;
                }

                // Keep capacity for reuse, just clear size
                this->blockBuffer.clear();
            }
            return copiedLen;
        }
        catch (const EncFSInvalidBlockException&) {
            SetLastError(ERROR_FILE_CORRUPT);
            return -1;
        }
    }

    /**
     * @brief Encrypts and writes data to the file
     * @param FileName File path for IV computation
     * @param fileSize Current logical file size
     * @param buff Input buffer containing data to encrypt
     * @param off Offset in the logical file
     * @param len Number of bytes to write
     * @return Number of bytes written, or -1 on error
     */
    int32_t EncFSFile::write(const LPCWSTR FileName, size_t fileSize, const char* buff, size_t off, DWORD len) {
        // Acquire instance lock only - each Dokan handle is independent
        // Cross-handle synchronization is managed by Windows/Dokan
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        
        // Validate handle before any operation
        if (!this->handle || this->handle == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        
        // Handle zero-length write request
        if (len == 0) {
            return 0;
        }
        
        try {
            // Lazy allocate buffers if needed
            this->ensureBuffersAllocated();

            // Get or create file IV
            int64_t fileIv;
            EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, true);
            if (ivResult == READ_ERROR) {
                SetLastError(ERROR_FILE_CORRUPT);
                return -1;
            }

            // Expand file if writing beyond current size
            if (off > fileSize) {
                if (!this->_setLength(FileName, fileSize, off)) {
                    SetLastError(ERROR_FILE_CORRUPT);
                    return -1;
                }
                // Update fileSize after expansion
                fileSize = off;
            }

            // Calculate block positions
            const size_t blockSize = encfs.getBlockSize();
            const size_t blockHeaderSize = encfs.getHeaderSize();
            const size_t blockDataSize = blockSize - blockHeaderSize;
            
            // Safeguard against invalid block configuration
            if (blockDataSize == 0) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return -1;
            }
            
            size_t shift = off % blockDataSize;
            size_t blockNum = off / blockDataSize;
            const size_t lastBlockNum = (off + len - 1) / blockDataSize;
            size_t blocksOffset = blockNum * blockSize;
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }

            // Seek to write position
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return -1;
            }

            // Handle partial block write at the beginning
            if (shift != 0) {
                // Read existing block data if file has content at this position
                // For files that were just truncated to 0 or new files, fileSize will be 0
                // and we should not try to read non-existent data
                size_t blockStartOffset = blockNum * blockDataSize;
                // Only try to read if the block actually has data (file extends into this block)
                bool blockHasData = (fileSize > blockStartOffset);
                
                if (blockHasData) {
                    // Read existing block from disk
                    this->encodeBuffer.resize(encfs.getBlockSize());
                    DWORD readLen;
                    if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                        return -1;
                    }
                    // Rewind file pointer
                    if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                        return -1;
                    }
                    
                    if (readLen > 0) {
                        this->encodeBuffer.resize(readLen);
                        this->decodeBuffer.clear();
                        encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
                    } else {
                        this->decodeBuffer.clear();
                    }
                } else {
                    this->decodeBuffer.clear();
                }
                
                // Pad with zeros if necessary (for partial writes at an offset)
                if (this->decodeBuffer.size() < shift) {
                    this->decodeBuffer.append(shift - this->decodeBuffer.size(), (char)0);
                }
            }

            size_t blockDataLen = 0;
            size_t writtenTotal = 0;
            
            // Write data block by block
            for (size_t i = 0; i < len; i += blockDataLen) {
                blockDataLen = (len - i) > blockDataSize - shift ? (blockDataSize - shift) : (len - i);

                // Prepare decrypted block data
                if (shift != 0) {
                    // Partial block write (first block with offset)
                    if (this->decodeBuffer.size() < shift + blockDataLen) {
                        this->decodeBuffer.resize(shift + blockDataLen);
                    }
                    memcpy(&this->decodeBuffer[shift], buff + i, blockDataLen);
                }
                else if (blockDataLen == blockDataSize) {
                    // Full block - no need to read existing data
                    this->decodeBuffer.assign(buff + i, blockDataLen);
                }
                else if (off + i + blockDataLen >= fileSize) {
                    // EOF block or writing beyond file size - no need to read beyond file
                    this->decodeBuffer.assign(buff + i, blockDataLen);
                }
                else {
                    // Partial block in the middle of existing file - Read-modify-write
                    this->encodeBuffer.resize(encfs.getBlockSize());
                    DWORD readLen;
                    if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                        return -1;
                    }
                    // Rewind file pointer
                    LARGE_INTEGER backMove;
                    backMove.QuadPart = -(LONGLONG)readLen;
                    if (!SetFilePointerEx(this->handle, backMove, NULL, FILE_CURRENT)) {
                        return -1;
                    }
                    
                    if (readLen > 0) {
                        this->encodeBuffer.resize(readLen);
                        this->decodeBuffer.clear();
                        encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
                    } else {
                        this->decodeBuffer.clear();
                    }

                    if (this->decodeBuffer.size() < blockDataLen) {
                        this->decodeBuffer.resize(blockDataLen);
                    }
                    memcpy(&this->decodeBuffer[0], buff + i, blockDataLen);
                }

                // Encrypt and write the block
                this->encodeBuffer.clear();
                encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

                // Write with retry logic
                if (!WriteWithRetry(this->handle, this->encodeBuffer.data(), this->encodeBuffer.size())) {
                    return -1;
                }

                blockNum++;
                shift = 0;
                writtenTotal += blockDataLen;
            }

            // Note: FlushFileBuffers removed here for performance
            // The caller (Dokan callback) is responsible for flushing when needed
            // This improves throughput significantly for sequential writes

            return (int32_t)writtenTotal;
        }
        catch (const EncFSInvalidBlockException&) {
            SetLastError(ERROR_FILE_CORRUPT);
            return -1;
        }
    }

    /**
     * @brief Reads plain data and encrypts it (reverse mode operation)
     * @param FileName File path for IV computation
     * @param buff Output buffer for encrypted data
     * @param off Offset in the physical file
     * @param len Number of bytes to read
     * @return Number of bytes read, or -1 on error
     */
    int32_t EncFSFile::reverseRead(const LPCWSTR FileName, char* buff, size_t off, DWORD len) {
        // Acquire instance lock only - each Dokan handle is independent
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        
        // Validate handle before any operation
        if (!this->handle || this->handle == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        
        if (!this->canRead) {
            SetLastError(ERROR_READ_FAULT);
            return -1;
        }

        // Handle zero-length read request
        if (len == 0) {
            return 0;
        }

        int64_t fileIv = 0; // File IV is not used in reverse mode

        // Calculate block positions
        int32_t blockSize = encfs.getBlockSize();
        
        // Safeguard against invalid block configuration
        if (blockSize <= 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return -1;
        }
        
        int32_t shift = off % blockSize;
        int64_t blockNum = off / blockSize;
        int64_t lastBlockNum = (off + len - 1) / blockSize;

        int64_t blocksOffset = blockNum * blockSize;
        int64_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;

        int32_t i = 0;
        int32_t blockDataLen;

        // Use cached block if available (with bounds check)
        if (blockNum == this->lastBlockNum && this->encodeBuffer.size() > (size_t)shift) {
            blockDataLen = (int32_t)std::min<size_t>(len, this->encodeBuffer.size() - shift);
            memcpy(buff, &this->encodeBuffer[shift], blockDataLen);
            blocksOffset = blockNum++ * blockSize;
            shift = 0;
            i += blockDataLen;
            if (i >= (int32_t)len || this->encodeBuffer.size() < (size_t)blockSize) {
                return i;
            }
        }

        // Seek to read position
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = blocksOffset;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return -1;
        }

        // Prepare buffer
        this->blockBuffer.resize(blocksLength);

        // Read with partial read handling
        DWORD totalRead = 0;
        while ((int64_t)totalRead < blocksLength) {
            DWORD readLen = 0;
            DWORD toRead = (DWORD)(blocksLength - totalRead);
            if (!ReadFile(this->handle, &this->blockBuffer[0] + totalRead, toRead, &readLen, NULL)) {
                return -1;
            }
            if (readLen == 0) {
                break;
            }
            totalRead += readLen;
        }
        
        // Adjust len if we read less than expected (accounting for shift)
        DWORD maxAvailable = totalRead + i;
        if (shift > 0 && totalRead > 0) {
            // First block might have shift offset
            maxAvailable = i + totalRead - shift;
        }
        if ((DWORD)len > maxAvailable) {
            len = maxAvailable;
        }
        if (i >= (int32_t)len) {
            // Keep capacity for reuse
            this->blockBuffer.clear();
            return i;
        }

        // Encrypt the read data block by block
        int64_t bufferPos = 0;
        for (; i < (int32_t)len; i += blockDataLen) {
            size_t remainInBuffer = totalRead - (bufferPos * blockSize);
            if (remainInBuffer == 0) {
                break;
            }
            
            blockDataLen = (int32_t)std::min<size_t>(len - i, blockSize - shift);
            blockDataLen = (int32_t)std::min<size_t>(blockDataLen, remainInBuffer - shift);

            // Prepare plain data
            size_t copySize = blockDataLen + shift;
            if (copySize > remainInBuffer) {
                copySize = remainInBuffer;
            }
            this->decodeBuffer.resize(copySize);
            memcpy(&this->decodeBuffer[0], &this->blockBuffer[bufferPos * blockSize], copySize);

            // Encrypt the block
            this->encodeBuffer.clear();
            encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

            // Copy encrypted data to output buffer (with bounds check)
            size_t availableInEncode = this->encodeBuffer.size();
            if (availableInEncode > (size_t)shift) {
                availableInEncode -= shift;
            } else {
                availableInEncode = 0;
            }
            if ((size_t)blockDataLen > availableInEncode) {
                blockDataLen = (int32_t)availableInEncode;
            }
            if (blockDataLen > 0) {
                memcpy(buff + i, &this->encodeBuffer[shift], blockDataLen);
            }

            blockNum++;
            bufferPos++;
            shift = 0;
        }
        
        // Keep capacity for reuse
        this->blockBuffer.clear();
        return i;
    }

    /**
     * @brief Flushes file buffers to disk
     * @return True on success, false on failure
     */
    bool EncFSFile::flush() {
        return FlushFileBuffers(this->handle);
    }

    /**
     * @brief Sets the logical file length
     * @param FileName File path for IV computation
     * @param length New logical file size
     * @return True on success, false on failure
     */
    bool EncFSFile::setLength(const LPCWSTR FileName, const size_t length) {
        // Acquire instance lock only - each Dokan handle is independent
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        
        // Validate handle before any operation
        if (!this->handle || this->handle == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return false;
        }

        // Get current file size
        LARGE_INTEGER encodedFileSize;
        if (!GetFileSizeEx(this->handle, &encodedFileSize)) {
            return false;
        }
        size_t fileSize = encfs.toDecodedLength(encodedFileSize.QuadPart);
        if (fileSize == length) {
            return true;
        }

        return this->_setLength(FileName, fileSize, length);
    }

    /**
     * @brief Internal implementation for setting file length
     * @param FileName File path for IV computation
     * @param fileSize Current logical file size
     * @param length New logical file size
     * @return True on success, false on failure
     */
    bool EncFSFile::_setLength(const LPCWSTR FileName, const size_t fileSize, const size_t length) {
        // Enable sparse file for large files
        if (length >= SPARSE_FILE_THRESHOLD) {
            DWORD temp;
            DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &temp, NULL);
        }

        // Handle truncation to zero
        if (length == 0) {
            LARGE_INTEGER offset;
            offset.QuadPart = 0;
            if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
                return false;
            }
            if (!SetEndOfFile(this->handle)) {
                return false;
            }
            this->fileIvAvailable.store(false, std::memory_order_release);
            InvalidateCache(&this->lastBlockNum, &this->decodeBuffer, &this->encodeBuffer);
            return true;
        }

        // Calculate block boundary positions
        size_t blockHeaderSize = encfs.getHeaderSize();
        size_t blockDataSize = encfs.getBlockSize() - blockHeaderSize;
        
        // Safeguard against invalid block configuration
        if (blockDataSize == 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        
        size_t shift;
        size_t blockNum;
        size_t blocksOffset = 0;  // Initialize to prevent undefined behavior
        
        if (length < fileSize) {
            // Shrinking file
            shift = length % blockDataSize;
            blockNum = length / blockDataSize;
        }
        else {
            // Expanding file
            shift = fileSize % blockDataSize;
            blockNum = fileSize / blockDataSize;
        }
        
        // Get file IV
        int64_t fileIv;
        if (this->getFileIV(FileName, &fileIv, true) == READ_ERROR) {
            return false;
        }
        
        // Save boundary block data before modifying file size
        string boundaryBlockData;
        bool haveBoundaryBlock = false;
        
        if (shift != 0) {
            blocksOffset = blockNum * encfs.getBlockSize();
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return false;
            }
            
            // Read and decrypt boundary block
            this->encodeBuffer.resize(encfs.getBlockSize());
            DWORD readLen;
            if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                return false;
            }
            
            if (readLen > 0) {
                this->encodeBuffer.resize(readLen);
                this->decodeBuffer.clear();
                try {
                    encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
                    // Save the decrypted data for later use
                    boundaryBlockData = this->decodeBuffer;
                    haveBoundaryBlock = true;
                }
                catch (const EncFSInvalidBlockException&) {
                    // If decryption fails, treat as empty block
                    haveBoundaryBlock = false;
                }
            }
        }

        // Set the new encrypted file size
        size_t encodedLength = encfs.toEncodedLength(length);
        {
            LARGE_INTEGER offset;
            offset.QuadPart = encodedLength;
            if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
                return false;
            }
            if (!SetEndOfFile(this->handle)) {
                return false;
            }
        }
        
        // Invalidate cache after file size change
        InvalidateCache(&this->lastBlockNum, &this->decodeBuffer, &this->encodeBuffer);

        // Re-encrypt and write boundary block if needed
        if (shift != 0 && haveBoundaryBlock) {
            size_t blockDataLen = length - blockNum * blockDataSize;
            if (blockDataLen > blockDataSize) {
                blockDataLen = blockDataSize;
            }
            
            // Restore the saved boundary block data
            this->decodeBuffer = boundaryBlockData;
            
            // Adjust decoded buffer size
            if (this->decodeBuffer.size() < blockDataLen) {
                this->decodeBuffer.append(blockDataLen - this->decodeBuffer.size(), (char)0);
            } else {
                this->decodeBuffer.resize(blockDataLen);
            }
            
            // Encrypt the block
            this->encodeBuffer.clear();
            encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

            // Seek back to block position
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = (LONGLONG)blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return false;
            }

            // Write with retry logic
            if (!WriteWithRetry(this->handle, this->encodeBuffer.data(), this->encodeBuffer.size())) {
                return false;
            }
        }

        // Handle expansion - encode trailing block if needed
        if (length > fileSize) {
            size_t s = length % blockDataSize;
            if (s != 0 && (fileSize == 0 || blockNum != length / blockDataSize)) {
                blockNum = length / blockDataSize;
                blocksOffset = blockNum * encfs.getBlockSize();
                if (encfs.isUniqueIV()) {
                    blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
                }
                
                this->decodeBuffer.assign(s, (char)0);
                this->encodeBuffer.clear();
                encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
                
                // Seek to the correct block position
                LARGE_INTEGER distanceToMove;
                distanceToMove.QuadPart = (LONGLONG)blocksOffset;
                if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                    return false;
                }

                // Write with retry logic
                if (!WriteWithRetry(this->handle, this->encodeBuffer.data(), this->encodeBuffer.size())) {
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * @brief Updates file IV when file is moved or renamed
     * @param FileName Original file path
     * @param NewFileName New file path  
     * @return True on success, false on failure
     */
    bool EncFSFile::changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName) {
        // Get current file IV
        int64_t fileIv;
        EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, false);
        if (ivResult == EMPTY) {
            return true;
        }
        if (ivResult == READ_ERROR) {
            return false;
        }
        
        // Encode new IV based on new filename (use optimized conversion)
        string cNewFileName;
        WideToUtf8(NewFileName, cNewFileName);
        string encodedFileHeader;
        encfs.encodeFileIv(cNewFileName, fileIv, encodedFileHeader);
        
        // Seek to file beginning
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return false;
        }
        
        // Write new IV with retry logic
        if (!WriteWithRetry(this->handle, encodedFileHeader.data(), encodedFileHeader.size())) {
            return false;
        }
        
        // Invalidate caches after IV change - the file will be accessed with new name after rename
        // This ensures the next read/write operation uses the new path for IV calculation
        this->fileNameCached = false;
        this->cachedUtf8FileName.clear();
        this->cachedWideFileName.clear();
        this->fileIvAvailable.store(false, std::memory_order_release);
        this->lastBlockNum = -1;  // Invalidate block cache too
        
        return true;
    }

    /**
     * @brief Clears block buffer while retaining capacity for reuse
     */
    void EncFSFile::clearBlockBuffer() {
        // Clear size only, capacity is retained for efficient reuse
        this->blockBuffer.clear();
    }

    /**
     * @brief Invalidates cached file name after rename operation
     */
    void EncFSFile::invalidateFileNameCache() {
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        this->fileNameCached = false;
        this->cachedUtf8FileName.clear();
        this->cachedWideFileName.clear();
        // Also invalidate block cache since it was computed with old path
        this->lastBlockNum = -1;
        this->decodeBuffer.clear();
        this->encodeBuffer.clear();
        // Reset IV cache as well
        this->fileIvAvailable.store(false, std::memory_order_release);
    }

} // namespace EncFS
