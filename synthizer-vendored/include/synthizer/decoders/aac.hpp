#pragma once

#include "synthizer/byte_stream.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"

#include <memory>

namespace synthizer {

/**
 * @brief Creates an AudioDecoder for AAC streams.
 *
 * This function attempts to detect an AAC stream (ADTS or ADIF format) from the provided ByteStream.
 * If successful, it returns a shared_ptr to an AacDecoder. Otherwise, it returns nullptr.
 *
 * @param stream A shared_ptr to a LookaheadByteStream representing the input AAC audio data.
 * @return std::shared_ptr<AudioDecoder> A decoder instance if successful, otherwise nullptr.
 */
std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream);

} // namespace synthizer