#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <algorithm>

// 🔊 AUDIO OUTPUT
#include <portaudio.h>

// 🚀 ZSTD DECOMPRESSION
#include <zstd.h>

// 🎮 PLAYBACK STATE
std::atomic<bool> is_playing{false};
std::atomic<bool> should_stop{false};
std::atomic<int64_t> current_sample{0};

// 🔥 HMICAP HEADER STRUCTURE
struct HMICAPHeader {
    char magic[8];          // "HMICAP01"
    uint32_t sample_rate;   // Hz
    uint16_t channels;      // 1=mono, 2=stereo
    uint16_t reserved1;
    uint64_t total_samples; // Samples per channel
    uint8_t reserved2[12];
};

// 🎧 AUDIO DATA - PRE-RENDERED AND READY TO BLAST!!
struct AudioData {
    int sample_rate;
    int channels;
    int64_t total_samples;
    std::vector<float> interleaved_data; // ALREADY interleaved = zero overhead!!
};

// 📂 LOAD HMICAP FILE (INSTANT LOADING - NO PARSING!!)
bool load_hmicap(const std::string& path, AudioData& audio) {
    std::cout << "📂 Loading HMICAP file...\n";
    
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Failed to open file\n";
        return false;
    }
    
    // Read header
    HMICAPHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // Verify magic number
    if (std::memcmp(header.magic, "HMICAP01", 8) != 0) {
        std::cerr << "❌ Invalid HMICAP file (bad magic number)\n";
        return false;
    }
    
    audio.sample_rate = header.sample_rate;
    audio.channels = header.channels;
    audio.total_samples = header.total_samples;
    
    std::cout << "  ✅ Valid HMICAP header detected! 💚\n";
    std::cout << "  🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "  🎧 Channels: " << audio.channels << "\n";
    std::cout << "  📊 Total samples: " << audio.total_samples << " per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    
    // Read interleaved sample data (INSTANT - just raw binary read!!)
    size_t total_floats = audio.total_samples * audio.channels;
    audio.interleaved_data.resize(total_floats);
    
    std::cout << "  📊 Reading " << total_floats * sizeof(float) / 1024.0 / 1024.0 << " MB of audio data...\n";
    
    file.read(reinterpret_cast<char*>(audio.interleaved_data.data()),
              total_floats * sizeof(float));
    
    if (!file) {
        std::cerr << "❌ Failed to read audio data\n";
        return false;
    }
    
    file.close();
    
    std::cout << "  ✅ HMICAP loaded INSTANTLY (no parsing needed fr fr) 🚀\n";
    
    return true;
}

// 🌀 LOAD HMICAP7 FILE (COMPRESSED)
bool load_hmicap7(const std::string& path, AudioData& audio) {
    std::cout << "📂 Loading HMICAP7 file (compressed)...\n";
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "❌ Failed to open file\n";
        return false;
    }
    
    std::streamsize compressed_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> compressed_data(compressed_size);
    if (!file.read(compressed_data.data(), compressed_size)) {
        std::cerr << "❌ Failed to read compressed file\n";
        return false;
    }
    file.close();
    
    std::cout << "  📦 Compressed size: " << compressed_size / 1024.0 / 1024.0 << " MB\n";
    
    // Get decompressed size
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(),
                                                                     compressed_size);
    
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        std::cerr << "❌ Not a valid Zstd file\n";
        return false;
    }
    
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        std::cerr << "❌ Decompressed size unknown\n";
        return false;
    }
    
    std::cout << "  🌀 Decompressing " << decompressed_size / 1024.0 / 1024.0 << " MB...\n";
    
    std::vector<char> decompressed_data(decompressed_size);
    size_t actual_size = ZSTD_decompress(decompressed_data.data(), decompressed_size,
                                         compressed_data.data(), compressed_size);
    
    if (ZSTD_isError(actual_size)) {
        std::cerr << "❌ Decompression error: " << ZSTD_getErrorName(actual_size) << "\n";
        return false;
    }
    
    std::cout << "  ✅ Decompressed successfully! 💚\n";
    
    // Parse header
    HMICAPHeader header;
    std::memcpy(&header, decompressed_data.data(), sizeof(header));
    
    if (std::memcmp(header.magic, "HMICAP01", 8) != 0) {
        std::cerr << "❌ Invalid HMICAP data in compressed file\n";
        return false;
    }
    
    audio.sample_rate = header.sample_rate;
    audio.channels = header.channels;
    audio.total_samples = header.total_samples;
    
    std::cout << "  ✅ Valid HMICAP header! 💚\n";
    std::cout << "  🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "  🎧 Channels: " << audio.channels << "\n";
    std::cout << "  📊 Total samples: " << audio.total_samples << " per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    
    // Copy sample data
    size_t total_floats = audio.total_samples * audio.channels;
    audio.interleaved_data.resize(total_floats);
    
    std::memcpy(audio.interleaved_data.data(),
                decompressed_data.data() + sizeof(header),
                total_floats * sizeof(float));
    
    std::cout << "  ✅ HMICAP7 loaded and ready to play! 🚀\n";
    
    return true;
}

