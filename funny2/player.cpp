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
#include <cmath>
#include <random>

// 🔊 AUDIO OUTPUT
#include <portaudio.h>

// 🚀 ZSTD DECOMPRESSION
#include <zstd.h>

// 🎮 PLAYBACK STATE
std::atomic<bool> is_playing{false};
std::atomic<bool> should_stop{false};
std::atomic<int64_t> current_sample{0};

// 💀 GLITCH STATE
std::atomic<bool> glitch_enabled{false};
std::atomic<float> glitch_intensity{0.0f};
std::mt19937 glitch_rng(std::random_device{}());

// 🔥 HMICAP HEADER STRUCTURE
struct HMICAPHeader {
    char magic[8];          // "HMICAP01"
    uint32_t sample_rate;   // Hz
    uint16_t channels;      // 1=mono, 2=stereo
    uint16_t bit_depth;     // 32 for int32
    uint64_t total_samples; // Samples per channel
    uint8_t reserved2[12];
};

// 🎧 AUDIO DATA - PRE-RENDERED INT32 AND READY TO BLAST!!
struct AudioData {
    int sample_rate;
    int channels;
    int bit_depth;
    int64_t total_samples;
    std::vector<int32_t> interleaved_data; // INT32 SUPREMACY = MAXIMUM QUALITY fr fr
};

// 🔥 INT32 TO FLOAT CONVERSION (ZERO QUALITY LOSS)
inline float int32_to_float(int32_t sample) {
    // Convert from int32 [-2147483648, 2147483647] to float [-1.0, 1.0]
    return static_cast<float>(sample) / 2147483648.0f;
}

// 💀 GLITCH EFFECTS (MAXIMUM CHAOS MODE)
inline float apply_glitch(float sample, float intensity) {
    if (intensity <= 0.0f) return sample;
    
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float rand = dist(glitch_rng);
    
    // Multiple glitch types based on intensity
    if (rand < intensity * 0.1f) {
        // BIT CRUSH - reduce bit depth
        int bits = static_cast<int>(16.0f - intensity * 12.0f);
        float scale = powf(2.0f, bits);
        return floorf(sample * scale) / scale;
    } else if (rand < intensity * 0.2f) {
        // SAMPLE REPEAT - stutter effect
        return sample * (dist(glitch_rng) > 0.5f ? 1.0f : 0.0f);
    } else if (rand < intensity * 0.3f) {
        // INVERT - flip the sample
        return -sample;
    } else if (rand < intensity * 0.4f) {
        // DISTORTION - hard clip with random threshold
        float threshold = 0.3f + dist(glitch_rng) * 0.4f;
        return std::max(-threshold, std::min(threshold, sample)) / threshold;
    } else if (rand < intensity * 0.5f) {
        // NOISE INJECTION
        float noise = (dist(glitch_rng) * 2.0f - 1.0f) * intensity * 0.5f;
        return std::max(-1.0f, std::min(1.0f, sample + noise));
    } else if (rand < intensity * 0.6f) {
        // RING MODULATION
        float freq = 50.0f + dist(glitch_rng) * 500.0f;
        float mod = sinf(current_sample * freq * 0.001f);
        return sample * mod;
    } else if (rand < intensity * 0.7f) {
        // DOWNSAMPLE - reduce sample rate effect
        return (static_cast<int>(sample * 8.0f) / 8.0f);
    } else if (rand < intensity * 0.8f) {
        // SILENCE GAPS
        return 0.0f;
    }
    
    return sample;
}

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
    audio.bit_depth = header.bit_depth;
    audio.total_samples = header.total_samples;
    
    std::cout << "  ✅ Valid HMICAP header detected! 💚\n";
    std::cout << "  🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "  🎧 Channels: " << audio.channels << "\n";
    std::cout << "  💎 Bit depth: " << audio.bit_depth << "-bit\n";
    std::cout << "  📊 Total samples: " << audio.total_samples << " per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    
    if (audio.bit_depth != 32) {
        std::cerr << "⚠️  Warning: Expected 32-bit, got " << audio.bit_depth << "-bit\n";
    }
    
    // Read interleaved int32 sample data (INSTANT - just raw binary read!!)
    size_t total_samples_count = audio.total_samples * audio.channels;
    audio.interleaved_data.resize(total_samples_count);
    
    std::cout << "  📊 Reading " << total_samples_count * sizeof(int32_t) / 1024.0 / 1024.0 << " MB of audio data...\n";
    
    file.read(reinterpret_cast<char*>(audio.interleaved_data.data()),
              total_samples_count * sizeof(int32_t));
    
    if (!file) {
        std::cerr << "❌ Failed to read audio data\n";
        return false;
    }
    
    file.close();
    
    std::cout << "  ✅ HMICAP INT32 loaded INSTANTLY (no parsing needed fr fr) 🚀\n";
    
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
    audio.bit_depth = header.bit_depth;
    audio.total_samples = header.total_samples;
    
    std::cout << "  ✅ Valid HMICAP header! 💚\n";
    std::cout << "  🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "  🎧 Channels: " << audio.channels << "\n";
    std::cout << "  💎 Bit depth: " << audio.bit_depth << "-bit\n";
    std::cout << "  📊 Total samples: " << audio.total_samples << " per channel\n";
    std::cout << "  ⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    
    if (audio.bit_depth != 32) {
        std::cerr << "⚠️  Warning: Expected 32-bit, got " << audio.bit_depth << "-bit\n";
    }
    
    // Copy sample data
    size_t total_samples_count = audio.total_samples * audio.channels;
    audio.interleaved_data.resize(total_samples_count);
    
    std::memcpy(audio.interleaved_data.data(),
                decompressed_data.data() + sizeof(header),
                total_samples_count * sizeof(int32_t));
    
    std::cout << "  ✅ HMICAP7 INT32 loaded and ready to play! 🚀\n";
    
    return true;
}

