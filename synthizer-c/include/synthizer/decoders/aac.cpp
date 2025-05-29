#include "synthizer/decoders/aac.hpp" // Assuming this will be the path

#include "synthizer/channel_mixing.hpp" // For potential future use with channel mapping
#include "synthizer/config.hpp"         // For potential config values
#include "synthizer/logging.hpp"

extern "C" {
#include <neaacdec.h>
}

#include <vector>
#include <cstring>   // For std::memcpy and std::memcmp
#include <algorithm> // For std::min

// Define a default buffer size for NeAACDecInit if not available in synthizer::config
// This should be large enough for FAAD2 to parse initial headers.
// FAAD2_MIN_STREAMSIZE for AAC-LC is 768 bytes. A few KB is safer.
#ifndef SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE
#define SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE 4096
#endif

// Minimum bytes we expect to successfully initialize FAAD2.
// This is a heuristic; ADTS headers are small, but NeAACDecInit might need more context.
#define MIN_BYTES_FOR_AAC_INIT 64

namespace synthizer {

namespace aac_detail {

// Callback for FAAD2 to read data from the ByteStream
long faad_read_callback(void *user_data, void *buffer, unsigned long bytes) {
    ByteStream *stream = static_cast<ByteStream *>(user_data);
    if (!stream) {
        return -1; // Error condition
    }
    return static_cast<long>(stream->read(bytes, static_cast<char *>(buffer)));
}

// Callback for FAAD2 to seek within the ByteStream
long faad_seek_callback(void *user_data, unsigned long long offset, int whence) {
    ByteStream *stream = static_cast<ByteStream *>(user_data);
    if (!stream || !stream->supportsSeek()) {
        return -1; // Error or not supported
    }

    long long stream_len = static_cast<long long>(stream->getLength());
    long long target_pos = 0;

    switch (whence) {
    case SEEK_SET:
        target_pos = static_cast<long long>(offset);
        break;
    case SEEK_CUR:
        target_pos = static_cast<long long>(stream->getPosition()) + static_cast<long long>(offset);
        break;
    case SEEK_END:
        if (stream_len <= 0) return -1; // Cannot seek from end if length is unknown
        target_pos = stream_len + static_cast<long long>(offset);
        break;
    default:
        return -1; // Invalid whence
    }

    if (target_pos < 0 || (stream_len > 0 && target_pos > stream_len) ) {
        // logDebug("AAC Seek: Target position %lld out of bounds (length %lld)", target_pos, stream_len);
        // FAAD2 expects 0 on success, non-zero on error.
        // Depending on how strict FAAD2 is, seeking past EOF might be an error or clamped.
        // For safety, let's call it an error if target is clearly out of bounds.
        // If length is unknown, we can only check for negative offset.
        if (target_pos < 0) return -1;
        if (stream_len > 0 && target_pos > stream_len) return -1;
    }
    
    stream->seek(static_cast<unsigned long long>(target_pos));
    return 0; // Success
}

class AacDecoder : public AudioDecoder {
public:
    AacDecoder(std::shared_ptr<LookaheadByteStream> stream_ptr) :
        stream(stream_ptr),
        decoder_handle(nullptr),
        channels(0),
        sample_rate(0),
        frame_count(0), // Raw AAC streams often don't have a total frame count in headers
        current_frame_data(nullptr) {

        decoder_handle = NeAACDecOpen();
        if (!decoder_handle) {
            throw Error("Unable to open FAAD2 decoder: NeAACDecOpen() failed.");
        }

        // Configure FAAD2 to output float samples
        NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(decoder_handle);
        if (!config) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Failed to get FAAD2 configuration.");
        }
        config->outputFormat = FAAD_FMT_FLOAT;
        // Potentially set other configurations, e.g., defSampleRate if needed, though
        // it should be determined from the stream.
        // config->defSampleRate = 44100; // Example, not typically needed if stream has info
        // config->dontUpSampleImplicitSBR = 1; // Example SBR handling
        if (NeAACDecSetConfiguration(decoder_handle, config) == 0) {
             NeAACDecClose(decoder_handle);
             decoder_handle = nullptr;
             throw Error("Failed to set FAAD2 configuration (output format to float).");
        }
        
