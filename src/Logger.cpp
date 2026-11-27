#include "Logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iostream>

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;
bool Logger::initialized_ = false;
std::mutex Logger::mutex_;

void Logger::initializeLocked(const std::string& log_dir, const std::string& log_level) {
    try {
        std::filesystem::create_directories(log_dir);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_dir + "/audiochat.log", 5 * 1024 * 1024, 3);
        file_sink->set_level(spdlog::level::trace);

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        logger_ = std::make_shared<spdlog::logger>("AudioChat", sinks.begin(), sinks.end());
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        spdlog::register_logger(logger_);
    } catch (const std::exception& ex) {
        std::cerr << "File logging unavailable, using console logging: " << ex.what() << std::endl;
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("AudioChat", console_sink);
    }

    std::string lower_level = log_level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    logger_->set_level(spdlog::level::from_str(lower_level));
    initialized_ = true;
    logger_->info("Logger initialized successfully");
}

void Logger::init(const std::string& log_dir, const std::string& log_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }
    initializeLocked(log_dir, log_level);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        initializeLocked("logs", "info");
    }
    return logger_;
}

void Logger::setLevel(const std::string& level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!logger_) {
        return;
    }

    std::string lower_level = level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    logger_->set_level(spdlog::level::from_str(lower_level));
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
        logger_->flush();
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
        logger_->flush();
    }
    spdlog::shutdown();
    logger_.reset();
    initialized_ = false;
}
