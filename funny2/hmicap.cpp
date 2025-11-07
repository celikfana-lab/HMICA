#include <cmath>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include <algorithm>

// 🎵 AUDIO DECODING
#include <mpg123.h>
#include <sndfile.h>

// 🚀 ZSTD COMPRESSION
#include <zstd.h>

namespace fs = std::filesystem;

// 🎧 AUDIO DATA STRUCTURE
struct AudioData {
    int sample_rate;
    int channels;
    int64_t total_samples;
    std::vector<int32_t> interleaved_data; // INT32 SUPREMACY!! 32-bit signed integers fr fr
};

// 🔥 HMICAP HEADER STRUCTURE (32 bytes total - cache line optimized fr fr)
struct HMICAPHeader {
    char magic[8];          // "HMICAP01"
    uint32_t sample_rate;   // Hz
    uint16_t channels;      // 1=mono, 2=stereo, etc
    uint16_t bit_depth;     // 32 for int32
    uint64_t total_samples; // Samples per channel
    uint8_t reserved2[12];  // Future metadata
};

// 🔥 FLOAT TO INT32 CONVERSION (MAXIMUM QUALITY NO CAP)
inline int32_t float_to_int32(float sample) {
    // Clamp to [-1.0, 1.0] first
    sample = std::max(-1.0f, std::min(1.0f, sample));
    // Scale to int32 range (preserving full 32-bit dynamic range)
    // Using 2147483647.0 instead of 2147483648.0 to avoid overflow on exactly 1.0
    return static_cast<int32_t>(sample * 2147483647.0f);
}

// 🎵 MP3 DECODER - STRAIGHT TO INTERLEAVED INT32 BABY!!
bool load_mp3_audio(const std::string& path, AudioData& audio) {
    std::cout << "🎵 Loading MP3 with mpg123...\n";
    
    int err;
    mpg123_handle* mh = mpg123_new(nullptr, &err);
    if (!mh) {
        std::cerr << "❌ Failed to create mpg123 handle\n";
        return false;
    }
    
    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
    
    if (mpg123_open(mh, path.c_str()) != MPG123_OK) {
        std::cerr << "❌ Failed to open MP3\n";
        mpg123_delete(mh);
        return false;
    }
    
    long rate;
    int channels, encoding;
    mpg123_getformat(mh, &rate, &channels, &encoding);
    
    mpg123_format_none(mh);
    mpg123_format(mh, rate, channels, MPG123_ENC_FLOAT_32);
    
    audio.sample_rate = rate;
    audio.channels = channels;
    
    std::cout << "  ✅ " << rate << "Hz, " << channels << " channels\n";
    
    // Read interleaved samples directly and convert to int32!!
    size_t buffer_size = mpg123_outblock(mh);
    std::vector<unsigned char> buffer(buffer_size);
    size_t done;
    int read_err;
    
    while ((read_err = mpg123_read(mh, buffer.data(), buffer_size, &done)) == MPG123_OK || read_err == MPG123_DONE) {
        if (done == 0) break;
        
        float* float_buffer = reinterpret_cast<float*>(buffer.data());
        size_t num_floats = done / sizeof(float);
        
        for (size_t i = 0; i < num_floats; i++) {
            float sample = float_buffer[i];
            if (!std::isfinite(sample)) sample = 0.0f;
            sample = std::max(-1.0f, std::min(1.0f, sample));
            audio.interleaved_data.push_back(float_to_int32(sample));
        }
        
        if (read_err == MPG123_DONE) break;
    }
    
    mpg123_close(mh);
    mpg123_delete(mh);
    
    audio.total_samples = audio.interleaved_data.size() / channels;
    
    std::cout << "  📊 Loaded " << audio.total_samples << " samples per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)audio.total_samples / rate << " seconds\n";
    
    return true;
}

