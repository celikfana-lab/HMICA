#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

// 🔊 AUDIO OUTPUT SUPREMACY
#include <portaudio.h>

// 🚀 ZSTD DECOMPRESSION BEAST MODE
#include <zstd.h>
#include <iomanip>


// 🎮 PLAYBACK STATE
std::atomic<bool> is_playing{false};
std::atomic<bool> should_stop{false};
std::atomic<int64_t> current_sample{0};

// 🎵 AUDIO DATA STRUCTURE
struct AudioData {
    int sample_rate;
    int channels;
    int64_t total_samples;
    std::vector<std::vector<float>> channel_data;
};

// 🔥 PARSE HMICA INFO BLOCK
bool parse_info_block(const std::string& content, AudioData& audio) {
    std::cout << "📋 Parsing info block...\n";
    
    size_t info_start = content.find("info{");
    size_t info_end = content.find("}", info_start);
    
    if (info_start == std::string::npos || info_end == std::string::npos) {
        std::cerr << "❌ No info block found!\n";
        return false;
    }
    
    std::string info_content = content.substr(info_start + 5, info_end - info_start - 5);
    std::istringstream info_stream(info_content);
    std::string line;
    
    while (std::getline(info_stream, line)) {
        // Remove whitespace
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
        if (line.empty()) continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        if (key == "hz") {
            audio.sample_rate = std::stoi(value);
            std::cout << "  🎵 Sample rate: " << audio.sample_rate << " Hz\n";
        } else if (key == "c") {
            audio.channels = std::stoi(value);
            std::cout << "  🎧 Channels: " << audio.channels << "\n";
        } else if (key == "sam") {
            audio.total_samples = std::stoll(value);
            std::cout << "  📊 Total samples: " << audio.total_samples << "\n";
        }
    }
    
    if (audio.sample_rate == 0 || audio.channels == 0 || audio.total_samples == 0) {
        std::cerr << "❌ Invalid audio parameters in info block!\n";
        return false;
    }
    
    // Initialize channel data
    audio.channel_data.resize(audio.channels);
    for (int ch = 0; ch < audio.channels; ch++) {
        audio.channel_data[ch].resize(audio.total_samples, 0.0f);
    }
    
    return true;
}

// 🎯 PARSE CHANNEL DATA WITH RLE SUPPORT (THE BIG BRAIN STUFF!!)
bool parse_channel_block(const std::string& content, int channel_idx, AudioData& audio) {
    std::cout << "🎨 Parsing channel " << channel_idx << "...\n";
    
    std::string search_tag = "C" + std::to_string(channel_idx) + "{";
    size_t ch_start = content.find(search_tag);
    
    if (ch_start == std::string::npos) {
        std::cerr << "❌ Channel " << channel_idx << " not found!\n";
        return false;
    }
    
    size_t ch_end = content.find("}", ch_start);
    if (ch_end == std::string::npos) {
        std::cerr << "❌ Channel " << channel_idx << " block not closed!\n";
        return false;
    }
    
    std::string channel_content = content.substr(ch_start + search_tag.length(), 
                                                 ch_end - ch_start - search_tag.length());
    
    // Parse samples and RLE ranges
    std::istringstream stream(channel_content);
    std::string token;
    int64_t sample_idx = 0;
    
    while (std::getline(stream, token, ',')) {
        // Remove whitespace
        token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (token.empty()) continue;
        
        // Check if this is RLE format: "start-end=value"
        size_t dash_pos = token.find('-');
        size_t eq_pos = token.find('=');
        
        if (dash_pos != std::string::npos && eq_pos != std::string::npos) {
            // RLE FORMAT DETECTED!! 🔥
            int64_t start_idx = std::stoll(token.substr(0, dash_pos));
            int64_t end_idx = std::stoll(token.substr(dash_pos + 1, eq_pos - dash_pos - 1));
            float value = std::stof(token.substr(eq_pos + 1));
            
            // Fill range with value
            for (int64_t i = start_idx; i <= end_idx && i < audio.total_samples; i++) {
                audio.channel_data[channel_idx - 1][i] = value;
            }
            
            sample_idx = end_idx + 1;
        } else {
            // Regular sample value
            float value = std::stof(token);
            if (sample_idx < audio.total_samples) {
                audio.channel_data[channel_idx - 1][sample_idx] = value;
            }
            sample_idx++;
        }
    }
    
    std::cout << "  ✅ Loaded " << sample_idx << " samples for channel " << channel_idx << "\n";
    return true;
}

