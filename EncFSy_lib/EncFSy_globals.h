#pragma once
#include <mutex>
#include "EncFSy.h"
#include "EncFSFile.h"
#include "EncFSVolume.h"

/**
 * @file EncFSy_globals.h
 * @brief Global state shared across EncFSy modules
 * 
 * These globals are defined once in EncFSy.cpp and used throughout the application.
 * Thread-safety is provided by dirMoveLock for directory operations.
 */

/**
 * @brief Global EncFS volume instance (handles encryption/decryption operations)
 * 
 * Contains volume configuration, encryption keys, and cryptographic state.
 * Initialized when StartEncFS loads the .encfs6.xml configuration.
 */
extern EncFS::EncFSVolume encfs;

/**
 * @brief Global EncFS options (mount point, root directory, Dokan settings)
 * 
 * Populated from command-line arguments and used throughout the filesystem operations.
 */
extern EncFSOptions g_efo;

/**
 * @brief Mutex for protecting directory move/rename operations
 * 
 * Used to serialize directory moves when chainedNameIV or externalIVChaining is enabled,
 * preventing race conditions during recursive IV updates.
 * Also protects Cleanup/CloseFile operations to prevent double-free.
 */
extern std::mutex dirMoveLock;
