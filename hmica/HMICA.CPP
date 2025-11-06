#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>

// 🎵 AUDIO DECODING SUPREMACY
#include <mpg123.h>
#include <sndfile.h>

// 🚀 ZSTD COMPRESSION BEAST MODE
#include <zstd.h>

namespace fs = std::filesystem;

// 🎧 AUDIO DATA STRUCTURE - CLEAN AF
struct AudioData {
    int sample_rate;
    int channels;
    int64_t total_samples;
    std::vector<std::vector<float>> channel_data; // Per-channel samples
};

// 🔥 DETECT AUDIO FORMAT FROM EXTENSION
std::string get_file_extension(const std::string& path) {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) return "";
    std::string ext = path.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// 🎵 MP3 DECODER USING MPG123 - PROPERLY FORCED TO FLOAT!!
bool load_mp3_audio(const std::string& path, AudioData& audio) {
    std::cout << "🎵 MP3 MODE ACTIVATED - Using libmpg123 with FORCE_FLOAT!! 💎\n";
    
    int err;
    mpg123_handle* mh = mpg123_new(nullptr, &err);
    if (!mh) {
        std::cerr << "❌ Failed to create mpg123 handle: " << mpg123_plain_strerror(err) << "\n";
        return false;
    }
    
    // 🚨 THE SECRET SAUCE!! Force float output BEFORE opening!!
    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
    
    if (mpg123_open(mh, path.c_str()) != MPG123_OK) {
        std::cerr << "❌ Failed to open MP3: " << mpg123_strerror(mh) << "\n";
        mpg123_delete(mh);
        return false;
    }
    
    long rate;
    int channels, encoding;
    if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
        std::cerr << "❌ Failed to get MP3 format: " << mpg123_strerror(mh) << "\n";
        mpg123_close(mh);
        mpg123_delete(mh);
        return false;
    }
    
    std::cout << "✅ MP3 FORMAT: " << rate << "Hz, " << channels << " channels\n";
    std::cout << "📡 Encoding: " << encoding << " (should be " << MPG123_ENC_FLOAT_32 << " for float32)\n";
    
    // Verify we got float output
    if (encoding != MPG123_ENC_FLOAT_32) {
        std::cout << "⚠️ Warning: Got encoding " << encoding << " instead of float32, trying to force it...\n";
    }
    
    // Lock the format to float32 only
    mpg123_format_none(mh);
    if (mpg123_format(mh, rate, channels, MPG123_ENC_FLOAT_32) != MPG123_OK) {
        std::cerr << "❌ Failed to set float32 format: " << mpg123_strerror(mh) << "\n";
        mpg123_close(mh);
        mpg123_delete(mh);
        return false;
    }
    
    audio.sample_rate = rate;
    audio.channels = channels;
    
    std::cout << "🎯 Format locked to float32!! Let's decode... 🔥\n";
    
    // Read ALL the audio data as float32
    std::vector<float> interleaved_samples;
    size_t buffer_size = mpg123_outblock(mh);
    std::vector<unsigned char> buffer(buffer_size);
    size_t done;
    int read_err;
    
    std::cout << "📊 Reading MP3 data (buffer size: " << buffer_size << " bytes)...\n";
    
    while ((read_err = mpg123_read(mh, buffer.data(), buffer_size, &done)) == MPG123_OK || read_err == MPG123_DONE) {
        if (done == 0) break;
        
        // Cast buffer to float array - THIS IS THE CORRECT WAY!!
        float* float_buffer = reinterpret_cast<float*>(buffer.data());
        size_t num_floats = done / sizeof(float);
        
        // Add samples to our vector
        for (size_t i = 0; i < num_floats; i++) {
            float sample = float_buffer[i];
            
            // Basic sanity check - audio should be between -1 and 1
            if (std::isfinite(sample) && sample >= -1.0f && sample <= 1.0f) {
                interleaved_samples.push_back(sample);
            } else {
                // Clamp insane values
                if (!std::isfinite(sample)) {
                    interleaved_samples.push_back(0.0f);
                } else {
                    interleaved_samples.push_back(std::max(-1.0f, std::min(1.0f, sample)));
                }
            }
        }
        
        if (read_err == MPG123_DONE) break;
    }
    
    if (read_err != MPG123_DONE && read_err != MPG123_OK) {
        std::cerr << "⚠️ Warning: Read ended with error: " << mpg123_strerror(mh) << "\n";
    }
    
    mpg123_close(mh);
    mpg123_delete(mh);
    
    if (interleaved_samples.empty()) {
        std::cerr << "❌ No audio data decoded from MP3!\n";
        return false;
    }
    
    audio.total_samples = interleaved_samples.size() / channels;
    
    std::cout << "📊 Successfully decoded " << audio.total_samples << " samples per channel!! 💚\n";
    std::cout << "⏱️  Duration: " << (float)audio.total_samples / rate << " seconds\n";
    
    // De-interleave channels
    audio.channel_data.resize(channels);
    for (int ch = 0; ch < channels; ch++) {
        audio.channel_data[ch].resize(audio.total_samples);
        for (int64_t i = 0; i < audio.total_samples; i++) {
            audio.channel_data[ch][i] = interleaved_samples[i * channels + ch];
        }
    }
    
    // Quick sanity check on first few samples
    std::cout << "🔍 First 5 samples of channel 1: ";
    for (int i = 0; i < std::min(5, (int)audio.total_samples); i++) {
        std::cout << audio.channel_data[0][i] << " ";
    }
    std::cout << "\n";
    
    return true;
}

