#include "synthizer/decoders/ogg.hpp"
#include "synthizer/channel_mixing.hpp"
#include "synthizer/config.hpp"
#include "synthizer/logging.hpp"

extern "C" {
#include <vorbis/vorbisfile.h>
}

#include <vector>
#include <cstring>
#include <algorithm>

namespace synthizer {

namespace ogg_detail {

size_t read_cb(void *ptr, size_t size, size_t nmemb, void *datasource) {
    ByteStream *stream = static_cast<ByteStream *>(datasource);
    return stream->read(size * nmemb, static_cast<char *>(ptr));
}

int seek_cb(void *datasource, ogg_int64_t offset, int whence) {
    ByteStream *stream = static_cast<ByteStream *>(datasource);
    if (!stream->supportsSeek())
        return -1;
    if (whence == SEEK_SET)
        stream->seek(static_cast<int>(offset));
    else if (whence == SEEK_CUR)
        stream->seek(stream->getPosition() + static_cast<int>(offset));
    else if (whence == SEEK_END)
        stream->seek(stream->getLength() + static_cast<int>(offset));
    else
        return -1;
    return 0;
}

int close_cb(void *datasource) {
    (void)datasource; // Evita warning C4100 (unused parameter)
    // Niente da fare, il ByteStream è gestito da std::shared_ptr
    return 0;
}

long tell_cb(void *datasource) {
    ByteStream *stream = static_cast<ByteStream *>(datasource);
    return static_cast<long>(stream->getPosition());
}

class OggDecoder : public AudioDecoder {
public:
    OggDecoder(std::shared_ptr<LookaheadByteStream> stream) : stream(stream) {
        ov_callbacks cbs;
        cbs.read_func = read_cb;
        cbs.seek_func = seek_cb;
        cbs.close_func = close_cb;
        cbs.tell_func = tell_cb;

        std::memset(&vf, 0, sizeof(vf));
        if (ov_open_callbacks(stream.get(), &vf, nullptr, 0, cbs) < 0) {
            throw Error("Unable to open ogg/vorbis stream");
        }

        auto info = ov_info(&vf, -1);
        channels = info->channels;
        sr = info->rate;
        ogg_int64_t total_frames = ov_pcm_total(&vf, -1);
        if (total_frames < 0) {
            // File senza informazioni sulla lunghezza, determina la lunghezza manualmente
            frame_count = 0;
            ogg_int64_t current_pos = ov_pcm_tell(&vf);
            if (ov_pcm_seek(&vf, 0) == 0) {
                // Vai alla fine per determinare la lunghezza
                long bitstream;
                float **pcm;
                long samples_read;
                while ((samples_read = ov_read_float(&vf, &pcm, 4096, &bitstream)) > 0) {
                    frame_count += samples_read;
                }
                // Ritorna alla posizione originale
                ov_pcm_seek(&vf, current_pos);
            }
            if (frame_count == 0) {
                throw Error("Cannot determine OGG file length");
            }
        } else {
            frame_count = static_cast<unsigned long long>(total_frames);
        }
    }

    ~OggDecoder() override {
        ov_clear(&vf);
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num, float *samples, unsigned int channels_req = 0) override {
        unsigned int ch_out = channels_req < 1 ? channels : channels_req;
        unsigned long long written = 0;

        while (written < num) {
            float **pcm = nullptr;
            long frames = ov_read_float(&vf, &pcm, static_cast<int>(num - written), nullptr);
            if (frames <= 0) break;

            // Interleaving
            for (long f = 0; f < frames; ++f) {
                for (int c = 0; c < channels; ++c) {
                    if (static_cast<unsigned int>(c) < ch_out)
                        samples[(written + static_cast<unsigned long long>(f)) * ch_out + c] = pcm[c][f];
                }
                // Azzeri i canali extra se richiesto
                for (unsigned int c = channels; c < ch_out; ++c) {
                    samples[(written + static_cast<unsigned long long>(f)) * ch_out + c] = 0.0f;
                }
            }

            written += static_cast<unsigned long long>(frames);
        }

        return written;
    }

    int getSr() override { return sr; }
    int getChannels() override { return channels; }
    AudioFormat getFormat() override { return AudioFormat::Unknown; }
    void seekPcm(unsigned long long pos) override {
        if (ov_pcm_seek(&vf, pos) != 0) throw Error("Cannot seek in Ogg file");
    }
    bool supportsSeek() override { return stream->supportsSeek(); }
    bool supportsSampleAccurateSeek() override { return supportsSeek(); }
    unsigned long long getLength() override { return frame_count; }

private:
    std::shared_ptr<ByteStream> stream;
    OggVorbis_File vf;
    int channels = 0;
    int sr = 0;
    unsigned long long frame_count = 0;
};

} // namespace ogg_detail

std::shared_ptr<AudioDecoder> decodeOgg(std::shared_ptr<LookaheadByteStream> stream) {
    try {
        char header[4] = {0};
        auto actuallyRead = stream->read(4, header);
        if (actuallyRead != 4 || std::memcmp(header, "OggS", 4) != 0)
            return nullptr;
        stream->reset();
        return std::make_shared<ogg_detail::OggDecoder>(stream);
    } catch (...) {
        logDebug("OGG decoder: error creating decoder");
        return nullptr;
    }
}

} // namespace synthizer