// 🚀 LOAD HMICA FILE (UNCOMPRESSED)
bool load_hmica(const std::string& path, AudioData& audio) {
    std::cout << "📂 Loading HMICA file...\n";
    
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "❌ Failed to open file: " << path << "\n";
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    std::cout << "  📄 File size: " << content.size() / 1024 << " KB\n";
    
    // Parse info block
    if (!parse_info_block(content, audio)) {
        return false;
    }
    
    // Parse all channel blocks
    for (int ch = 1; ch <= audio.channels; ch++) {
        if (!parse_channel_block(content, ch, audio)) {
            return false;
        }
    }
    
    std::cout << "✅ HMICA loaded successfully!! 💚\n";
    return true;
}

// 🌀 LOAD HMICA7 FILE (ZSTD COMPRESSED)
bool load_hmica7(const std::string& path, AudioData& audio) {
    std::cout << "📂 Loading HMICA7 file (compressed)...\n";
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "❌ Failed to open file: " << path << "\n";
        return false;
    }
    
    std::streamsize compressed_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> compressed_data(compressed_size);
    if (!file.read(compressed_data.data(), compressed_size)) {
        std::cerr << "❌ Failed to read compressed file!\n";
        return false;
    }
    file.close();
    
    std::cout << "  📦 Compressed size: " << compressed_size / 1024 << " KB\n";
    
    // Decompress with Zstd
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), 
                                                                     compressed_size);
    
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        std::cerr << "❌ Not a valid Zstd compressed file!\n";
        return false;
    }
    
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        std::cerr << "❌ Decompressed size unknown!\n";
        return false;
    }
    
    std::cout << "  🌀 Decompressing " << decompressed_size / 1024 << " KB...\n";
    
    std::vector<char> decompressed_data(decompressed_size);
    size_t actual_decompressed = ZSTD_decompress(decompressed_data.data(), decompressed_size,
                                                  compressed_data.data(), compressed_size);
    
    if (ZSTD_isError(actual_decompressed)) {
        std::cerr << "❌ Decompression error: " << ZSTD_getErrorName(actual_decompressed) << "\n";
        return false;
    }
    
    std::cout << "  ✅ Decompressed successfully! 🔥\n";
    
    std::string content(decompressed_data.begin(), decompressed_data.end());
    
    // Parse info block
    if (!parse_info_block(content, audio)) {
        return false;
    }
    
    // Parse all channel blocks
    for (int ch = 1; ch <= audio.channels; ch++) {
        if (!parse_channel_block(content, ch, audio)) {
            return false;
        }
    }
    
    std::cout << "✅ HMICA7 loaded successfully!! 💚\n";
    return true;
}

// 🔊 PORTAUDIO CALLBACK (WHERE THE MAGIC HAPPENS!!)
static int audio_callback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    AudioData* audio = (AudioData*)userData;
    float* out = (float*)outputBuffer;
    
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        if (current_sample >= audio->total_samples || should_stop) {
            // Fill with silence if done
            for (int ch = 0; ch < audio->channels; ch++) {
                *out++ = 0.0f;
            }
            if (current_sample >= audio->total_samples) {
                return paComplete;
            }
        } else {
            // Output samples from all channels
            for (int ch = 0; ch < audio->channels; ch++) {
                *out++ = audio->channel_data[ch][current_sample];
            }
            current_sample++;
        }
    }
    
    return paContinue;
}