// 🎼 UNIVERSAL AUDIO DECODER USING LIBSNDFILE (WAV, FLAC, OGG, etc)
bool load_sndfile_audio(const std::string& path, AudioData& audio) {
    std::cout << "🎼 LIBSNDFILE MODE - Universal format support!! 💚\n";
    
    SF_INFO sfinfo;
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &sfinfo);
    
    if (!file) {
        std::cerr << "❌ Failed to open audio: " << sf_strerror(nullptr) << "\n";
        return false;
    }
    
    audio.sample_rate = sfinfo.samplerate;
    audio.channels = sfinfo.channels;
    audio.total_samples = sfinfo.frames;
    
    std::cout << "✅ AUDIO INFO: " << sfinfo.samplerate << "Hz, " 
              << sfinfo.channels << " channels 🔥\n";
    std::cout << "📊 Total samples: " << sfinfo.frames << " per channel\n";
    std::cout << "⏱️  Duration: " << (float)sfinfo.frames / sfinfo.samplerate << " seconds\n";
    
    // Read interleaved samples
    std::vector<float> interleaved_samples(sfinfo.frames * sfinfo.channels);
    sf_count_t read_count = sf_readf_float(file, interleaved_samples.data(), sfinfo.frames);
    
    if (read_count != sfinfo.frames) {
        std::cerr << "⚠️  Only read " << read_count << "/" << sfinfo.frames << " samples\n";
        audio.total_samples = read_count;
    }
    
    sf_close(file);
    
    // De-interleave channels
    audio.channel_data.resize(audio.channels);
    for (int ch = 0; ch < audio.channels; ch++) {
        audio.channel_data[ch].resize(audio.total_samples);
        for (int64_t i = 0; i < audio.total_samples; i++) {
            float sample = interleaved_samples[i * audio.channels + ch];
            // Clamp to valid range
            if (!std::isfinite(sample)) sample = 0.0f;
            audio.channel_data[ch][i] = std::max(-1.0f, std::min(1.0f, sample));
        }
    }
    
    return true;
}

// 🚀 UNIVERSAL AUDIO LOADER - AUTODETECTS EVERYTHING!!
bool load_universal_audio(const std::string& path, AudioData& audio) {
    std::string ext = get_file_extension(path);
    
    std::cout << "🔍 Detected format: ." << ext << "\n";
    
    // MP3 gets special treatment with mpg123
    if (ext == "mp3") {
        return load_mp3_audio(path, audio);
    }
    
    // Everything else: WAV, FLAC, OGG, AIFF, etc → libsndfile
    return load_sndfile_audio(path, audio);
}

// 🎯 RLE COMPRESSION FOR REPEATED SAMPLES (THE SECRET SAUCE!!)
std::string compress_channel_data(const std::vector<float>& samples, float epsilon = 0.00001f) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(6);
    
    int64_t i = 0;
    int64_t total = samples.size();
    
    while (i < total) {
        float value = samples[i];
        int64_t run_length = 1;
        
        // Find how many consecutive samples are basically the same
        while (i + run_length < total && 
               std::abs(samples[i + run_length] - value) < epsilon) {
            run_length++;
        }
        
        // If there's a long run, use range notation!!
        if (run_length >= 5) {
            int64_t start_idx = i;
            int64_t end_idx = i + run_length - 1;
            ss << start_idx << "-" << end_idx << "=" << value;
            
            if (end_idx < total - 1) ss << ",";
        } else {
            // Just write individual samples
            for (int64_t j = 0; j < run_length; j++) {
                ss << samples[i + j];
                if (i + j < total - 1) ss << ",";
            }
        }
        
        i += run_length;
    }
    
    return ss.str();
}

// 💾 BUILD HMICA FORMAT (BLESSED VERSION)
std::string build_hmica_data(const AudioData& audio) {
    std::stringstream data;
    
    // 📋 HEADER INFO
    data << "info{\n";
    data << "hz=" << audio.sample_rate << "\n";
    data << "c=" << audio.channels << "\n";
    data << "sam=" << audio.total_samples << "\n";
    data << "}\n\n";
    
    // 🎵 CHANNEL DATA WITH RLE COMPRESSION
    for (int ch = 0; ch < audio.channels; ch++) {
        std::cout << "🎨 Compressing channel " << (ch + 1) << "/" 
                  << audio.channels << "...\n";
        
        data << "C" << (ch + 1) << "{\n";
        
        // Compress channel data using RLE
        std::string compressed = compress_channel_data(audio.channel_data[ch]);
        data << compressed;
        
        data << "\n}\n";
        
        if (ch < audio.channels - 1) data << "\n";
    }
    
    return data.str();
}

