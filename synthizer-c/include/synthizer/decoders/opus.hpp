#pragma once

#include "synthizer/byte_stream.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include <memory>

namespace synthizer {

std::shared_ptr<AudioDecoder> decodeOpusFile(std::shared_ptr<LookaheadByteStream> stream);

}
