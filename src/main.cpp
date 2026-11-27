#include "AudioChatEngine.h"
#include "AudioDeviceMonitor.h"
#include "ConfigManager.h"
#include "Logger.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include <windows.h>

namespace {

std::atomic<bool> g_running{true};

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

AudioChatConfig convertConfig(const ApplicationConfig& config) {
    AudioChatConfig engine_config;
    engine_config.local_ip = config.network.local_ip;
    engine_config.local_port = config.network.local_port;
    engine_config.remote_ip = config.network.remote_ip;
    engine_config.remote_port = config.network.remote_port;
    engine_config.sample_rate = static_cast<int>(config.audio.sample_rate);
    engine_config.frame_duration_ms = static_cast<int>(config.audio.frame_duration_ms);
    engine_config.input_device = config.audio.input_device;
    engine_config.output_device = config.audio.output_device;
    engine_config.bitrate = static_cast<int>(config.codec.bitrate);
    engine_config.complexity = static_cast<int>(config.codec.complexity);
    engine_config.enable_fec = config.codec.enable_fec;
    engine_config.enable_dtx = config.codec.enable_dtx;
    engine_config.enable_aec = config.aec.enable;
    engine_config.aec_suppression_level = static_cast<int>(config.aec.suppression_level);
    engine_config.reference_buffer_ms = static_cast<int>(config.aec.filter_length_ms);
    return engine_config;
}

void printUsage() {
    std::cout
        << "AudioChat 0.5.0\n"
        << "Usage: AudioChat [options]\n"
        << "  --config <path>     Load YAML configuration\n"
        << "  --preset <name>     low-latency, balanced, high-quality\n"
        << "  --list-devices      List PortAudio input/output devices\n"
        << "  --check             Validate configuration and exit\n"
        << "  --duration <sec>    Stop automatically after N seconds\n"
        << "  --version           Print version\n"
        << "  --help              Show this help\n";
}

void listDevices() {
    AudioDeviceMonitor monitor;
    std::cout << "Input devices:\n";
    for (const auto& device : monitor.getInputDevices()) {
        std::cout << "  [" << device.device_id << "] " << device.device_name
                  << (device.is_default ? " (default)" : "") << '\n';
    }
    std::cout << "Output devices:\n";
    for (const auto& device : monitor.getOutputDevices()) {
        std::cout << "  [" << device.device_id << "] " << device.device_name
                  << (device.is_default ? " (default)" : "") << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    Logger::init("logs", "info");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    std::string config_path = "config/default.yaml";
    std::string preset;
    bool explicit_config = false;
    bool check_only = false;
    bool list_only = false;
    int duration_seconds = 0;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            auto requireValue = [&](const char* option) -> std::string {
                if (index + 1 >= argc) {
                    throw std::invalid_argument(std::string("Missing value for ") + option);
                }
                return argv[++index];
            };

            if (argument == "--help" || argument == "-h") {
                printUsage();
                return 0;
            }
            if (argument == "--version") {
                std::cout << "AudioChat 0.5.0\n";
                return 0;
            }
            if (argument == "--config") {
                config_path = requireValue("--config");
                explicit_config = true;
            } else if (argument == "--preset") {
                preset = requireValue("--preset");
            } else if (argument == "--list-devices") {
                list_only = true;
            } else if (argument == "--check") {
                check_only = true;
            } else if (argument == "--duration") {
                duration_seconds = std::stoi(requireValue("--duration"));
                if (duration_seconds < 0) {
                    throw std::invalid_argument("Duration must be non-negative");
                }
            } else {
                throw std::invalid_argument("Unknown option: " + argument);
            }
        }

        if (list_only) {
            listDevices();
            return 0;
        }

        ConfigManager config_manager;
        if (!preset.empty() && !config_manager.loadPresetByName(preset)) {
            throw std::runtime_error(config_manager.getLastError());
        }

        if (explicit_config || std::filesystem::exists(config_path)) {
            if (!config_manager.loadFromFile(config_path)) {
                throw std::runtime_error(config_manager.getLastError());
            }
        } else {
            LOG_WARN("Default configuration not found: {}; using built-in defaults", config_path);
        }

        if (!config_manager.validate()) {
            throw std::runtime_error(config_manager.getLastError());
        }

        const auto& config = config_manager.getConfig();
#ifndef HAS_WEBRTC_AEC
        if (config.aec.enable) {
            if (check_only) {
                std::cout << "Warning: runtime AEC is unavailable in this build.\n";
            } else {
                throw std::runtime_error(
                    "AEC was requested but this build has no WebRTC backend; "
                    "build with the aec feature or set aec.enable=false");
            }
        }
#endif
        std::cout << "AudioChat 0.5.0\n"
                  << "Local:  " << config.network.local_ip << ':' << config.network.local_port << '\n'
                  << "Remote: " << config.network.remote_ip << ':' << config.network.remote_port << '\n'
                  << "Audio:  " << config.audio.sample_rate << " Hz, "
                  << config.audio.frame_duration_ms << " ms, "
                  << config.codec.bitrate << " bps\n";

        if (check_only) {
            std::cout << "Configuration valid.\n";
            return 0;
        }

        AudioChatEngine engine(convertConfig(config));
        auto result = engine.init();
        if (result.isError()) {
            throw std::runtime_error("Engine initialization failed: " + result.message());
        }
        result = engine.start();
        if (result.isError()) {
            throw std::runtime_error("Engine start failed: " + result.message());
        }

        std::cout << "Running. Press Ctrl+C to stop.\n";
        std::cout << "AEC: " << (engine.isAecActive() ? "active" : "inactive") << "\n";
        const auto started = std::chrono::steady_clock::now();
        while (g_running.load()) {
            if (!engine.isRunning()) {
                break;
            }
            if (duration_seconds > 0 &&
                std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(duration_seconds)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        LOG_INFO("AEC statistics:\n{}", engine.getAecStatistics());
        const std::string fatal_error = engine.getFatalError();
        result = engine.stop();
        if (result.isError()) {
            LOG_WARN("Engine stop reported: {}", result.message());
        }
        if (!fatal_error.empty()) {
            std::cerr << "Fatal runtime error: " << fatal_error << '\n';
            return 1;
        }
        std::cout << "Stopped.\n";
        return 0;
    } catch (const std::exception& error) {
        LOG_ERROR("Fatal error: {}", error.what());
        std::cerr << "Error: " << error.what() << '\n';
        Logger::shutdown();
        return 1;
    }
}
