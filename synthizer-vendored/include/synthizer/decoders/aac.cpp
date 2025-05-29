// File: aac.cpp

// 1. Header di Synthizer (C++)
#include "synthizer/decoders/aac.hpp"
#include "synthizer/byte_stream.hpp"
#include "synthizer/config.hpp"      // Per SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE
#include "synthizer/decoding.hpp"    // Per AudioDecoder, AudioFormat
#include "synthizer/error.hpp"       // Per Error
#include "synthizer/logging.hpp"     // Per logDebug

// 2. Header Standard C++
#include <vector>
#include <string>
#include <cstring>      // Per std::memset, std::memcpy, std::memmove
#include <algorithm>    // Per std::min
#include <memory>       // Per std::shared_ptr, std::make_shared

// 3. Header C (FAAD2)
extern "C" {
#include <neaacdec.h>   // Header per FAAD2
}

// 4. Definizioni e resto del codice

#ifndef SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE
#define SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE 4096
#endif

#ifndef MIN_BYTES_FOR_AAC_INIT
#define MIN_BYTES_FOR_AAC_INIT 64
#endif

// Non usiamo più un CHANNELS_MAX placeholder generico.
// Definiamo un limite specifico per il decoder se necessario.

namespace synthizer {

namespace aac_detail {

// Limite massimo ragionevole per i canali richiesti a questo decoder.
// Miniaudio usa 32 (MA_MAX_CHANNELS). Possiamo allinearci o scegliere un valore diverso.
static constexpr unsigned int INTERNAL_DECODER_CHANNELS_REQUEST_MAX = 32;

class AacDecoder : public AudioDecoder {
private:
    std::shared_ptr<LookaheadByteStream> stream;
    NeAACDecHandle decoder_handle;
    int channels; // Canali effettivi decodificati
    int sample_rate;
    unsigned long long frame_count;
    void* current_frame_data;

    std::vector<unsigned char> internal_input_buffer;
    unsigned long current_valid_bytes_in_buffer;
    bool stream_at_eos;
    static constexpr unsigned int AAC_INPUT_BUFFER_CAPACITY = 8192;

public:
    AacDecoder(std::shared_ptr<LookaheadByteStream> stream_ptr) :
        stream(stream_ptr),
        decoder_handle(nullptr),
        channels(0),
        sample_rate(0),
        frame_count(0),
        current_frame_data(nullptr),
        current_valid_bytes_in_buffer(0),
        stream_at_eos(false) {

        internal_input_buffer.resize(AAC_INPUT_BUFFER_CAPACITY);

        decoder_handle = NeAACDecOpen();
        if (!decoder_handle) {
            throw Error("Impossibile aprire il decoder FAAD2: NeAACDecOpen() fallito.");
        }

        NeAACDecConfigurationPtr config_ptr = NeAACDecGetCurrentConfiguration(decoder_handle);
        if (!config_ptr) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr; 
            throw Error("Fallimento nel recuperare la configurazione FAAD2.");
        }
        config_ptr->outputFormat = FAAD_FMT_FLOAT;
        if (NeAACDecSetConfiguration(decoder_handle, config_ptr) == 0) { 
             NeAACDecClose(decoder_handle);
             decoder_handle = nullptr;
             throw Error("Fallimento nell'impostare la configurazione FAAD2 (output a float).");
        }
        
        std::vector<unsigned char> init_temp_buffer(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE);
        unsigned long long actual_read_for_init = stream->read(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE, reinterpret_cast<char*>(init_temp_buffer.data()));

        if (actual_read_for_init < MIN_BYTES_FOR_AAC_INIT) {
            stream->reset(); 
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Stream AAC troppo corto per inizializzare: letti solo " + std::to_string(actual_read_for_init) + " bytes.");
        }

        unsigned long sr_long = 0;
        unsigned char ch_uchar = 0;
        long bytes_consumed_by_init = NeAACDecInit(decoder_handle, init_temp_buffer.data(), static_cast<unsigned long>(actual_read_for_init), &sr_long, &ch_uchar);

        if (bytes_consumed_by_init < 0) {
            stream->reset();
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Fallimento inizializzazione decoder AAC (NeAACDecInit): FAAD2 error code " + std::to_string(bytes_consumed_by_init));
        }

        stream->reset(); 

        if (bytes_consumed_by_init > 0) {
            std::vector<char> dummy_skip_buffer(bytes_consumed_by_init);
            unsigned long long skipped_count = stream->read(static_cast<unsigned long long>(bytes_consumed_by_init), dummy_skip_buffer.data());
            
            if (skipped_count != static_cast<unsigned long long>(bytes_consumed_by_init)) {
                NeAACDecClose(decoder_handle);
                decoder_handle = nullptr;
                throw Error("Errore stream AAC: fallito il salto dei byte dell'header consumati. Attesi " +
                            std::to_string(bytes_consumed_by_init) + ", saltati " + std::to_string(skipped_count));
            }
        }