// 🔊 PORTAUDIO CALLBACK (ZERO OVERHEAD - DIRECT MEMORY ACCESS!!)
static int audio_callback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    AudioData* audio = (AudioData*)userData;
    float* out = (float*)outputBuffer;
    
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        if (current_sample >= audio->total_samples || should_stop) {
            // Fill with silence
            for (int ch = 0; ch < audio->channels; ch++) {
                *out++ = 0.0f;
            }
            if (current_sample >= audio->total_samples) {
                return paComplete;
            }
        } else {
            // Direct copy from interleaved buffer (MAXIMUM SPEED!!)
            int64_t base_idx = current_sample * audio->channels;
            for (int ch = 0; ch < audio->channels; ch++) {
                *out++ = audio->interleaved_data[base_idx + ch];
            }
            current_sample++;
        }
    }
    
    return paContinue;
}

// 🎮 PLAY AUDIO (THE MAIN EVENT!!)
void play_audio(AudioData& audio) {
    PaError err;
    PaStream* stream;
    
    std::cout << "\n🔊 Initializing PortAudio...\n";
    
    err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "❌ PortAudio init failed: " << Pa_GetErrorText(err) << "\n";
        return;
    }
    
    // Open audio stream
    err = Pa_OpenDefaultStream(&stream,
                              0,                    // no input
                              audio.channels,       // output channels
                              paFloat32,           // 32-bit float
                              audio.sample_rate,   // sample rate
                              256,                 // frames per buffer
                              audio_callback,      // callback
                              &audio);             // user data
    
    if (err != paNoError) {
        std::cerr << "❌ Failed to open stream: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return;
    }
    
    std::cout << "✅ Audio stream opened!\n";
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
    
    // Wait for stop
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
    std::cout << "🔥🔥🔥 HMICAP PLAYER - INSTANT LOADING SUPREMACY 🔥🔥🔥\n";
    std::cout << "💎 SUPPORTS: HMICAP (binary) & HMICAP7 (compressed) 💎\n";
    std::cout << "⚡ ZERO PARSING OVERHEAD = MAXIMUM SPEED = UNDEFEATED ⚡\n\n";
    
    // Get file path
    std::string file_path;
    std::cout << "Enter HMICAP/HMICAP7 file path: ";
    std::getline(std::cin, file_path);
    
    // Detect format
    std::string ext;
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = file_path.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    AudioData audio;
    bool loaded = false;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (ext == "hmicap") {
        loaded = load_hmicap(file_path, audio);
    } else if (ext == "hmicap7") {
        loaded = load_hmicap7(file_path, audio);
    } else {
        std::cerr << "❌ Unknown format! Use .hmicap or .hmicap7\n";
        return 1;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (!loaded) {
        std::cerr << "❌ Failed to load audio file\n";
        return 1;
    }
    
    std::cout << "\n⚡ Loading time: " << duration.count() << " ms (INSTANT fr fr) 💯\n";
    
    // Validate audio
    std::cout << "\n🔍 Validating audio data...\n";
    bool has_audio = false;
    for (size_t i = 0; i < std::min((size_t)1000, audio.interleaved_data.size()); i++) {
        if (audio.interleaved_data[i] != 0.0f) {
            has_audio = true;
            break;
        }
    }
    
    if (!has_audio) {
        std::cout << "⚠️  Warning: First samples are all zero (might be silence)\n";
    } else {
        std::cout << "✅ Audio data validated! 💚\n";
    }
    
    // Play the audio
    play_audio(audio);
    
    std::cout << "\n💥 HMICAP PLAYER SESSION COMPLETE 💥\n";
    std::cout << "🚀 PRE-RENDERED FORMAT = INSTANT LOADING = BLESSED 🚀\n";
    
    return 0;
}