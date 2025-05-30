// File: aac.cpp - Versione con fix per eliminare i pop audio

#include "synthizer/decoders/aac.hpp"
#include "synthizer/byte_stream.hpp"
#include "synthizer/config.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include "synthizer/logging.hpp"

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <memory>
#include <cmath>

extern "C" {
#include <neaacdec.h>
}

#ifndef SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE
#define SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE 4096
#endif

#ifndef MIN_BYTES_FOR_AAC_INIT
#define MIN_BYTES_FOR_AAC_INIT 64
#endif

namespace synthizer {
namespace aac_detail {

static constexpr unsigned int INTERNAL_DECODER_CHANNELS_REQUEST_MAX = 32;
static constexpr unsigned int AAC_INPUT_BUFFER_CAPACITY = 32768;
static constexpr unsigned int FADE_SAMPLES = 64; // Campioni per fade in/out
static constexpr unsigned int MAX_CONSECUTIVE_ERRORS = 3;
static constexpr float SILENCE_THRESHOLD = 1e-6f;

class AacDecoder : public AudioDecoder {
private:
    std::shared_ptr<LookaheadByteStream> stream;
    NeAACDecHandle decoder_handle;
    int channels;
    int sample_rate;
    unsigned long long frame_count;
    void* current_frame_data;

    std::vector<unsigned char> internal_input_buffer;
    unsigned long current_valid_bytes_in_buffer;
    bool stream_at_eos;
    
    // Variabili per la gestione dei pop audio
    std::vector<float> last_frame_samples; // Ultimi campioni per continuità
    std::vector<float> fade_buffer; // Buffer per fade in/out
    bool need_fade_in;
    bool decoder_recovering;
    int consecutive_decode_errors;
    unsigned long long total_frames_decoded;
    
    // Filtro passa-alto per rimuovere DC offset
    std::vector<float> dc_filter_x1, dc_filter_y1;
    static constexpr float DC_FILTER_ALPHA = 0.995f;

public:
    AacDecoder(std::shared_ptr<LookaheadByteStream> stream_ptr) :
        stream(stream_ptr),
        decoder_handle(nullptr),
        channels(0),
        sample_rate(0),
        frame_count(0),
        current_frame_data(nullptr),
        current_valid_bytes_in_buffer(0),
        stream_at_eos(false),
        need_fade_in(true),
        decoder_recovering(false),
        consecutive_decode_errors(0),
        total_frames_decoded(0) {

        internal_input_buffer.resize(AAC_INPUT_BUFFER_CAPACITY);

        decoder_handle = NeAACDecOpen();
        if (!decoder_handle) {
            throw Error("Impossibile aprire il decoder FAAD2: NeAACDecOpen() fallito.");
        }

        NeAACDecConfigurationPtr config_ptr = NeAACDecGetCurrentConfiguration(decoder_handle);
        if (!config_ptr) {
            NeAACDecClose(decoder_handle);
            throw Error("Fallimento nel recuperare la configurazione FAAD2.");
        }
        
        // Configurazione ottimizzata per ridurre i pop
        config_ptr->outputFormat = FAAD_FMT_FLOAT;
        config_ptr->downMatrix = 0; // Non fare downmix automatico
        config_ptr->useOldADTSFormat = 0;
        config_ptr->dontUpSampleImplicitSBR = 1; // Evita upsampling che può causare pop
        
        if (NeAACDecSetConfiguration(decoder_handle, config_ptr) == 0) {
            NeAACDecClose(decoder_handle);
            throw Error("Fallimento nell'impostare la configurazione FAAD2 (output a float).");
        }

        std::vector<unsigned char> init_temp_buffer(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE);
        unsigned long long actual_read_for_init = stream->read(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE, reinterpret_cast<char*>(init_temp_buffer.data()));

        if (actual_read_for_init < MIN_BYTES_FOR_AAC_INIT) {
            stream->reset();
            NeAACDecClose(decoder_handle);
            throw Error("Stream AAC troppo corto per inizializzare: letti solo " + std::to_string(actual_read_for_init) + " bytes.");
        }

        unsigned long sr_long = 0;
        unsigned char ch_uchar = 0;
        long bytes_consumed_by_init = NeAACDecInit(decoder_handle, init_temp_buffer.data(), static_cast<unsigned long>(actual_read_for_init), &sr_long, &ch_uchar);

        if (bytes_consumed_by_init < 0) {
            stream->reset();
            NeAACDecClose(decoder_handle);
            throw Error("Fallimento inizializzazione decoder AAC (NeAACDecInit): FAAD2 error code " + std::to_string(bytes_consumed_by_init));
        }

        stream->reset();

        if (bytes_consumed_by_init > 0) {
            std::vector<char> dummy_skip_buffer(bytes_consumed_by_init);
            unsigned long long skipped_count = stream->read(static_cast<unsigned long long>(bytes_consumed_by_init), dummy_skip_buffer.data());
            if (skipped_count != static_cast<unsigned long long>(bytes_consumed_by_init)) {
                NeAACDecClose(decoder_handle);
                throw Error("Errore stream AAC: fallito il salto dei byte dell'header consumati. Attesi " +
                            std::to_string(bytes_consumed_by_init) + ", saltati " + std::to_string(skipped_count));
            }
        }

        this->sample_rate = static_cast<int>(sr_long);
        this->channels = static_cast<int>(ch_uchar);

        if (this->sample_rate == 0 || this->channels == 0) {
            NeAACDecClose(decoder_handle);
            throw Error("Parametri stream AAC (sample rate/channels) non validi dopo init.");
        }
        
        // Inizializza i buffer per la gestione dei pop
        last_frame_samples.resize(this->channels, 0.0f);
        fade_buffer.resize(FADE_SAMPLES * this->channels, 0.0f);
        dc_filter_x1.resize(this->channels, 0.0f);
        dc_filter_y1.resize(this->channels, 0.0f);
    }

