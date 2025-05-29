// File: aac.cpp - Riscritto manualmente

#include "synthizer/decoders/aac.hpp" 
#include "synthizer/byte_stream.hpp"
#include "synthizer/config.hpp"
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include "synthizer/logging.hpp"

#include <vector>       // Standard C++
#include <string>       // Standard C++
#include <cstring>      // Standard C++
#include <algorithm>    // Standard C++

extern "C" {
#include <neaacdec.h>   // FAAD2 Header
}

#ifndef SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE
#define SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE 4096
#endif

// Byte minimi che ci aspettiamo di leggere per tentare un'inizializzazione valida di FAAD2.
// È un'euristica; gli header ADTS sono piccoli, ma NeAACDecInit potrebbe aver bisogno di più contesto.
#ifndef MIN_BYTES_FOR_AAC_INIT
#define MIN_BYTES_FOR_AAC_INIT 64
#endif

// Se CHANNELS_MAX non è in config.hpp o constants.hpp, dovrai trovarlo o definirlo.
// Per ora, se non è definito, usiamo un placeholder.
#ifndef CHANNELS_MAX
#define CHANNELS_MAX 8 // Placeholder ragionevole, ma usa quello di Synthizer se possibile!
#endif


namespace synthizer {

namespace aac_detail {

// Callback per FAAD2 per leggere dati dal ByteStream
long faad_read_callback(void *user_data, void *buffer, unsigned long bytes_to_read) {
    ByteStream *stream = static_cast<ByteStream *>(user_data);
    if (!stream) {
        return -1; // Condizione di errore
    }
    // stream->read restituisce unsigned long long, la callback si aspetta long.
    // Assicurati che la conversione sia sicura per i tuoi casi d'uso.
    // Se bytes_to_read è molto grande, questo cast potrebbe troncare su sistemi a 32 bit per 'long'.
    // Tuttavia, 'bytes_to_read' è unsigned long, quindi dovrebbe andare bene.
    return static_cast<long>(stream->read(bytes_to_read, static_cast<char *>(buffer)));
}

// Callback per FAAD2 per posizionarsi nel ByteStream
long faad_seek_callback(void *user_data, unsigned long long offset, int whence) {
    ByteStream *stream = static_cast<ByteStream *>(user_data);
    if (!stream || !stream->supportsSeek()) {
        return -1; // Errore o non supportato
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
        if (stream_len <= 0 && offset > 0) { // Non si può cercare relativo alla fine se la lunghezza è sconosciuta e l'offset è positivo
             // se offset è 0 o negativo potrebbe avere senso in alcuni contesti, ma FAAD2 probabilmente non lo usa così
            return -1;
        }
        target_pos = stream_len + static_cast<long long>(offset); // offset può essere negativo
        break;
    default:
        return -1; // whence non valido
    }
    
    // Controlla se target_pos è valido.
    // Se la lunghezza dello stream è nota, target_pos non deve superarla.
    // target_pos non deve essere negativo.
    if (target_pos < 0 || (stream_len > 0 && target_pos > stream_len) ) {
         // FAAD2 si aspetta 0 per successo, non-zero per errore.
        return -1;
    }
    
    stream->seek(static_cast<unsigned long long>(target_pos));
    return 0; // Successo
}

class AacDecoder : public AudioDecoder {
public:
    AacDecoder(std::shared_ptr<LookaheadByteStream> stream_ptr) :
        stream(stream_ptr),
        decoder_handle(nullptr),
        channels(0),
        sample_rate(0),
        frame_count(0), // I flussi AAC raw spesso non hanno un conteggio totale di frame negli header
        current_frame_data(nullptr) {

        decoder_handle = NeAACDecOpen();
        if (!decoder_handle) {
            throw Error("Impossibile aprire il decoder FAAD2: NeAACDecOpen() fallito.");
        }

        // Configura FAAD2 per restituire campioni float
        NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(decoder_handle);
        if (!config) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Fallimento nel recuperare la configurazione FAAD2.");
        }
        config->outputFormat = FAAD_FMT_FLOAT;
        // NB: NeAACDecSetConfiguration restituisce 1 per successo, 0 per fallimento.
        if (NeAACDecSetConfiguration(decoder_handle, config) == 0) { // 0 indica fallimento
             NeAACDecClose(decoder_handle);
             decoder_handle = nullptr;
             throw Error("Fallimento nell'impostare la configurazione FAAD2 (formato output a float).");
        }
        
        // --- Logica di Inizializzazione Adattata per LookaheadByteStream ---
        std::vector<unsigned char> init_buf_vector(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE);
        unsigned long long actual_bytes_read_for_init = 0;

