#include "Logger.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <iostream>

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
bool Logger::initialized_ = false;

void Logger::init(const std::string& log_dir, const std::string& log_level) {
    if (initialized_) {
        return;
    }

    try {
        // Create log directory if not exists
        std::filesystem::create_directories(log_dir);

        // Create console sink (stdout with colors)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);

        // Create rotating file sink (max 5MB per file, keep 3 files)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_dir + "/audiochat.log", 5 * 1024 * 1024, 3);
        file_sink->set_level(spdlog::level::trace);

        // Combine sinks
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};

        // Create logger
        logger_ = std::make_shared<spdlog::logger>("AudioChat", sinks.begin(), sinks.end());
        logger_->set_level(spdlog::level::trace);

        // Set pattern
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        // Register logger
        spdlog::register_logger(logger_);

        // Set log level
        setLevel(log_level);

        initialized_ = true;

        LOG_INFO("Logger initialized successfully");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (!initialized_) {
        init();
    }
    return logger_;
}

void Logger::setLevel(const std::string& level) {
    if (!logger_) {
        return;
    }

    std::string lower_level = level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), 
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_level == "trace") {
        logger_->set_level(spdlog::level::trace);
    } else if (lower_level == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (lower_level == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (lower_level == "warn") {
        logger_->set_level(spdlog::level::warn);
    } else if (lower_level == "error") {
        logger_->set_level(spdlog::level::err);
    } else if (lower_level == "critical") {
        logger_->set_level(spdlog::level::critical);
    } else {
        LOG_WARN("Unknown log level: {}, setting to info", level);
        logger_->set_level(spdlog::level::info);
    }
}

void Logger::flush() {
    if (logger_) {
        logger_->flush();
        spdlog::flush_every(std::chrono::seconds(5));
    }
}

void Logger::shutdown() {
    if (logger_) {
        logger_->flush();
        spdlog::shutdown();
        initialized_ = false;
    }
}