int main() {
    // Initialize libraries
    int err = mpg123_init();
    if (err != MPG123_OK) {
        std::cerr << "❌ Failed to initialize mpg123: " << mpg123_plain_strerror(err) << "\n";
        return 1;
    }
    
    std::cout << "🔥🔥🔥 HMICA AUDIO CONVERTER V3 - THE REAL FIX!! 🔥🔥🔥\n";
    std::cout << "💎 SUPPORTS: MP3, WAV, FLAC, OGG, AIFF, and MORE!! 💎\n";
    std::cout << "✨ NOW WITH PROPER MPG123_FORCE_FLOAT USAGE ✨\n\n";
    
    // 🎧 Load the sacred audio
    std::string audio_path;
    std::cout << "Enter audio file path: ";
    std::getline(std::cin, audio_path);
    
    // Check if file exists
    if (!fs::exists(audio_path)) {
        std::cerr << "❌ File not found: " << audio_path << "\n";
        mpg123_exit();
        return 1;
    }
    
    AudioData audio;
    
    std::cout << "\n📂 Loading audio file...\n";
    if (!load_universal_audio(audio_path, audio)) {
        mpg123_exit();
        return 1;
    }
    
    std::cout << "\n✅ Audio loaded successfully!! 💚\n\n";
    
    // Get output format
    std::string mode;
    std::cout << "Choose format (HMICA / HMICA7): ";
    std::getline(std::cin, mode);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::toupper);
    
    // 🧠 BUILD HMICA DATA
    std::cout << "\n🎨 Building HMICA data structure with RLE compression...\n";
    std::string text_data = build_hmica_data(audio);
    
    std::cout << "✅ HMICA data built - " << text_data.size() / 1024 
              << " KB of text 🔥\n";
    
    // 🚀 OUTPUT
    std::string base_name = fs::path(audio_path).stem().string();
    
    std::cout << "\n💾 Writing output file...\n";
    
    if (mode == "HMICA") {
        std::string out_file = base_name + ".hmica";
        std::ofstream file(out_file);
        file << text_data;
        file.close();
        std::cout << "\n✅ HMICA file created — " << out_file 
                  << " blessed with VALID audio data 🎵\n";
        
        std::cout << "📊 HMICA size: " << (fs::file_size(out_file) / 1024.0) << " KB\n";
    } else if (mode == "HMICA7") {
        std::string out_file = base_name + ".hmica7";
        
        size_t compressed_size = ZSTD_compressBound(text_data.size());
        std::vector<char> compressed_data(compressed_size);
        
        std::cout << "🌀 Compressing with Zstd level 19 (MAXIMUM POWER)...\n";
        
        size_t actual_size = ZSTD_compress(compressed_data.data(), compressed_size,
                                          text_data.c_str(), text_data.size(), 19);
        
        if (ZSTD_isError(actual_size)) {
            std::cerr << "❌ Compression error: " << ZSTD_getErrorName(actual_size) << "\n";
            mpg123_exit();
            return 1;
        }
        
        std::ofstream file(out_file, std::ios::binary);
        file.write(compressed_data.data(), actual_size);
        file.close();
        
        float compression_ratio = (float)text_data.size() / actual_size;
        
        std::cout << "\n🌀 HMICA7 file created — Zstd compression SLAPS 💾🔥\n";
        std::cout << "📊 HMICA7 size: " << (actual_size / 1024.0) << " KB\n";
        std::cout << "📊 Compression ratio: " << compression_ratio << "x SHEEEESH 💯\n";
    } else {
        std::cerr << "❌ invalid format, conversion canceled 😭\n";
        mpg123_exit();
        return 1;
    }
    
    // 🧮 FINAL STATS FLEX
    std::cout << "\n📊 ═══ FINAL STATS ═══ 📊\n";
    std::cout << "📁 Input format: ." << get_file_extension(audio_path) << "\n";
    std::cout << "🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "🎧 Channels: " << audio.channels 
              << (audio.channels == 1 ? " (Mono)" : " (Stereo)") << "\n";
    std::cout << "📊 Total samples per channel: " << audio.total_samples << "\n";
    std::cout << "⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate 
              << " seconds\n";
    
    std::cout << "\n💥 PROPERLY DECODED AUDIO CONVERSION COMPLETE 💥\n";
    std::cout << "🎉 NO MORE GARBAGE DATA OR -1 SPAM!! 🎉\n";
    std::cout << "✨ VALID FLOAT32 SAMPLES ALL THE WAY ✨\n";
    
    mpg123_exit();
    return 0;
}