        actual_bytes_read_for_init = stream->read(SYNTHIZER_CONFIG_AAC_INIT_BUFFER_SIZE, reinterpret_cast<char*>(init_buf_vector.data()));

        if (actual_bytes_read_for_init < MIN_BYTES_FOR_AAC_INIT) {
            stream->reset(); 
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Stream AAC troppo corto per inizializzare: letti solo " + std::to_string(actual_bytes_read_for_init) + " bytes.");
        }

        unsigned long sr_long = 0;
        unsigned char ch_uchar = 0;
        long bytes_consumed_by_init = NeAACDecInit(decoder_handle, init_buf_vector.data(), static_cast<unsigned long>(actual_bytes_read_for_init), &sr_long, &ch_uchar);

        if (bytes_consumed_by_init < 0) {
            stream->reset();
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Fallimento inizializzazione decoder AAC (NeAACDecInit): FAAD2 error code " + std::to_string(bytes_consumed_by_init));
        }

        stream->reset(); // Torna all'inizio dello stream

        if (bytes_consumed_by_init > 0) {
            std::vector<char> dummy_skip_buffer(bytes_consumed_by_init); // Deve essere grande quanto bytes_consumed_by_init
            if (static_cast<unsigned long>(bytes_consumed_by_init) > dummy_skip_buffer.capacity()) {
                 // Questo non dovrebbe succedere se il vector è creato con la dimensione corretta.
                 // Ma per sicurezza, gestiamo il caso in cui bytes_consumed_by_init fosse enorme.
                NeAACDecClose(decoder_handle);
                decoder_handle = nullptr;
                throw Error("Errore interno: bytes_consumed_by_init troppo grande per il buffer di skip.");
            }
            unsigned long long skipped_count = stream->read(static_cast<unsigned long long>(bytes_consumed_by_init), dummy_skip_buffer.data());
            
            if (skipped_count != static_cast<unsigned long long>(bytes_consumed_by_init)) {
                NeAACDecClose(decoder_handle);
                decoder_handle = nullptr;
                throw Error("Errore stream AAC: fallito il salto dei byte dell'header consumati. Attesi " +
                            std::to_string(bytes_consumed_by_init) + ", saltati " + std::to_string(skipped_count));
            }
        }

        this->sample_rate = static_cast<int>(sr_long);
        this->channels = static_cast<int>(ch_uchar);

        if (this->sample_rate == 0 || this->channels == 0) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
            throw Error("Parametri stream AAC (sample rate/channels) non validi dopo init.");
        }

        // Imposta le callback di FAAD2 dopo che lo stream è posizionato correttamente
        NeAACDecCallbacks faad_callbacks_struct;
        std::memset(&faad_callbacks_struct, 0, sizeof(faad_callbacks_struct));
        faad_callbacks_struct.read_callback = faad_read_callback;
        faad_callbacks_struct.seek_callback = faad_seek_callback;
        faad_callbacks_struct.user_data = stream.get(); // Passa il puntatore al ByteStream
        NeAACDecSetCallbacks(decoder_handle, &faad_callbacks_struct);
        // --- Fine Logica di Inizializzazione Adattata ---

        // Stima di frame_count (opzionale, spesso 0 per AAC raw)
        if (stream->supportsSeek() && stream->getLength() > 0 && this->sample_rate > 0 && this->channels > 0) {
            // Potresti provare una stima MOLTO approssimativa qui se necessario,
            // ma per AAC raw è difficile senza analizzare l'intero stream o avere metadati.
            // Lasciamo this->frame_count = 0 (sconosciuto) per ora.
        }
    }

