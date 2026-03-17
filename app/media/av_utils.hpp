#pragma once

#include "av_types.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace nemo
{

    /// Normalizza un nome di codec in forma canonica maiuscola.
    inline std::string normalizeCodecName(const std::string &codec)
    {
        std::string out;
        out.reserve(codec.size());
        for (char c : codec)
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if (out == "AAC" || out == "AACLC" || out == "AAC_LC") return "AAC_LC";
        if (out == "OPUS")                                      return "OPUS";
        if (out == "PCM" || out == "PCM16" || out == "PCM_S16LE") return "PCM";
        return out;
    }

    /// Restituisce true se il codec e' PCM (nessuna decodifica necessaria).
    inline bool isPcmCodec(const std::string &codec)
    {
        return normalizeCodecName(codec) == "PCM";
    }

    /// Converte un array di byte in stringa esadecimale (per i GStreamer caps codec_data).
    inline std::string bytesToHex(const std::vector<uint8_t> &data)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t byte : data)
            oss << std::setw(2) << static_cast<int>(byte);
        return oss.str();
    }

    /// Overload per puntatore raw con limite max_bytes (usato nel logging).
    inline std::string bytesToHex(const uint8_t *data, std::size_t size, std::size_t max_bytes)
    {
        if (!data || size == 0 || max_bytes == 0) return "<empty>";
        const std::size_t count = std::min(size, max_bytes);
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i) oss << ' ';
            oss << std::setw(2) << static_cast<int>(data[i]);
        }
        if (size > count) oss << " ...";
        return oss.str();
    }

    /// Timestamp corrente in millisecondi (steady_clock, zero-overhead su hot path).
    inline uint64_t nowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// Interpreta una stringa di env-var come valore booleano.
    inline bool envTruthy(const char *value)
    {
        if (!value || !*value) return false;
        std::string s(value);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return (s == "1" || s == "true" || s == "yes" || s == "on");
    }

} // namespace nemo