    ~AacDecoder() override {
        if (decoder_handle) {
            NeAACDecClose(decoder_handle);
        }
    }

private:
    // Applica fade in per evitare pop all'inizio
    void applyFadeIn(float* samples, unsigned long long num_frames, unsigned int channels_count) {
        unsigned long long fade_frames = std::min(static_cast<unsigned long long>(FADE_SAMPLES), num_frames);
        for (unsigned long long f = 0; f < fade_frames; ++f) {
            float fade_factor = static_cast<float>(f) / static_cast<float>(FADE_SAMPLES);
            for (unsigned int c = 0; c < channels_count; ++c) {
                samples[f * channels_count + c] *= fade_factor;
            }
        }
    }
    
    // Applica fade out per evitare pop alla fine
    void applyFadeOut(float* samples, unsigned long long num_frames, unsigned int channels_count) {
        unsigned long long fade_frames = std::min(static_cast<unsigned long long>(FADE_SAMPLES), num_frames);
        unsigned long long start_frame = num_frames - fade_frames;
        for (unsigned long long f = start_frame; f < num_frames; ++f) {
            float fade_factor = static_cast<float>(num_frames - f) / static_cast<float>(FADE_SAMPLES);
            for (unsigned int c = 0; c < channels_count; ++c) {
                samples[f * channels_count + c] *= fade_factor;
            }
        }
    }
    
    // Smooth transition tra frame per evitare discontinuità
    void applySmoothTransition(float* samples, unsigned long long num_frames, unsigned int channels_count) {
        if (num_frames == 0 || last_frame_samples.size() != channels_count) return;
        
        // Controlla se c'è una discontinuità significativa
        bool needs_smoothing = false;
        for (unsigned int c = 0; c < channels_count; ++c) {
            float diff = std::abs(samples[c] - last_frame_samples[c]);
            if (diff > SILENCE_THRESHOLD * 10.0f) { // Soglia per rilevare discontinuità
                needs_smoothing = true;
                break;
            }
        }
        
        if (needs_smoothing) {
            unsigned long long smooth_frames = std::min(static_cast<unsigned long long>(16), num_frames);
            for (unsigned long long f = 0; f < smooth_frames; ++f) {
                float blend_factor = static_cast<float>(f) / static_cast<float>(smooth_frames);
                for (unsigned int c = 0; c < channels_count; ++c) {
                    float current_sample = samples[f * channels_count + c];
                    float smooth_sample = last_frame_samples[c] * (1.0f - blend_factor) + current_sample * blend_factor;
                    samples[f * channels_count + c] = smooth_sample;
                }
            }
        }
    }
    