        this->sample_rate = static_cast<int>(sr_long);
        this->channels = static_cast<int>(ch_uchar); // Canali effettivi del file

        if (this->sample_rate == 0 || this->channels == 0) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Parametri stream AAC (sample rate/channels) non validi dopo init.");
        }
    }

    ~AacDecoder() override {
        if (decoder_handle) {
            NeAACDecClose(decoder_handle);
        }
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num_frames_to_write, float *output_samples, unsigned int channels_req = 0) override {
        if (!decoder_handle || num_frames_to_write == 0) return 0;

        // Usa la costante definita localmente per validare channels_req
        unsigned int ch_out = (channels_req < 1 || channels_req > INTERNAL_DECODER_CHANNELS_REQUEST_MAX) ? this->channels : channels_req;
        
        if (this->channels == 0) return 0; 

        unsigned long long frames_written_total = 0;
        NeAACDecFrameInfo frame_info; 

        while (frames_written_total < num_frames_to_write) {
            if (current_valid_bytes_in_buffer < 1024 && !stream_at_eos) { 
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

            if (current_valid_bytes_in_buffer == 0) {
                break; 
            }

            current_frame_data = NeAACDecDecode(decoder_handle, &frame_info, internal_input_buffer.data(), current_valid_bytes_in_buffer);

            // Gestione errori e byte consumati
            bool processed_consumed_bytes_this_iteration = false;
            if (frame_info.error > 0) {
                logDebug("Errore decodifica FAAD2: %s (consumati: %lu, campioni: %lu)", 
                         NeAACDecGetErrorMessage(frame_info.error), frame_info.bytesconsumed, frame_info.samples);
                // Consuma i byte anche in caso di errore per avanzare
                if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= current_valid_bytes_in_buffer) {
                    std::memmove(internal_input_buffer.data(),
                                 internal_input_buffer.data() + frame_info.bytesconsumed,
                                 current_valid_bytes_in_buffer - frame_info.bytesconsumed);
                    current_valid_bytes_in_buffer -= frame_info.bytesconsumed;
                } else if (frame_info.bytesconsumed > current_valid_bytes_in_buffer) {
                    current_valid_bytes_in_buffer = 0; 
                } else { // Errore ma 0 byte consumati, svuota buffer per evitare loop
                    current_valid_bytes_in_buffer = 0;
                }
                processed_consumed_bytes_this_iteration = true;
                break; 
            }

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
                    } else if (!stream_at_eos && current_valid_bytes_in_buffer > 0) {
                        logDebug("FAAD2: Nessun campione, nessun byte consumato, dati nel buffer. Svuoto buffer.");
                        current_valid_bytes_in_buffer = 0;
                    }
                }
                processed_consumed_bytes_this_iteration = true;
                if (stream_at_eos && current_valid_bytes_in_buffer == 0) break; 
                if (frame_info.samples == 0) continue; 
            }
            
            // Processa campioni audio
            if (frame_info.samples > 0 && current_frame_data != nullptr) {
                unsigned long long frames_in_this_faad_output = frame_info.samples;
                unsigned int channels_in_faad_output = frame_info.channels; 

                if (channels_in_faad_output > 0) {
                    float* input_ptr = static_cast<float*>(current_frame_data);
                    unsigned long long frames_to_copy_this_iteration = std::min(frames_in_this_faad_output, num_frames_to_write - frames_written_total);

                    for (unsigned long long f = 0; f < frames_to_copy_this_iteration; ++f) {
                        for (unsigned int c_in = 0; c_in < channels_in_faad_output; ++c_in) {
                            if (c_in < ch_out) {
                                output_samples[(frames_written_total + f) * ch_out + c_in] = *input_ptr;
                            }
                            input_ptr++; 
                        }
                        for (unsigned int c_fill = channels_in_faad_output; c_fill < ch_out; ++c_fill) {
                            output_samples[(frames_written_total + f) * ch_out + c_fill] = 0.0f;
                        }
                    }
                    frames_written_total += frames_to_copy_this_iteration;
                }
            }

            // Aggiorna buffer di input (solo se non già gestito sopra)
            if (!processed_consumed_bytes_this_iteration) {
                if (frame_info.bytesconsumed > 0 && frame_info.bytesconsumed <= current_valid_bytes_in_buffer) {
                    std::memmove(internal_input_buffer.data(),
                                 internal_input_buffer.data() + frame_info.bytesconsumed,
                                 current_valid_bytes_in_buffer - frame_info.bytesconsumed);
                    current_valid_bytes_in_buffer -= frame_info.bytesconsumed;
                } else if (frame_info.bytesconsumed > current_valid_bytes_in_buffer) {
                    logDebug("FAAD2 ha consumato più byte di quelli disponibili (successo).");
                    current_valid_bytes_in_buffer = 0;
                    break; 
                } else if (frame_info.bytesconsumed == 0 && frame_info.samples > 0) { 
                    logDebug("FAAD2 ha prodotto campioni ma 0 byte consumati (successo). Invalido buffer.");
                    current_valid_bytes_in_buffer = 0; 
                }
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