        // Set up FAAD2 callbacks
        NeAACDecCallbacks faad_callbacks;
        std::memset(&faad_callbacks, 0, sizeof(faad_callbacks));
        faad_callbacks.read_callback = faad_read_callback;
        faad_callbacks.seek_callback = faad_seek_callback;
        faad_callbacks.user_data = stream.get();
        NeAACDecSetCallbacks(decoder_handle, &faad_callbacks);

        // Initialize the decoder with the first chunk of data from the stream.
        // NeAACDecInit requires a buffer with some stream data to parse headers.
        unsigned char *init_buffer_ptr = nullptr;
        long init_buffer_len = stream->lookahead(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE, &init_buffer_ptr);

        if (init_buffer_len < MIN_BYTES_FOR_AAC_INIT) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            // stream->reset(); // Not strictly needed if lookahead doesn't consume on failure to get enough bytes
            throw Error("AAC stream too short to initialize decoder: got " + std::to_string(init_buffer_len) + " bytes.");
        }

        unsigned long sr_long;
        unsigned char ch_uchar;
        // NeAACDecInit will read from init_buffer_ptr and return bytes consumed.
        long bytes_consumed = NeAACDecInit(decoder_handle, init_buffer_ptr, init_buffer_len, &sr_long, &ch_uchar);

        if (bytes_consumed < 0) { // Error during init
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            // stream->reset(); // Lookahead bytes not consumed yet.
            throw Error("Failed to initialize AAC decoder: FAAD2 error code " + std::to_string(bytes_consumed));
        }
        
        // Consume the bytes that NeAACDecInit used from the lookahead buffer.
        stream->consumeLookedBytes(bytes_consumed);

        this->sample_rate = static_cast<int>(sr_long);
        this->channels = static_cast<int>(ch_uchar);

        if (this->sample_rate == 0 || this->channels == 0) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("AAC stream parameters (sample rate/channels) invalid after init.");
        }

        // Try to estimate frame_count if stream length is known. This is a rough estimate.
        // AAC frame size is typically 1024 samples (can vary with SBR, LD, etc.)
        // This is very approximate for raw AAC.
        if (stream->getLength() > 0 && this->sample_rate > 0 && this->channels > 0) {
            // A very rough estimate assuming an average bitrate leading to ~1024 samples/frame
            // This part is highly speculative for raw ADTS/ADIF without more info.
            // For now, let's keep frame_count as 0, indicating unknown length.
            this->frame_count = 0; 
        }
    }

    ~AacDecoder() override {
        if (decoder_handle) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
        }
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num_frames_to_write, float *output_samples, unsigned int channels_req = 0) override {
        if (!decoder_handle) return 0;

        unsigned int ch_out = (channels_req < 1 || channels_req > CHANNELS_MAX) ? this->channels : channels_req;
        unsigned long long frames_written_total = 0;
        NeAACDecFrameInfo frame_info;

        while (frames_written_total < num_frames_to_write) {
            // Decode one frame.
            // If callbacks are set, buffer and buffer_size for NeAACDecDecode can be NULL and 0.
            // FAAD2 will use the read_callback internally.
            current_frame_data = NeAACDecDecode(decoder_handle, &frame_info, nullptr, 0);

            if (frame_info.error > 0) {
                logDebug("FAAD2 decoding error: %s\n", NeAACDecGetErrorMessage(frame_info.error));
                // Consider if this is a fatal error or if we can continue.
                // For now, stop decoding on error.
                break;
            }

            if (current_frame_data == nullptr || frame_info.samples == 0) {
                // End of stream or no samples produced
                break;
            }
            
            // frame_info.samples contains samples per channel for the current frame.
            // frame_info.channels contains the actual number of channels in decoded_frame_data.
            // The decoded_frame_data is interleaved: L, R, L, R ... for stereo float.
            
            unsigned long long frames_in_this_faad_frame = frame_info.samples;
            unsigned int channels_in_faad_frame = frame_info.channels;
            float* input_ptr = static_cast<float*>(current_frame_data);

            unsigned long long frames_to_copy_this_iteration = std::min(frames_in_this_faad_frame, num_frames_to_write - frames_written_total);

            for (unsigned long long f = 0; f < frames_to_copy_this_iteration; ++f) {
                for (unsigned int c_in = 0; c_in < channels_in_faad_frame; ++c_in) {
                    if (c_in < ch_out) {
                        // output_samples is the user's buffer, expecting ch_out channels
                        output_samples[(frames_written_total + f) * ch_out + c_in] = *input_ptr;
                    }
                    input_ptr++; // Advance in the FAAD2 output buffer (already interleaved)
                }
                // Zero-fill extra channels if upmixing (ch_out > channels_in_faad_frame)
                for (unsigned int c_fill = channels_in_faad_frame; c_fill < ch_out; ++c_fill) {
                    output_samples[(frames_written_total + f) * ch_out + c_fill] = 0.0f;
                }
            }
            frames_written_total += frames_to_copy_this_iteration;
        }
        return frames_written_total;
    }

    int getSr() override { return sample_rate; }
    int getChannels() override { return channels; }
    AudioFormat getFormat() override { return AudioFormat::Float32; } // Configured FAAD2 for float

    void seekPcm(unsigned long long pos) override {
        if (!decoder_handle || !stream->supportsSeek()) {
            throw Error("AAC seeking not supported or stream not seekable.");
        }
        // Seeking in raw AAC to an exact PCM sample is non-trivial with FAAD2.
        // FAAD2's seek callback operates on the byte stream.
        // A precise PCM seek would require:
        // 1. Estimating byte offset for PCM sample `pos`.
        // 2. Calling stream->seek() (via faad_seek_callback indirectly if FAAD2 uses it for this,
        //    or directly if we manage seeking state).
        // 3. Re-syncing/flushing the decoder.
        // 4. Decoding and discarding samples until `pos` is reached.
        // This is complex. For now, this function could be a no-op or throw.
        // The `faad_seek_callback` allows FAAD2 to seek its input. If FAAD2 itself
        // had a PCM seek function that used this, it would work. But it doesn't.
        // Let's throw, as per Ogg example if ov_pcm_seek fails.
        logDebug("AacDecoder::seekPcm is not fully implemented for sample-accurate seeking.");
        throw Error("Sample-accurate PCM seeking is not implemented for this AAC decoder.");
        // If we wanted a byte-level seek, we could do:
        // stream->seek(pos_in_bytes);
        // And then potentially re-init or flush FAAD2, but NeAACDecInit is for the start.
        // There isn't a clear "flush and resync at new position" in FAAD2 API easily.
    }

    bool supportsSeek() override { return stream->supportsSeek(); }
    // Sample-accurate seek is hard for raw AAC without an index.
    bool supportsSampleAccurateSeek() override { return false; } 
    unsigned long long getLength() override { return frame_count; } // Often unknown for raw AAC

