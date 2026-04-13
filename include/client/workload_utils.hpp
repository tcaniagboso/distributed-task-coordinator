#pragma once

#include <string>

#include "types.hpp"
#include "../config/system_config.hpp"

namespace client {
    size_t get_text_size(WorkloadSize size) {
        switch (size) {
            case WorkloadSize::SHORT:
                return config::SHORT_TEXT_WORDS;
            case WorkloadSize::MEDIUM:
                return config::MEDIUM_TEXT_WORDS;
            case WorkloadSize::LONG:
                return config::LONG_TEXT_WORDS;
            default:
                return config::SHORT_TEXT_WORDS;
        }
    }

    uint64_t get_duration(WorkloadSize size) {
        switch (size) {
            case WorkloadSize::SHORT:
                return config::SHORT_DURATION_US;
            case WorkloadSize::MEDIUM:
                return config::MEDIUM_DURATION_US;
            case WorkloadSize::LONG:
                return config::LONG_DURATION_US;
            default:
                return config::SHORT_DURATION_US;
        }
    }

    std::string generate_text(size_t num_words, TextMode mode = TextMode::CLEAN) {
        static const std::string clean = "distributed";
        static const std::string noisy = "distributed's";

        const std::string &word = (mode == TextMode::CLEAN) ? clean : noisy;

        std::string result;
        result.reserve(num_words * (word.size() + 1));

        for (size_t i = 0; i < num_words; i++) {
            result += word;
            if (mode == TextMode::CLEAN) {
                result += ' ';
            } else {
                result += " !?,.; ";
            }
        }

        return result;
    }
} // namespace client