// 🔊 PORTAUDIO CALLBACK (WITH OPTIONAL GLITCH EFFECTS!!)
static int audio_callback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    AudioData* audio = (AudioData*)userData;
    float* out = (float*)outputBuffer;
    float intensity = glitch_intensity.load();
    bool glitching = glitch_enabled.load();
    
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
            // Convert int32 to float and apply glitch if enabled
            int64_t base_idx = current_sample * audio->channels;
            for (int ch = 0; ch < audio->channels; ch++) {
                float sample = int32_to_float(audio->interleaved_data[base_idx + ch]);
                
                if (glitching) {
                    sample = apply_glitch(sample, intensity);
                }
                
                *out++ = sample;
            }
            current_sample++;
        }
    }
    
    return paContinue;
}

// 🎮 CONTROL THREAD (HANDLES GLITCH CONTROLS)
void control_thread() {
    std::cout << "\n💀 ═══ GLITCH CONTROLS ═══ 💀\n";
    std::cout << "Commands:\n";
    std::cout << "  g     - Toggle glitch on/off\n";
    std::cout << "  0-9   - Set glitch intensity (0=none, 9=maximum chaos)\n";
    std::cout << "  q     - Quit\n";
    std::cout << "  ?     - Show this help\n\n";
    
    while (is_playing && !should_stop) {
        std::string input;
        std::cout << ">> ";
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        
        char cmd = input[0];
        
        if (cmd == 'q' || cmd == 'Q') {
            should_stop = true;
            std::cout << "🛑 Stopping playback...\n";
            break;
        } else if (cmd == 'g' || cmd == 'G') {
            bool current = glitch_enabled.load();
            glitch_enabled.store(!current);
            std::cout << "💀 Glitch " << (!current ? "ENABLED 🔥" : "DISABLED ✅") << "\n";
        } else if (cmd >= '0' && cmd <= '9') {
            float intensity = (cmd - '0') / 9.0f;
            glitch_intensity.store(intensity);
            std::cout << "💀 Glitch intensity set to " << (int)(intensity * 100) << "% ";
            if (intensity == 0.0f) std::cout << "(clean)";
            else if (intensity < 0.3f) std::cout << "(subtle)";
            else if (intensity < 0.6f) std::cout << "(moderate)";
            else if (intensity < 0.9f) std::cout << "(intense)";
            else std::cout << "(MAXIMUM CHAOS)";
            std::cout << "\n";
        } else if (cmd == '?') {
            std::cout << "\n💀 ═══ GLITCH CONTROLS ═══ 💀\n";
            std::cout << "  g     - Toggle glitch on/off\n";
            std::cout << "  0-9   - Set glitch intensity\n";
            std::cout << "  q     - Quit\n";
            std::cout << "  ?     - Show this help\n\n";
        } else {
            std::cout << "❌ Unknown command. Press '?' for help.\n";
        }
    }
}

// 🎮 PLAY AUDIO (THE MAIN EVENT WITH GLITCH SUPPORT!!)
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
                              paFloat32,           // output as 32-bit float
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
    std::cout << "\n🎵 ═══ NOW PLAYING (INT32 FORMAT) ═══ 🎵\n";
    std::cout << "⏱️  Duration: " << (float)audio.total_samples / audio.sample_rate << " seconds\n";
    std::cout << "🎧 Channels: " << audio.channels << (audio.channels == 2 ? " (Stereo)" : " (Mono)") << "\n";
    std::cout << "🎵 Sample rate: " << audio.sample_rate << " Hz\n";
    std::cout << "💎 Bit depth: " << audio.bit_depth << "-bit (converted to float for playback)\n";
    std::cout << "💀 Glitch mode: AVAILABLE\n";
    
    // Start playback
    current_sample = 0;
    should_stop = false;
    glitch_enabled.store(false);
    glitch_intensity.store(0.0f);
    
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
            
            std::string glitch_status = "";
            if (glitch_enabled.load()) {
                int intensity_pct = (int)(glitch_intensity.load() * 100);
                glitch_status = " | 💀 GLITCHING " + std::to_string(intensity_pct) + "%";
            }
            
            std::cout << "\r🎵 " << std::fixed << std::setprecision(1)
                      << progress << "% | "
                      << time_elapsed << "s / " << total_time << "s"
                      << glitch_status << "        " << std::flush;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    // Control thread for glitch effects
    std::thread ctrl_thread(control_thread);
    
    // Wait for threads
    ctrl_thread.join();
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
    std::cout << "🔥🔥🔥 HMICAP PLAYER - INT32 GLITCH EDITION 🔥🔥🔥\n";
    std::cout << "💎 SUPPORTS: HMICAP (binary) & HMICAP7 (compressed) 💎\n";
    std::cout << "⚡ INT32 FORMAT = MAXIMUM QUALITY + INSTANT LOADING ⚡\n";
    std::cout << "💀 GLITCH MODE = REAL-TIME AUDIO CHAOS 💀\n\n";
    
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
        if (audio.interleaved_data[i] != 0) {
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
    
    std::cout << "\n💥 HMICAP INT32 GLITCH PLAYER SESSION COMPLETE 💥\n";
    std::cout << "🚀 32-BIT INTEGER FORMAT + REAL-TIME GLITCH EFFECTS = LITERALLY BLESSED 🚀\n";
    
    return 0;
}