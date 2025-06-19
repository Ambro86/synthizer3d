#include "synthizer/decoders/opus.hpp"
#include "synthizer/channel_mixing.hpp"
#include "synthizer/config.hpp"
#include "synthizer/logging.hpp"

extern "C" {
#include <opus/opusfile.h>
}

#ifndef OPUSCALL
# ifdef _WIN32
#  define OPUSCALL __cdecl
# else
#  define OPUSCALL
# endif
#endif

#include <vector>
#include <cstring>
#include <algorithm>

// --- CALLBACKS GLOBALI ---
// NOTA: int nbytes qui, come da tua typedef!
extern "C" int OPUSCALL read_cb(void *stream, unsigned char *ptr, int nbytes) {
    synthizer::ByteStream *bs = static_cast<synthizer::ByteStream *>(stream);
    return static_cast<int>(bs->read(static_cast<size_t>(nbytes), reinterpret_cast<char *>(ptr)));
}

extern "C" int OPUSCALL seek_cb(void *stream, opus_int64 offset, int whence) {
    synthizer::ByteStream *bs = static_cast<synthizer::ByteStream *>(stream);
    if (!bs->supportsSeek())
        return -1;
    if (whence == SEEK_SET)
        bs->seek(static_cast<int>(offset));
    else if (whence == SEEK_CUR)
        bs->seek(bs->getPosition() + static_cast<int>(offset));
    else if (whence == SEEK_END)
        bs->seek(bs->getLength() + static_cast<int>(offset));
    else
        return -1;
    return 0;
}

extern "C" int OPUSCALL close_cb(void *stream) {
    (void)stream;
    return 0;
}

extern "C" opus_int64 OPUSCALL tell_cb(void *stream) {
    synthizer::ByteStream *bs = static_cast<synthizer::ByteStream *>(stream);
    return static_cast<opus_int64>(bs->getPosition());
}

namespace synthizer {
namespace opus_detail {

class OpusDecoder : public AudioDecoder {
public:
    OpusDecoder(std::shared_ptr<LookaheadByteStream> stream) : stream(stream) {
        OpusFileCallbacks cbs;
        cbs.read = read_cb;
        cbs.seek = seek_cb;
        cbs.tell = tell_cb;
        cbs.close = close_cb;

        int err = 0;
        of = op_open_callbacks(stream.get(), &cbs, nullptr, 0, &err);
        if (of == nullptr) {
            throw Error("Unable to open Opus file, err=" + std::to_string(err));
        }

        sr = 48000; // Opus standard sample rate
        channels = op_channel_count(of, -1);
        frame_count = op_pcm_total(of, -1);
    }

    ~OpusDecoder() override {
        if (of) op_free(of);
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num, float *samples, unsigned int channels_req = 0) override {
        unsigned int ch_out = channels_req < 1 ? channels : channels_req;
        unsigned long long written = 0;
        
        // Always try float version first, then fallback to integer conversion
        std::vector<float> tmp_buf(config::BLOCK_SIZE * channels);
        std::vector<opus_int16> tmp_buf_int(config::BLOCK_SIZE * channels);

        while (written < num) {
            int to_read = static_cast<int>(std::min<unsigned long long>(num - written, config::BLOCK_SIZE));
            int frames = 0;
            
            // Try op_read_float first (if available at runtime)
            #ifdef OP_READ_FLOAT_AVAILABLE
            frames = op_read_float(of, tmp_buf.data(), to_read * channels, nullptr);
            #else
            // Fallback: Use integer version and convert to float
            frames = op_read(of, tmp_buf_int.data(), to_read * channels, nullptr);
            if (frames > 0) {
                // Convert from int16 to float with proper scaling
                for (int i = 0; i < frames * channels; ++i) {
                    tmp_buf[i] = static_cast<float>(tmp_buf_int[i]) / 32768.0f;
                }
            }
            #endif
            
            if (frames <= 0) break;

            if (channels == static_cast<int>(ch_out)) {
                std::memcpy(samples + written * ch_out, tmp_buf.data(), sizeof(float) * frames * ch_out);
            } else {
                mixChannels(static_cast<unsigned long long>(frames), tmp_buf.data(), channels, samples + written * ch_out, ch_out);
            }

            written += static_cast<unsigned long long>(frames);
        }

        return written;
    }

    int getSr() override { return sr; }
    int getChannels() override { return channels; }
    AudioFormat getFormat() override { return AudioFormat::Unknown; }
    void seekPcm(unsigned long long pos) override {
        if (op_pcm_seek(of, static_cast<opus_int64>(pos)) != 0) throw Error("Cannot seek in Opus file");
    }
    bool supportsSeek() override { return stream->supportsSeek(); }
    bool supportsSampleAccurateSeek() override { return supportsSeek(); }
    unsigned long long getLength() override { return frame_count; }

private:
    std::shared_ptr<ByteStream> stream;
    OggOpusFile *of = nullptr;
    int channels = 0;
    int sr = 48000;
    opus_int64 frame_count = 0;
};

} // namespace opus_detail

std::shared_ptr<AudioDecoder> decodeOpusFile(std::shared_ptr<LookaheadByteStream> stream) {
    try {
        stream->reset();
        return std::make_shared<opus_detail::OpusDecoder>(stream);
    } catch (...) {
        logDebug("OPUS decoder: error creating decoder");
        return nullptr;
    }
}

} // namespace synthizer
