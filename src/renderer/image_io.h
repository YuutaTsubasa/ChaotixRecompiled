#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

// Writes an XRGB8888 image as an uncompressed (stored-deflate) PNG.
bool write_png(const std::string& path, const uint32_t* pixels, int width, int height, int stride_pixels);

// Writes interleaved stereo int16 samples as a WAV file.
bool write_wav(const std::string& path, const std::vector<int16_t>& stereo, int rate);

// FNV-1a hash of an image region, used by rendering regression tests.
uint64_t image_hash(const uint32_t* pixels, int width, int height, int stride_pixels);

} // namespace chaotix