    // Filtro passa-alto per rimuovere DC offset che può causare pop
    void applyDCFilter(float* samples, unsigned long long num_frames, unsigned int channels_count) {
        for (unsigned long long f = 0; f < num_frames; ++f) {
            for (unsigned int c = 0; c < channels_count && c < dc_filter_x1.size(); ++c) {
                float input = samples[f * channels_count + c];
                float output = input - dc_filter_x1[c] + DC_FILTER_ALPHA * dc_filter_y1[c];
                dc_filter_x1[c] = input;
                dc_filter_y1[c] = output;
                samples[f * channels_count + c] = output;
            }
        }
    }
    
    // Salva gli ultimi campioni per la continuità
    void saveLastSamples(float* samples, unsigned long long num_frames, unsigned int channels_count) {
        if (num_frames > 0 && channels_count == last_frame_samples.size()) {
            unsigned long long last_frame_index = (num_frames - 1) * channels_count;
            for (unsigned int c = 0; c < channels_count; ++c) {
                last_frame_samples[c] = samples[last_frame_index + c];
            }
        }
    }
    
    // Genera silenzio con fade per coprire errori
    void generateSilenceWithFade(float* output, unsigned long long num_frames, unsigned int channels_count) {
        // Riempi con silenzio
        std::fill(output, output + num_frames * channels_count, 0.0f);
        
        // Applica fade in dal last_frame_samples se disponibile
        if (!last_frame_samples.empty() && num_frames > 0) {
            unsigned long long fade_frames = std::min(static_cast<unsigned long long>(FADE_SAMPLES/2), num_frames);
            for (unsigned long long f = 0; f < fade_frames; ++f) {
                float fade_factor = 1.0f - (static_cast<float>(f) / static_cast<float>(fade_frames));
                for (unsigned int c = 0; c < channels_count && c < last_frame_samples.size(); ++c) {
                    output[f * channels_count + c] = last_frame_samples[c] * fade_factor;
                }
            }
        }
    }

public:
    unsigned long long writeSamplesInterleaved(unsigned long long num_frames_to_write, float *output_samples, unsigned int channels_req = 0) override {
        if (!decoder_handle || num_frames_to_write == 0) return 0;

        unsigned int ch_out = (channels_req < 1 || channels_req > INTERNAL_DECODER_CHANNELS_REQUEST_MAX) ? this->channels : channels_req;
        if (this->channels == 0) return 0;

        unsigned long long frames_written_total = 0;
        NeAACDecFrameInfo frame_info;

        // Inizializza a zero l'output
        std::fill(output_samples, output_samples + num_frames_to_write * ch_out, 0.0f);

        while (frames_written_total < num_frames_to_write) {
            // Riempi il buffer se serve
            const unsigned long MIN_DECODE_CHUNK = 2048;
            if (current_valid_bytes_in_buffer < MIN_DECODE_CHUNK && !stream_at_eos) {
                unsigned long bytes_to_read_target = AAC_INPUT_BUFFER_CAPACITY - current_valid_bytes_in_buffer;
                if (bytes_to_read_target > 0) {
                    unsigned long long actually_read_from_stream = stream->read(
                        bytes_to_read_target,
                        reinterpret_cast<char*>(internal_input_buffer.data() + current_valid_bytes_in_buffer)
                    );
                    if (actually_read_from_stream == 0 && bytes_to_read_target > 0) {
                        stream_at_eos = true;
                    }
                    current_valid_bytes_in_buffer += static_cast<unsigned long>(actually_read_from_stream);
                }
            }

            // Se il buffer è vuoto e lo stream è finito, applica fade out e termina
            if (current_valid_bytes_in_buffer == 0 && stream_at_eos) {
                if (frames_written_total > 0) {
                    applyFadeOut(output_samples, frames_written_total, ch_out);
                }
                break;
            }
            if (current_valid_bytes_in_buffer == 0) break;

            current_frame_data = NeAACDecDecode(decoder_handle, &frame_info, internal_input_buffer.data(), current_valid_bytes_in_buffer);

            logDebug("FAAD2: decoded frame: samples=%lu, bytesconsumed=%lu, error=%d, eos=%d",
                     frame_info.samples, frame_info.bytesconsumed, frame_info.error, stream_at_eos);

            // Gestione errori migliorata
            if (frame_info.error > 0) {
                logDebug("Errore decodifica FAAD2: %s (consumati: %lu, campioni: %lu)",
                         NeAACDecGetErrorMessage(frame_info.error), frame_info.bytesconsumed, frame_info.samples);

                consecutive_decode_errors++;
                decoder_recovering = true;
                
                // Per errori minori, inserisci silenzio con fade
                if (consecutive_decode_errors <= MAX_CONSECUTIVE_ERRORS && frames_written_total < num_frames_to_write) {
                    unsigned long long frames_to_fill = std::min(static_cast<unsigned long long>(1024), num_frames_to_write - frames_written_total);
                    generateSilenceWithFade(output_samples + frames_written_total * ch_out, frames_to_fill, ch_out);
                    frames_written_total += frames_to_fill;
                }

                // Gestione buffer dopo errore - più conservativa
                if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= current_valid_bytes_in_buffer) {
                    std::memmove(internal_input_buffer.data(),
                                 internal_input_buffer.data() + frame_info.bytesconsumed,
                                 current_valid_bytes_in_buffer - frame_info.bytesconsumed);
                    current_valid_bytes_in_buffer -= frame_info.bytesconsumed;
                } else {
                    // Su errore grave, salta una piccola quantità di byte
                    unsigned long bytes_to_skip = std::min(static_cast<unsigned long>(256), current_valid_bytes_in_buffer);
                    if (bytes_to_skip > 0) {
                        std::memmove(internal_input_buffer.data(),
                                     internal_input_buffer.data() + bytes_to_skip,
                                     current_valid_bytes_in_buffer - bytes_to_skip);
                        current_valid_bytes_in_buffer -= bytes_to_skip;
                    } else {
                        current_valid_bytes_in_buffer = 0;
                    }
                }
                continue;
            } else {
                // Reset contatore errori su successo
                if (consecutive_decode_errors > 0) {
                    consecutive_decode_errors = 0;
                    decoder_recovering = true; // Mantieni il flag per il prossimo frame valido
                }
            }

            // Se il frame non produce campioni, ma non è errore
            if (current_frame_data == nullptr || frame_info.samples == 0) {
                if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= current_valid_bytes_in_buffer) {
                    std::memmove(internal_input_buffer.data(),
                                 internal_input_buffer.data() + frame_info.bytesconsumed,
                                 current_valid_bytes_in_buffer - frame_info.bytesconsumed);
                    current_valid_bytes_in_buffer -= frame_info.bytesconsumed;
                } else if (frame_info.bytesconsumed > current_valid_bytes_in_buffer) {
                    current_valid_bytes_in_buffer = 0;
                } else if (frame_info.bytesconsumed == 0) {
                    if (stream_at_eos && current_valid_bytes_in_buffer > 0) {
                        current_valid_bytes_in_buffer = 0;
                    }
                }
                continue;
            }

