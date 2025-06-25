#include "synthizer/decoders/aac.hpp"
#include <cstring>

namespace synthizer {

namespace aac_detail {

AacDecoder::AacDecoder(std::shared_ptr<LookaheadByteStream> stream_in) 
    : decoder(nullptr), sr(0), channels(0), frame_count(0), initialized(false), 
      buffer_size(0), buffer_pos(0) {
    
    stream_in->resetFinal();
    this->stream = stream_in;
    
    this->decoder = NeAACDecOpen();
    if (!this->decoder) {
        throw Error("Unable to open AAC decoder");
    }
    
    if (!initializeDecoder()) {
        NeAACDecClose(this->decoder);
        throw Error("Unable to initialize AAC decoder");
    }
}

AacDecoder::~AacDecoder() {
    if (this->decoder) {
        NeAACDecClose(this->decoder);
    }
}

bool AacDecoder::fillBuffer() {
    if (buffer_pos < buffer_size) {
        // Move remaining data to the start of the buffer
        size_t remaining = buffer_size - buffer_pos;
        std::memmove(buffer.data(), buffer.data() + buffer_pos, remaining);
        buffer_size = remaining;
        buffer_pos = 0;
    } else {
        buffer_size = 0;
        buffer_pos = 0;
    }

    // Read new data into the remaining space
    size_t bytes_to_read = buffer.size() - buffer_size;
    size_t bytes_read = stream->read(bytes_to_read, reinterpret_cast<char*>(buffer.data() + buffer_size));
    buffer_size += bytes_read;

    return buffer_size > 0;
}

bool AacDecoder::initializeDecoder() {
    if (!fillBuffer()) {
        return false;
    }
    
    NeAACDecConfiguration *config = NeAACDecGetCurrentConfiguration(this->decoder);
    config->outputFormat = FAAD_FMT_FLOAT;
    config->defSampleRate = 44100;
    config->dontUpSampleImplicitSBR = 1;
    NeAACDecSetConfiguration(this->decoder, config);
    
    unsigned long samplerate;
    unsigned char local_channels;
    
    long init_result = NeAACDecInit(this->decoder, 
                                   buffer.data() + buffer_pos, 
                                   buffer_size - buffer_pos,
                                   &samplerate, 
                                   &local_channels);
    
    if (init_result < 0) {
        return false;
    }
    
    buffer_pos += init_result;
    
    this->sr = samplerate;
    this->channels = local_channels;
    this->initialized = true;
    
    if (this->channels == 0 || this->channels > config::MAX_CHANNELS) {
        return false;
    }
    
    this->frame_count = 0;
    
    return true;
}

unsigned long long AacDecoder::writeSamplesInterleaved(unsigned long long num_frames, 
                                                      float *samples, 
                                                      unsigned int output_channels) {
    if (!initialized) {
        return 0;
    }
    
    unsigned int actual_channels = (output_channels < 1 || output_channels > config::MAX_CHANNELS) 
                                  ? this->channels 
                                  : output_channels;
    
    if (actual_channels == this->channels) {
        return decodeFramesDirect(num_frames, samples);
    } else {
        return decodeFramesWithMixing(num_frames, samples, actual_channels);
    }
}

unsigned long long AacDecoder::decodeFramesDirect(unsigned long long num_frames, float *samples) {
    unsigned long long frames_decoded = 0;
    
    while (frames_decoded < num_frames) {
        if (!fillBuffer()) {
            break;
        }
        
        // Se non ci sono dati disponibili nel buffer, esci
        if (buffer_pos >= buffer_size) {
            break;
        }
        
        NeAACDecFrameInfo frame_info;
        void *decoded_samples = NeAACDecDecode(this->decoder, 
                                             &frame_info,
                                             buffer.data() + buffer_pos, 
                                             buffer_size - buffer_pos);
        
        if (frame_info.error != 0) {
            break;
        }
        
        // Avanza nel buffer
        if (frame_info.bytesconsumed > 0) {
            buffer_pos += frame_info.bytesconsumed;
        } else {
            // Se non viene consumato nessun byte, avanza di almeno 1 per evitare loop infiniti
            buffer_pos++;
            if (buffer_pos >= buffer_size) {
                break;
            }
        }
        
        if (frame_info.samples == 0) {
            continue;
        }
        
        unsigned long long frame_samples = frame_info.samples / this->channels;
        unsigned long long frames_to_copy = std::min(frame_samples, num_frames - frames_decoded);
        
        std::memcpy(samples + frames_decoded * this->channels,
                   decoded_samples,
                   frames_to_copy * this->channels * sizeof(float));
        
        frames_decoded += frames_to_copy;
    }
    
    return frames_decoded;
}

unsigned long long AacDecoder::decodeFramesWithMixing(unsigned long long num_frames, 
                                                     float *samples, 
                                                     unsigned int output_channels) {
    std::fill(samples, samples + num_frames * output_channels, 0.0f);
    
    unsigned long long frames_decoded = 0;
    unsigned long long tmp_capacity = tmp_buf.size() / this->channels;
    
    while (frames_decoded < num_frames) {
        unsigned long long frames_this_iteration = std::min(num_frames - frames_decoded, tmp_capacity);
        unsigned long long got = decodeFramesDirect(frames_this_iteration, tmp_buf.data());
        
        if (got == 0) {
            break;
        }
        
        mixChannels(got, 
                   tmp_buf.data(), 
                   this->channels,
                   samples + frames_decoded * output_channels, 
                   output_channels);
        
        frames_decoded += got;
    }
    
    return frames_decoded;
}

int AacDecoder::getSr() {
    return static_cast<int>(this->sr);
}

int AacDecoder::getChannels() {
    return static_cast<int>(this->channels);
}

AudioFormat AacDecoder::getFormat() {
    return AudioFormat::Aac;
}

void AacDecoder::seekPcm(unsigned long long /* pos */) {
    throw Error("Seeking not supported for AAC files");
}

bool AacDecoder::supportsSeek() {
    return false;
}

bool AacDecoder::supportsSampleAccurateSeek() {
    return false;
}

unsigned long long AacDecoder::getLength() {
    return this->frame_count;
}

} // namespace aac_detail

std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream) {
    try {
        auto decoder = std::make_shared<aac_detail::AacDecoder>(stream);
        return decoder;
    } catch (const std::exception &) {
        return nullptr;
    }
}

} // namespace synthizer