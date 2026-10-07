#pragma once
#include <string>

// Minimal file storage on the SD card. All paths are absolute ("/coffeescale/...").
// Thread safe; mount() may take a moment when no card is present.
namespace storage {
bool mount();      // try to mount the SD card (no-op if mounted)
bool mounted();
void unmount();
bool check();      // still there? unmounts if the card was pulled
bool read(const char* path, std::string& out);
bool write(const char* path, const std::string& data);   // atomic: tmp + rename
bool remove(const char* path);
bool mkdirs(const char* path);
uint64_t freeBytes();
}  // namespace storage
