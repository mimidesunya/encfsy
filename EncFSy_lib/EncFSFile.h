#pragma once
#include <dokan.h>
#include <winbase.h>

#include "EncFSVolume.h"

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>

extern EncFS::EncFSVolume encfs;

namespace EncFS
{
	/**
	 * @brief Result codes for getFileIV operation
	 */
	enum EncFSGetFileIVResult {
		EXISTS,      // File IV exists and was retrieved successfully
		READ_ERROR,  // Error occurred while reading file IV
		EMPTY        // File is empty (no IV present)
	};

	/**
	 * @brief Global file lock manager for coordinating access across multiple handles
	 * 
	 * When multiple processes/threads open the same file with different handles,
	 * each gets its own EncFSFile instance. This manager provides file-level
	 * synchronization to prevent block corruption during concurrent access.
	 */
	class FileLockManager {
	public:
		struct FileLockEntry {
			std::mutex lock;
			std::atomic<int> refCount{0};
		};
		
	private:
		std::mutex mapLock;
		std::unordered_map<std::wstring, std::shared_ptr<FileLockEntry>> fileLocks;
		
		FileLockManager() = default;
		
	public:
		static FileLockManager& getInstance() {
			static FileLockManager instance;
			return instance;
		}
		
		// Get or create a lock for a specific file path
		std::shared_ptr<FileLockEntry> getLock(const std::wstring& filePath);
		
		// Release a file lock (decrements ref count, removes if zero)
		void releaseLock(const std::wstring& filePath);
		
		// Prevent copying
		FileLockManager(const FileLockManager&) = delete;
		FileLockManager& operator=(const FileLockManager&) = delete;
	};
	
	/**
	 * @brief RAII helper for file-level locking
	 */
	class FileScopedLock {
	private:
		std::shared_ptr<FileLockManager::FileLockEntry> lockEntry;
		std::unique_lock<std::mutex> lock;
		std::wstring filePath;  // Store path for releaseLock in destructor
	public:
		FileScopedLock(const std::wstring& filePath);
		~FileScopedLock();
		
		// Prevent copying
		FileScopedLock(const FileScopedLock&) = delete;
		FileScopedLock& operator=(const FileScopedLock&) = delete;
	};

	/**
	 * @brief Handles encrypted file operations with buffering and IV management
	 * 
	 * This class provides read/write operations for encrypted files, managing
	 * file IVs (Initialization Vectors), block-level encryption/decryption,
	 * and buffering for improved performance. All operations are thread-safe.
	 * 
	 * Memory Optimization:
	 * - Lazy buffer allocation (allocated only when first used)
	 * - Small String Optimization (SSO) friendly conversions
	 * - Cache reuse with threshold-based cleanup
	 * - File name conversion caching
	 * 
	 * Performance Optimization:
	 * - Block-level caching
	 * - File name conversion caching (avoid repeated conversions)
	 * - Efficient buffer management
	 */
	class EncFSFile {
	private:
		HANDLE handle;           // Windows file handle
		bool canRead;            // Flag indicating if file should be readable

		int64_t fileIv;          // File initialization vector for encryption
		std::atomic<bool> fileIvAvailable;  // Flag indicating if fileIv has been loaded (atomic for thread safety)

		std::string blockBuffer;      // Buffer for encrypted block data (lazy allocated)
		int64_t lastBlockNum;    // Last accessed block number (for caching)
		std::string encodeBuffer;     // Temporary buffer for encoding operations (lazy allocated)
		std::string decodeBuffer;     // Temporary buffer for decoding operations (lazy allocated)
		
		mutable std::mutex mutexLock;         // Mutex for thread-safe operations within this handle
		
		// File name caching to avoid repeated conversions
		mutable std::string cachedUtf8FileName;  // Cached UTF-8 file name
		mutable std::wstring cachedWideFileName; // Cached wide file name for file-level locking
		mutable bool fileNameCached;        // Flag indicating if file name is cached

	public:
		static std::atomic<int64_t> counter;  // Global atomic counter for active EncFSFile instances

		/**
		 * @brief Constructs an EncFSFile object
		 * @param handle Windows file handle (must be valid)
		 * @param canRead True if file should be readable
		 * @throws EncFSIllegalStateException if handle is invalid
		 * 
		 * Note: Buffers are allocated on-demand for better memory efficiency.
		 * Typical memory savings: ~3KB per file instance when not accessed.
		 */
		EncFSFile(HANDLE handle, bool canRead);

		/**
		 * @brief Destructor - closes file handle and decrements counter
		 */
		~EncFSFile();

		// Prevent copy construction and assignment
		EncFSFile(const EncFSFile&) = delete;
		EncFSFile& operator=(const EncFSFile&) = delete;

		/**
		 * @brief Gets the underlying Windows file handle
		 * @return Windows file handle
		 */
		inline HANDLE getHandle() const {
			return this->handle;
		}