private:
    std::shared_ptr<LookaheadByteStream> stream;
    NeAACDecHandle decoder_handle;
    int channels;
    int sample_rate;
    unsigned long long frame_count; // In PCM samples per channel

    // FAAD2's NeAACDecDecode returns a pointer to an internal buffer.
    // We don't need to manage this buffer's memory directly, other than not using it after next decode call.
    void* current_frame_data; 
};

} // namespace aac_detail

std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream) {
    if (!stream) {
        return nullptr;
    }

    // Try to identify AAC stream.
    // ADTS syncword: 12 bits of 1 (0xFFF). Check first 2 bytes: buffer[0] == 0xFF, (buffer[1] & 0xF0) == 0xF0.
    // ADIF format starts with "ADIF".
    unsigned char header[4] = {0}; // Read up to 4 bytes for ADIF check
    long actual_read = stream->read(4, reinterpret_cast<char*>(header));

    bool looks_like_aac = false;
    if (actual_read >= 2) {
        if (header[0] == 0xFF && (header[1] & 0xF0) == 0xF0) {
            looks_like_aac = true; // Likely ADTS
        }
    }
    if (!looks_like_aac && actual_read == 4) {
        if (std::memcmp(header, "ADIF", 4) == 0) {
            looks_like_aac = true; // Likely ADIF
        }
    }

    // IMPORTANT: Reset the stream so the AacDecoder starts reading from the beginning.
    stream->reset();

    if (!looks_like_aac) {
        return nullptr; // Not recognized as AAC
    }

    try {
        return std::make_shared<aac_detail::AacDecoder>(stream);
    } catch (const std::exception &e) {
        logDebug("AAC decoder: Failed to create AacDecoder: %s", e.what());
        // Stream is already reset. If AacDecoder constructor threw, its internal state is managed by its destructor.
        return nullptr;
    } catch (...) {
        logDebug("AAC decoder: Unknown error creating AacDecoder.");
        return nullptr;
    }
}

} // namespace synthizer
