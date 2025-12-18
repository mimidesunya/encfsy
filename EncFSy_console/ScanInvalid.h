#pragma once

#include "EncFSy.h"
#include <vector>
#include <string>

// Runs invalid filename scan. Returns EXIT_SUCCESS / EXIT_FAILURE.
int RunScanInvalid(const EncFSOptions& efo, char* password);
