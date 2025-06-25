#pragma once

#include "synthizer/byte_stream.hpp"
#include "synthizer/channel_mixing.hpp"
#include "synthizer/config.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include "synthizer/types.hpp"

#include <neaacdec.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace synthizer {

std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream);

namespace aac_detail {

class AacDecoder : public AudioDecoder {
public:
    AacDecoder(std::shared_ptr<LookaheadByteStream> stream);
    ~AacDecoder() override;
    unsigned long long writeSamplesInterleaved(unsigned long long num, float *samples, unsigned int channels = 0) override;
    int getSr() override;
    int getChannels() override;
    AudioFormat getFormat() override;
    void seekPcm(unsigned long long pos) override;
    bool supportsSeek() override;
    bool supportsSampleAccurateSeek() override;
    unsigned long long getLength() override;

private:
    NeAACDecHandle decoder;
    std::shared_ptr<ByteStream> stream;
    unsigned long sr;
    unsigned char channels;
    unsigned long long frame_count;
    bool initialized;
    std::array<uint8_t, 4096> buffer;
    size_t buffer_size;
    size_t buffer_pos;
    std::array<float, config::BLOCK_SIZE * config::MAX_CHANNELS> tmp_buf{{0.0f}};
    
    bool fillBuffer();
    bool initializeDecoder();
    unsigned long long decodeFramesDirect(unsigned long long num_frames, float *samples);
    unsigned long long decodeFramesWithMixing(unsigned long long num_frames, float *samples, unsigned int output_channels);
};

} // namespace aac_detail

} // namespace synthizer