            // Scrivi i campioni decodificati
            if (frame_info.samples > 0 && current_frame_data != nullptr) {
                unsigned long long frames_in_this_faad_output = frame_info.samples / frame_info.channels;
                unsigned int channels_in_faad_output = frame_info.channels;

                float* input_ptr = static_cast<float*>(current_frame_data);
                unsigned long long frames_to_copy_this_iteration = std::min(frames_in_this_faad_output, num_frames_to_write - frames_written_total);

                // Copia i campioni nel buffer temporaneo per l'elaborazione
                std::vector<float> temp_buffer(frames_to_copy_this_iteration * ch_out, 0.0f);
                
                for (unsigned long long f = 0; f < frames_to_copy_this_iteration; ++f) {
                    for (unsigned int c_in = 0; c_in < channels_in_faad_output; ++c_in) {
                        if (c_in < ch_out) {
                            temp_buffer[f * ch_out + c_in] = *input_ptr;
                        }
                        input_ptr++;
                    }
                    // Azzera eventuali canali extra
                    for (unsigned int c_fill = channels_in_faad_output; c_fill < ch_out; ++c_fill) {
                        temp_buffer[f * ch_out + c_fill] = 0.0f;
                    }
                }
                
                // Applica i filtri per eliminare i pop
                applyDCFilter(temp_buffer.data(), frames_to_copy_this_iteration, ch_out);
                
                // Applica smooth transition se necessario
                if (total_frames_decoded > 0) {
                    applySmoothTransition(temp_buffer.data(), frames_to_copy_this_iteration, ch_out);
                }
                
                // Applica fade in se è il primo frame o se stiamo recuperando da errore
                if (need_fade_in || decoder_recovering) {
                    applyFadeIn(temp_buffer.data(), frames_to_copy_this_iteration, ch_out);
                    need_fade_in = false;
                    decoder_recovering = false;
                }
                
                // Copia nel buffer di output
                std::copy(temp_buffer.begin(), temp_buffer.end(), 
                         output_samples + frames_written_total * ch_out);
                
                // Salva gli ultimi campioni per la continuità
                saveLastSamples(temp_buffer.data(), frames_to_copy_this_iteration, ch_out);
                
                frames_written_total += frames_to_copy_this_iteration;
                total_frames_decoded += frames_to_copy_this_iteration;
            }

