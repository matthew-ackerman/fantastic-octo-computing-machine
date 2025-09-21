// FNV-1a 64-bit hash helper for files
#pragma once

#include <string>

// Computes FNV-1a 64-bit hash of the file at path and returns
// a lowercase hex string (16 chars). Returns empty string on error.
std::string hash_file_fnv1a64(const char* path);