// 🎼 LIBSNDFILE LOADER
bool load_sndfile_audio(const std::string& path, AudioData& audio) {
    std::cout << "🎼 Loading with libsndfile...\n";
    
    SF_INFO sfinfo;
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfinfo);
    
    if (!file) {
        std::cerr << "❌ Failed to open audio file\n";
        return false;
    }
    
    audio.sample_rate = sfinfo.samplerate;
    audio.channels = sfinfo.channels;
    audio.total_samples = sfinfo.frames;
    
    std::cout << "  ✅ " << sfinfo.samplerate << "Hz, " << sfinfo.channels << " channels\n";
    std::cout << "  📊 " << sfinfo.frames << " samples per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)sfinfo.frames / sfinfo.samplerate << " seconds\n";
    
    // Read as float first then convert to int32
    std::vector<float> float_data(sfinfo.frames * sfinfo.channels);
    sf_count_t read_count = sf_readf_float(file, float_data.data(), sfinfo.frames);
    
    if (read_count != sfinfo.frames) {
        std::cout << "  ⚠️  Only read " << read_count << "/" << sfinfo.frames << " samples\n";
        audio.total_samples = read_count;
        float_data.resize(read_count * audio.channels);
    }
    
    sf_close(file);
    
    // Convert to int32
    audio.interleaved_data.reserve(float_data.size());
    for (float sample : float_data) {
        if (!std::isfinite(sample)) sample = 0.0f;
        sample = std::max(-1.0f, std::min(1.0f, sample));
        audio.interleaved_data.push_back(float_to_int32(sample));
    }
    
    return true;
}

// 🚀 UNIVERSAL AUDIO LOADER
bool load_audio(const std::string& path, AudioData& audio) {
    std::string ext;
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = path.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    std::cout << "🔍 Detected format: ." << ext << "\n";
    
    if (ext == "mp3") {
        return load_mp3_audio(path, audio);
    }
    
    return load_sndfile_audio(path, audio);
}

// 💾 WRITE HMICAP FILE (UNCOMPRESSED BINARY - RAW SPEED!!)
bool write_hmicap(const std::string& path, const AudioData& audio) {
    std::cout << "\n💾 Writing HMICAP file (INT32 format)...\n";
    
    HMICAPHeader header;
    std::memset(&header, 0, sizeof(header));
    
    std::memcpy(header.magic, "HMICAP01", 8);
    header.sample_rate = audio.sample_rate;
    header.channels = audio.channels;
    header.bit_depth = 32; // INT32 BABY
    header.total_samples = audio.total_samples;
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Failed to create HMICAP file\n";
        return false;
    }
    
    // Write header
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // Write interleaved int32 sample data (ALREADY READY TO GO!!)
    file.write(reinterpret_cast<const char*>(audio.interleaved_data.data()),
               audio.interleaved_data.size() * sizeof(int32_t));
    
    file.close();
    
    size_t file_size = fs::file_size(path);
    std::cout << "  ✅ HMICAP written: " << file_size / 1024.0 / 1024.0 << " MB\n";
    std::cout << "  💎 32-bit integer format = MAXIMUM QUALITY 💎\n";
    
    return true;
}