// 🎮 PLAYBACK CONTROLS
void play_audio(AudioData& audio) {
    PaError err;
    PaStream* stream;
    
    std::cout << "\n🔊 Initializing PortAudio...\n";
    
    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "❌ PortAudio init failed: " << Pa_GetErrorText(err) << "\n";
        return;
    }
    
    // Open stream
    err = Pa_OpenDefaultStream(&stream,
                              0,                    // no input
                              audio.channels,       // output channels
                              paFloat32,           // 32-bit float
                              audio.sample_rate,   // sample rate
                              256,                 // frames per buffer
                              audio_callback,      // callback function
                              &audio);             // user data
    
    if (err != paNoError) {
        std::cerr << "❌ Failed to open stream: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return;
    }
    
    std::cout << "✅ Audio stream opened successfully!\n";
    std::cout << "\n🎵 ═══ NOW PLAYING ═══ 🎵\n";
    std::cout << "⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    std::cout << "🎧 Channels: " << audio.channels << (audio.channels == 2 ? " (Stereo)" : " (Mono)") << "\n";
    std::cout << "🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "\n💡 Press ENTER to stop playback...\n\n";
    
    // Start playback
    current_sample = 0;
    should_stop = false;
    
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "❌ Failed to start stream: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return;
    }
    
    is_playing = true;
    
    // Progress display thread
    std::thread progress_thread([&audio]() {
        while (is_playing && current_sample < audio.total_samples && !should_stop) {
            float progress = (float)current_sample / audio.total_samples * 100.0f;
            float time_elapsed = (float)current_sample / audio.sample_rate;
            float total_time = (float)audio.total_samples / audio.sample_rate;
            
            std::cout << "\r🎵 Playing... " << std::fixed << std::setprecision(1) 
                      << progress << "% | "
                      << time_elapsed << "s / " << total_time << "s        " << std::flush;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    // Wait for user to stop or audio to finish
    std::string input;
    std::getline(std::cin, input);
    should_stop = true;
    
    // Stop stream
    err = Pa_StopStream(stream);
    if (err != paNoError) {
        std::cerr << "\n⚠️  Error stopping stream: " << Pa_GetErrorText(err) << "\n";
    }
    
    is_playing = false;
    progress_thread.join();
    
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    std::cout << "\n\n✅ Playback stopped! 🎵\n";
}

int main() {
    std::cout << "🔥🔥🔥 HMICA AUDIO PLAYER - LEGENDARY EDITION 🔥🔥🔥\n";
    std::cout << "💎 SUPPORTS: HMICA (uncompressed) & HMICA7 (Zstd compressed) 💎\n";
    std::cout << "🔊 Powered by PortAudio (UNDEFEATED) 🔊\n\n";
    
    // Get file path
    std::string audio_path;
    std::cout << "Enter HMICA/HMICA7 file path: ";
    std::getline(std::cin, audio_path);
    
    // Detect format from extension
    std::string ext;
    size_t dot_pos = audio_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = audio_path.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    AudioData audio;
    audio.sample_rate = 0;
    audio.channels = 0;
    audio.total_samples = 0;
    
    bool loaded = false;
    
    if (ext == "hmica") {
        loaded = load_hmica(audio_path, audio);
    } else if (ext == "hmica7") {
        loaded = load_hmica7(audio_path, audio);
    } else {
        std::cerr << "❌ Unknown format! Use .hmica or .hmica7\n";
        return 1;
    }
    
    if (!loaded) {
        std::cerr << "❌ Failed to load audio file!\n";
        return 1;
    }
    
    // Validate audio data
    std::cout << "\n🔍 Validating audio data...\n";
    int64_t non_zero_samples = 0;
    for (int ch = 0; ch < audio.channels; ch++) {
        for (int64_t i = 0; i < audio.total_samples; i++) {
            if (audio.channel_data[ch][i] != 0.0f) {
                non_zero_samples++;
                break;
            }
        }
    }
    
    if (non_zero_samples == 0) {
        std::cout << "⚠️  Warning: All audio data is zero (silence)!\n";
    } else {
        std::cout << "✅ Audio data validated - contains actual samples! 🎵\n";
    }
    
    // Play the audio
    play_audio(audio);
    
    std::cout << "\n💥 HMICA PLAYER SESSION COMPLETE 💥\n";
    std::cout << "🎉 THANKS FOR USING YOUR CUSTOM FORMAT!! 🎉\n";
    
    return 0;
}