            // Gestione buffer dopo decodifica
            if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= current_valid_bytes_in_buffer) {
                std::memmove(internal_input_buffer.data(),
                             internal_input_buffer.data() + frame_info.bytesconsumed,
                             current_valid_bytes_in_buffer - frame_info.bytesconsumed);
                current_valid_bytes_in_buffer -= frame_info.bytesconsumed;
            } else if (frame_info.bytesconsumed > current_valid_bytes_in_buffer) {
                logDebug("FAAD2 ha consumato più byte di quelli disponibili.");
                current_valid_bytes_in_buffer = 0;
                break;
            }
        }
        
        return frames_written_total;
    }

    int getSr() override { return sample_rate; }
    int getChannels() override { return channels; }
    AudioFormat getFormat() override { return AudioFormat::Unknown; }

    void seekPcm([[maybe_unused]] unsigned long long pcm_frame_pos) override {
        if (!stream->supportsSeek()) {
            throw Error("Seek PCM AAC non supportato: lo stream non è seekable.");
        }
        logDebug("AacDecoder::seekPcm chiamato, ma il seek PCM accurato è complesso e non pienamente implementato.");
        throw Error("Seek PCM accurato non implementato per questo decoder AAC (richiede reinizializzazione decoder).");
    }

    bool supportsSeek() override { return stream->supportsSeek(); }
    bool supportsSampleAccurateSeek() override { return false; }
    unsigned long long getLength() override { return frame_count; }
};

} // namespace aac_detail

std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream_ptr) {
    if (!stream_ptr) {
        return nullptr;
    }

    unsigned char header_check_buffer[4] = {0};
    long long actual_read = stream_ptr->read(4, reinterpret_cast<char*>(header_check_buffer));

    bool looks_like_aac = false;
    if (actual_read >= 2) {
        if (header_check_buffer[0] == 0xFF && (header_check_buffer[1] & 0xF0) == (unsigned char)0xF0) {
            looks_like_aac = true;
        }
    }
    if (!looks_like_aac && actual_read == 4) {
        if (std::memcmp(header_check_buffer, "ADIF", 4) == 0) {
            looks_like_aac = true;
        }
    }
    stream_ptr->reset();
    if (!looks_like_aac) {
        return nullptr;
    }

    try {
        return std::make_shared<aac_detail::AacDecoder>(stream_ptr);
    } catch (const std::exception &e) {
        logDebug("Decoder AAC: Fallimento creazione AacDecoder: %s", e.what());
        return nullptr;
    } catch (...) {
        logDebug("Decoder AAC: Errore sconosciuto durante creazione AacDecoder.");
        return nullptr;
    }
}

} // namespace synthizer