    ~AacDecoder() override {
        if (decoder_handle) {
            NeAACDecClose(decoder_handle);
            decoder_handle = nullptr;
        }
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num_frames_to_write, float *output_samples, unsigned int channels_req = 0) override {
        if (!decoder_handle || num_frames_to_write == 0) return 0;

        unsigned int ch_out = (channels_req < 1 || channels_req > CHANNELS_MAX) ? this->channels : channels_req;
        if (this->channels == 0) return 0; // Non dovremmo arrivare qui se l'init è andato a buon fine

        unsigned long long frames_written_total = 0;
        NeAACDecFrameInfo frame_info; // Spostato fuori dal loop per riutilizzo

        while (frames_written_total < num_frames_to_write) {
            // Decodifica un frame. FAAD2 userà la read_callback internamente.
            current_frame_data = NeAACDecDecode(decoder_handle, &frame_info, nullptr, 0);

            if (frame_info.error > 0) {
                // Usa logDebug o un logging più appropriato per Synthizer
                logDebug("Errore decodifica FAAD2: %s (bytes consumati: %lu, campioni: %lu)\n", 
                         NeAACDecGetErrorMessage(frame_info.error), frame_info.bytesconsumed, frame_info.samples);
                // Considera se questo è un errore fatale. Per ora, interrompi la decodifica su errore.
                break;
            }

            if (current_frame_data == nullptr || frame_info.samples == 0) {
                // Fine dello stream o nessun campione prodotto (potrebbe essere un errore non segnalato da frame_info.error)
                break;
            }
            
            unsigned long long frames_in_this_faad_frame = frame_info.samples;
            // frame_info.channels dovrebbe corrispondere a this->channels se tutto è coerente
            unsigned int channels_in_faad_frame = frame_info.channels; 
            if (channels_in_faad_frame == 0) break; // Frame vuoto o errore

            float* input_ptr = static_cast<float*>(current_frame_data);

            unsigned long long frames_to_copy_this_iteration = std::min(frames_in_this_faad_frame, num_frames_to_write - frames_written_total);

            for (unsigned long long f = 0; f < frames_to_copy_this_iteration; ++f) {
                for (unsigned int c_in = 0; c_in < channels_in_faad_frame; ++c_in) {
                    if (c_in < ch_out) {
                        output_samples[(frames_written_total + f) * ch_out + c_in] = *input_ptr;
                    }
                    input_ptr++; 
                }
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
    AudioFormat getFormat() override { 
        // Assicurati che AudioFormat::Float32 esista nel tuo enum AudioFormat
        // Se non esiste, dovrai usare il membro corretto o AudioFormat::Unknown
        return AudioFormat::Unknown; 
    }

    void seekPcm(unsigned long long pcm_frame_pos) override {
        if (!decoder_handle || !stream->supportsSeek()) {
            throw Error("Seek PCM AAC non supportato o stream non seekable.");
        }
        // Il seek PCM accurato per AAC raw è complesso. FAAD2 non lo supporta direttamente.
        logDebug("AacDecoder::seekPcm chiamato, ma il seek PCM accurato non è implementato per AAC raw.");
        throw Error("Seek PCM accurato non implementato per questo decoder AAC.");
    }

    bool supportsSeek() override { return stream->supportsSeek(); }
    bool supportsSampleAccurateSeek() override { return false; } 
    unsigned long long getLength() override { return frame_count; } // Spesso sconosciuto (0) per AAC raw

private:
    std::shared_ptr<LookaheadByteStream> stream;
    NeAACDecHandle decoder_handle;
    int channels;
    int sample_rate;
    unsigned long long frame_count;

    void* current_frame_data; 
};

} // namespace aac_detail

std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream) {
    if (!stream) {
        return nullptr;
    }

    unsigned char header_check_buffer[4] = {0}; // Leggi fino a 4 byte per ADIF
    // Leggiamo i byte per l'identificazione. stream->read avanza la posizione.
    long long actual_read = stream->read(4, reinterpret_cast<char*>(header_check_buffer));

    bool looks_like_aac = false;
    if (actual_read >= 2) { // Minimo per ADTS
        // Controllo ADTS: syncword 0xFFF (primi 12 bit a 1)
        // header_check_buffer[0] == 0xFF
        // (header_check_buffer[1] & 0xF0) == 0xF0
        // Alcuni controlli più specifici (es. (header_check_buffer[1] & 0xF6) == 0xF0 per MPEG-4)
        // ma per ora un controllo generico ADTS è sufficiente.
        if (header_check_buffer[0] == 0xFF && (header_check_buffer[1] & 0xF0) == (unsigned char)0xF0) {
            looks_like_aac = true;
        }
    }
    if (!looks_like_aac && actual_read == 4) { // Controllo ADIF se non è ADTS e abbiamo letto 4 byte
        if (std::memcmp(header_check_buffer, "ADIF", 4) == 0) {
            looks_like_aac = true;
        }
    }

    // IMPORTANTE: Resetta lo stream INDIPENDENTEMENTE dal risultato del controllo,
    // in modo che il decoder (o il prossimo tentativo di decodifica) inizi dall'inizio.
    stream->reset();

    if (!looks_like_aac) {
        return nullptr; // Non riconosciuto come AAC
    }

    try {
        return std::make_shared<aac_detail::AacDecoder>(stream);
    } catch (const std::exception &e) {
        logDebug("Decoder AAC: Fallimento creazione AacDecoder: %s", e.what());
        return nullptr;
    } catch (...) {
        logDebug("Decoder AAC: Errore sconosciuto durante creazione AacDecoder.");
        return nullptr;
    }
}

} // namespace synthizer