// 🌀 WRITE HMICAP7 FILE (ZSTD COMPRESSED - MAXIMUM COMPRESSION!!)
bool write_hmicap7(const std::string& path, const AudioData& audio) {
    std::cout << "\n🌀 Writing HMICAP7 file (compressed INT32)...\n";
    
    // Build uncompressed HMICAP data in memory
    std::vector<char> uncompressed_data;
    uncompressed_data.resize(sizeof(HMICAPHeader) + audio.interleaved_data.size() * sizeof(int32_t));
    
    HMICAPHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, "HMICAP01", 8);
    header.sample_rate = audio.sample_rate;
    header.channels = audio.channels;
    header.bit_depth = 32; // INT32 SUPREMACY
    header.total_samples = audio.total_samples;
    
    // Copy header
    std::memcpy(uncompressed_data.data(), &header, sizeof(header));
    
    // Copy sample data
    std::memcpy(uncompressed_data.data() + sizeof(header),
                audio.interleaved_data.data(),
                audio.interleaved_data.size() * sizeof(int32_t));
    
    // Compress with Zstd level 19 (SHEEEESH)
    size_t compressed_bound = ZSTD_compressBound(uncompressed_data.size());
    std::vector<char> compressed_data(compressed_bound);
    
    std::cout << "  🔄 Compressing " << uncompressed_data.size() / 1024.0 / 1024.0 << " MB...\n";
    
    size_t compressed_size = ZSTD_compress(compressed_data.data(), compressed_bound,
                                           uncompressed_data.data(), uncompressed_data.size(), 19);
    
    if (ZSTD_isError(compressed_size)) {
        std::cerr << "❌ Compression failed: " << ZSTD_getErrorName(compressed_size) << "\n";
        return false;
    }
    
    // Write compressed data
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Failed to create HMICAP7 file\n";
        return false;
    }
    
    file.write(compressed_data.data(), compressed_size);
    file.close();
    
    float ratio = (float)uncompressed_data.size() / compressed_size;
    
    std::cout << "  ✅ HMICAP7 written: " << compressed_size / 1024.0 / 1024.0 << " MB\n";
    std::cout << "  📊 Compression ratio: " << ratio << "x 💯\n";
    std::cout << "  💎 INT32 format preserved = LOSSLESS 💎\n";
    
    return true;
}

int main() {
    // Initialize mpg123
    mpg123_init();
    
    std::cout << "🔥🔥🔥 HMICAP CONVERTER - INT32 EDITION 🔥🔥🔥\n";
    std::cout << "💎 SUPPORTS: MP3, WAV, FLAC, OGG, AIFF → HMICAP/HMICAP7 💎\n";
    std::cout << "⚡ INT32 FORMAT = MAXIMUM QUALITY + INSTANT LOADING ⚡\n\n";
    
    // Get input file
    std::string input_path;
    std::cout << "Enter audio file path: ";
    std::getline(std::cin, input_path);
    
    if (!fs::exists(input_path)) {
        std::cerr << "❌ File not found!\n";
        mpg123_exit();
        return 1;
    }
    
    // Load audio
    AudioData audio;
    std::cout << "\n📂 Loading audio...\n";
    
    if (!load_audio(input_path, audio)) {
        mpg123_exit();
        return 1;
    }
    
    std::cout << "\n✅ Audio loaded successfully!! 💚\n";
    
    // Get output format
    std::string format;
    std::cout << "\nChoose format (HMICAP / HMICAP7): ";
    std::getline(std::cin, format);
    std::transform(format.begin(), format.end(), format.begin(), ::toupper);
    
    std::string base_name = fs::path(input_path).stem().string();
    bool success = false;
    
    if (format == "HMICAP") {
        std::string output = base_name + ".hmicap";
        success = write_hmicap(output, audio);
    } else if (format == "HMICAP7") {
        std::string output = base_name + ".hmicap7";
        success = write_hmicap7(output, audio);
    } else {
        std::cerr << "❌ Invalid format!\n";
        mpg123_exit();
        return 1;
    }
    
    if (!success) {
        mpg123_exit();
        return 1;
    }
    
    // STATS FLEX 💪
    std::cout << "\n📊 ═══ CONVERSION COMPLETE ═══ 📊\n";
    std::cout << "🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "🎧 Channels: " << audio.channels << "\n";
    std::cout << "💎 Bit depth: 32-bit signed integer\n";
    std::cout << "📊 Total samples: " << audio.total_samples << " per channel\n";
    std::cout << "⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    std::cout << "💾 Format: " << format << "\n";
    
    std::cout << "\n💥 INT32 PRE-RENDERED AUDIO READY FOR INSTANT PLAYBACK 💥\n";
    std::cout << "🚀 MAXIMUM QUALITY + ZERO PARSING = LITERALLY UNDEFEATED 🚀\n";
    
    mpg123_exit();
    return 0;
}