		/**
		 * @brief Reads decrypted data from the file
		 * @param FileName File path for IV computation
		 * @param buff Output buffer for decrypted data
		 * @param off Offset in the logical (decrypted) file
		 * @param len Number of bytes to read
		 * @return Number of bytes read, or -1 on error
		 */
		int32_t read(const LPCWSTR FileName, char* buff, size_t off, DWORD len);

		/**
		 * @brief Writes encrypted data to the file
		 * @param FileName File path for IV computation
		 * @param fileSize Current logical file size
		 * @param buff Input buffer containing data to encrypt and write
		 * @param off Offset in the logical (decrypted) file
		 * @param len Number of bytes to write
		 * @return Number of bytes written, or -1 on error
		 */
		int32_t write(const LPCWSTR FileName, size_t fileSize, const char* buff, size_t off, DWORD len);

		/**
		 * @brief Reads and encrypts data in reverse mode (for reverse encryption)
		 * @param FileName File path for IV computation
		 * @param buff Output buffer for encrypted data
		 * @param off Offset in the physical file
		 * @param len Number of bytes to read and encrypt
		 * @return Number of bytes read, or -1 on error
		 */
		int32_t reverseRead(const LPCWSTR FileName, char* buff, size_t off, DWORD len);

		/**
		 * @brief Flushes file buffers to disk
		 * @return True on success, false on failure
		 */
		bool flush();

		/**
		 * @brief Sets the logical file length (with encryption/decryption)
		 * @param FileName File path for IV computation
		 * @param length New logical file size in bytes
		 * @return True on success, false on failure
		 */
		bool setLength(const LPCWSTR FileName, const size_t length);

		/**
		 * @brief Updates file IV when file is renamed (for chained IV mode)
		 * @param FileName Original file path
		 * @param NewFileName New file path
		 * @return True on success, false on failure
		 */
		bool changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName);

		/**
		 * @brief Invalidates cached file name after rename operation
		 * 
		 * This should be called after a successful rename to ensure subsequent
		 * operations use the new file path for IV calculations.
		 */
		void invalidateFileNameCache();

	private:
		/**
		 * @brief Retrieves or creates the file IV
		 * @param FileName File path for IV computation
		 * @param fileIv Output parameter for the file IV
		 * @param create If true, create IV if it doesn't exist
		 * @return Result code indicating success or error type
		 */
		EncFSGetFileIVResult getFileIV(const LPCWSTR FileName, int64_t *fileIv, bool create);

		/**
		 * @brief Internal implementation for setting file length
		 * @param FileName File path for IV computation
		 * @param fileSize Current logical file size
		 * @param length New logical file size
		 * @return True on success, false on failure
		 */
		bool _setLength(const LPCWSTR FileName, const size_t fileSize, const size_t length);

		/**
		 * @brief Clears block buffer (size only, capacity retained for reuse)
		 */
		void clearBlockBuffer();

		/**
		 * @brief Gets cached UTF-8 file name or converts and caches it
		 * @param wstr Wide std::string file name
		 * @return Cached or newly converted UTF-8 file name
		 * 
		 * Performance: Avoids repeated std::string conversions for the same file
		 */
		const std::string& getCachedUtf8FileName(const LPCWSTR wstr);
		
		/**
		 * @brief Gets cached wide file name for file-level locking
		 * @param wstr Wide string file name
		 * @return Cached wide file name
		 */
		const std::wstring& getCachedWideFileName(const LPCWSTR wstr);

		/**
		 * @brief Converts wide std::string to UTF-8 std::string (optimized for short strings)
		 * @param wstr Wide std::string to convert
		 * @param output Output std::string (preallocated for efficiency)
		 * @return True on success, false on failure
		 * 
		 * Optimization: Uses output parameter to avoid return value copy
		 * and leverage Small String Optimization (SSO)
		 */
		static bool WideToUtf8(const wchar_t* wstr, std::string& output);

		/**
		 * @brief Converts UTF-8 std::string to wide std::string (optimized for short strings)
		 * @param utf8 UTF-8 encoded std::string to convert
		 * @param output Output wide std::string (preallocated for efficiency)
		 * @return True on success, false on failure
		 */
		static bool Utf8ToWide(const std::string& utf8, std::wstring& output);
		
		/**
		 * @brief Ensures encode/decode buffers are allocated
		 * 
		 * This lazy allocation saves ~2KB of memory per file instance
		 * when files are opened but not read/written.
		 */
		inline void ensureBuffersAllocated() {
			if (this->encodeBuffer.capacity() == 0) {
				const size_t bs = encfs.getBlockSize();
				this->blockBuffer.reserve(bs);
				this->encodeBuffer.reserve(bs);
				this->decodeBuffer.reserve(bs);
			}
		}
	};
}
