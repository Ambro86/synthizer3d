// File: aac.cpp

#include "synthizer/decoders/aac.hpp" // O il nome del tuo header per il decoder AAC
#include "synthizer/byte_stream.hpp"
#include "synthizer/config.hpp"
#include "synthizer/decoding.hpp" // Per AudioFormat::Raw
#include "synthizer/error.hpp"
#include "synthizer/logging.hpp"

// Headers FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h> // Per AV_CH_LAYOUT_*
#include <libavutil/opt.h>          // Per av_opt_set_int
// Potrebbe servire per conversioni di formato più complesse, ma iniziamo senza
// #include <libswresample/swresample.h>
}

#include <vector>
#include <string>
#include <algorithm> // Per std::min

// Dimensioni del buffer per l'AVIOContext
#define FFMPEG_IO_BUFFER_SIZE 32768

namespace synthizer {
namespace aac_detail { // O il tuo namespace appropriato

// Struttura per passare il ByteStream di Synthizer all'AVIOContext di FFmpeg
struct CustomIoContext {
    LookaheadByteStream *stream;
    // Potrebbe essere necessario tenere traccia della posizione se lo stream non lo fa in modo affidabile per FFmpeg
    // int64_t current_pos; 
};

// Callback di lettura per FFmpeg che usa il nostro ByteStream
static int read_packet_callback(void *opaque, uint8_t *buf, int buf_size) {
    CustomIoContext *custom_io = static_cast<CustomIoContext*>(opaque);
    if (!custom_io || !custom_io->stream) {
        return AVERROR_EXTERNAL; // Errore
    }

    // logDebug("FFMPEG_IO: read_packet_callback richiesti %d byte", buf_size);
    long long bytes_read = custom_io->stream->read(buf_size, reinterpret_cast<char*>(buf));
    // logDebug("FFMPEG_IO: letti %lld byte", bytes_read);

    if (bytes_read < 0) { // Errore di stream
        return AVERROR_EXTERNAL; 
    }
    if (bytes_read == 0) { // Fine dello stream (EOF)
        return AVERROR_EOF;
    }
    return static_cast<int>(bytes_read);
}

// Callback di seek per FFmpeg che usa il nostro ByteStream
static int64_t seek_callback(void *opaque, int64_t offset, int whence) {
    CustomIoContext *custom_io = static_cast<CustomIoContext*>(opaque);
    if (!custom_io || !custom_io->stream) {
        return -1; // Errore, FFmpeg si aspetta un valore negativo
    }
    
    // logDebug("FFMPEG_IO: seek_callback offset %lld, whence %d", offset, whence);

    if (!custom_io->stream->supportsSeek()) {
        // logDebug("FFMPEG_IO: seek non supportato dallo stream");
        return -1; // O AVERROR(ENOSYS) se preferisci segnalare "non implementato"
    }

    // 'whence' in FFmpeg:
    // AVSEEK_SIZE: restituisce la dimensione dello stream (se nota).
    // SEEK_SET, SEEK_CUR, SEEK_END: come fseek.
    
    if (whence == AVSEEK_SIZE) {
        // Se il tuo ByteStream può restituire la lunghezza totale
        // return custom_io->stream->getLength(); // Assumendo che esista getLength()
        // Altrimenti, se non conosci la dimensione, restituisci un errore o -1
        // logDebug("FFMPEG_IO: AVSEEK_SIZE non supportato");
        return -1; 
    }

    // Converti whence da FFmpeg a quello che si aspetta il tuo ByteStream (es. stdio SEEK_SET, ecc.)
    // Questo dipende dall'API del tuo ByteStream::seek.
    // Qui assumiamo che ByteStream::seek usi una logica simile a fseek.
    // ByteStream::seek(offset, whence_stdio_style)
    // Se ByteStream::seek restituisce la nuova posizione, va bene.
    // Altrimenti, potrebbe essere necessario usare ByteStream::tell() dopo il seek.
    
    // Esempio di mappatura (da adattare al tuo ByteStream::seek)
    // int stream_whence;
    // if (whence == SEEK_SET) stream_whence = 0; // O il valore per SEEK_SET del tuo stream
    // else if (whence == SEEK_CUR) stream_whence = 1; // O il valore per SEEK_CUR
    // else if (whence == SEEK_END) stream_whence = 2; // O il valore per SEEK_END
    // else {
    //    logDebug("FFMPEG_IO: whence non supportato: %d", whence);
    //    return -1;
    // }

    // bool success = custom_io->stream->seek(offset); // Adatta alla tua API di seek
    // if (!success) {
    //    logDebug("FFMPEG_IO: seek fallito nello stream");
    //    return -1;
    // }
    // return custom_io->stream->tell(); // Restituisce la nuova posizione
    
    // Placeholder: Il seek è complesso da mappare correttamente senza conoscere l'API esatta di ByteStream::seek
    // Per ora, lo implementiamo in modo che fallisca, così FFmpeg non tenterà seek aggressivi
    // che potrebbero non funzionare.
    // Dovrai implementare questo correttamente se vuoi il supporto al seek.
    logWarning("FFMPEG_IO: seek_callback non completamente implementato per questo ByteStream.");
    return -1; // Indica fallimento o non supportato
}


class FFmpegAacDecoder : public AudioDecoder {
private:
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    AVIOContext *avio_ctx = nullptr;
    uint8_t *io_buffer = nullptr; // Buffer per AVIOContext
    CustomIoContext custom_io_ctx_data; // Contiene il puntatore al ByteStream

    AVPacket *packet = nullptr;
    AVFrame *decoded_frame = nullptr;

    int audio_stream_index = -1;
    int channels = 0;
    int sample_rate = 0;
    AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;

    // Buffer per campioni interleaved, se FFmpeg produce planare
    std::vector<float> interleaved_buffer;
    unsigned int interleaved_buffer_frames_ready = 0;
    unsigned int interleaved_buffer_pos = 0;


public:
    FFmpegAacDecoder(std::shared_ptr<LookaheadByteStream> stream_ptr) {
        if (!stream_ptr) {
            throw Error("FFmpegAacDecoder: stream_ptr nullo.");
        }

        // Inizializza il contesto I/O personalizzato
        custom_io_ctx_data.stream = stream_ptr.get();
        // custom_io_ctx_data.current_pos = 0; // Se necessario

        // Alloca il buffer per l'I/O personalizzato di FFmpeg
        io_buffer = static_cast<uint8_t*>(av_malloc(FFMPEG_IO_BUFFER_SIZE));
        if (!io_buffer) {
            throw Error("FFmpegAacDecoder: Impossibile allocare io_buffer.");
        }

        // Crea l'AVIOContext usando il buffer e le callback
        // L'ultimo argomento '0' per write_flag indica che è read-only
        avio_ctx = avio_alloc_context(io_buffer, FFMPEG_IO_BUFFER_SIZE, 0, 
                                      &custom_io_ctx_data, read_packet_callback, 
                                      nullptr, // Non implementiamo write_packet_callback
                                      seek_callback); 
        if (!avio_ctx) {
            av_free(io_buffer); // Libera il buffer se avio_alloc_context fallisce
            io_buffer = nullptr;
            throw Error("FFmpegAacDecoder: Impossibile allocare avio_ctx.");
        }
        // Evita che FFmpeg liberi il nostro buffer esterno quando avio_ctx viene liberato
        // avio_ctx->buffer = nullptr; // No, questo è sbagliato. FFmpeg gestisce il buffer allocato con av_malloc
                                  // se avio_alloc_context lo ha preso.
                                  // Il buffer 'io_buffer' deve essere liberato solo se avio_alloc_context fallisce
                                  // o nel distruttore se avio_ctx non lo fa.
                                  // In realtà, avio_context_free si aspetta che il buffer sia parte di esso.
                                  // La gestione corretta è liberare avio_ctx->buffer e poi avio_ctx.
                                  // Ma se usiamo il nostro buffer esterno, dobbiamo essere cauti.
                                  // La documentazione dice: "buffer: Buffer for input/output.
                                  // If NULL, AVIOContext will allocate its own buffer.
                                  // If non-NULL, AVIOContext will use this buffer and will NOT free it on exit."
                                  // Quindi, se passiamo il nostro buffer, dobbiamo liberarlo noi.
                                  // Però, io_buffer è stato allocato con av_malloc, quindi av_free è corretto.
                                  // La frase "will NOT free it on exit" si riferisce a buffer non allocati con av_malloc.
                                  // Per av_malloc, avio_context_free dovrebbe gestirlo.
                                  // Per sicurezza, liberiamo io_buffer manualmente nel distruttore SE avio_ctx non lo fa.
                                  // La prassi più sicura è passare nullptr per il buffer e lasciare che FFmpeg allochi.
                                  // Ma per ora proviamo così.
                                  // UPDATE: La documentazione di avio_alloc_context dice che se il buffer è fornito,
                                  // il chiamante lo possiede e deve liberarlo. Se il buffer è NULL, avio_alloc_context alloca.
                                  // Quindi, io_buffer deve essere liberato da noi nel distruttore.

        format_ctx = avformat_alloc_context();
        if (!format_ctx) {
            // av_free(avio_ctx->buffer); // Se avio_ctx ha preso possesso del buffer
            av_free(io_buffer); // Liberiamo il nostro buffer
            avio_context_free(&avio_ctx); // Libera l'avio_ctx
            throw Error("FFmpegAacDecoder: Impossibile allocare format_ctx.");
        }
        format_ctx->pb = avio_ctx;
        format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO; // Indica che usiamo I/O personalizzato

        // Apri l'input. FFmpeg tenterà di rilevare il formato.
        // Per AAC grezzo (ADTS), potrebbe essere necessario dare un "hint" a FFmpeg.
        // AVInputFormat *input_fmt = av_find_input_format("aac"); // Per ADTS stream
        // if (avformat_open_input(&format_ctx, nullptr, input_fmt, nullptr) != 0) {
        if (avformat_open_input(&format_ctx, "dummy_filename", nullptr, nullptr) != 0) { // "dummy_filename" è ignorato con custom I/O
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avformat_close_input(&format_ctx); // Anche se fallito, alcuni campi potrebbero essere allocati
            throw Error("FFmpegAacDecoder: Impossibile aprire l'input stream.");
        }

        // Trova informazioni sullo stream
        if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile trovare stream info.");
        }

        // Trova il primo stream audio
        AVCodec *decoder = nullptr;
        audio_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (audio_stream_index < 0 || !decoder) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile trovare stream audio o decodificatore.");
        }
        
        // Alloca il contesto per il decodificatore
        codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile allocare codec_ctx.");
        }

        // Copia i parametri del codec dallo stream al contesto del codec
        if (avcodec_parameters_to_context(codec_ctx, format_ctx->streams[audio_stream_index]->codecpar) < 0) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile copiare i parametri del codec.");
        }

        // Apri il decodificatore
        // Specifichiamo il formato di output desiderato (float planar)
        // codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_FLTP; // Spesso il default per AAC
        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile aprire il decodificatore.");
        }

        // Estrai informazioni
        this->channels = codec_ctx->ch_layout.nb_channels; // Usa ch_layout per un conteggio canali più robusto
        this->sample_rate = codec_ctx->sample_rate;
        this->sample_format = codec_ctx->sample_fmt; // Formato effettivo prodotto dal decodificatore

        if (this->channels == 0 || this->sample_rate == 0) {
             // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Canali o sample rate non validi dopo l'apertura del codec.");
        }
        
        // Verifica se il formato è float. Se non lo è, avremmo bisogno di swresample per convertirlo.
        // Per AAC, FFmpeg di solito decodifica in AV_SAMPLE_FMT_FLTP (float planar).
        if (this->sample_format != AV_SAMPLE_FMT_FLTP && this->sample_format != AV_SAMPLE_FMT_FLT) {
            logWarning("FFmpegAacDecoder: Il formato campione del decodificatore non è float planar (FLTP) o float (FLT), ma %s. La conversione manuale potrebbe non funzionare correttamente.", av_get_sample_fmt_name(this->sample_format));
            // Qui sarebbe il posto per inizializzare SwrContext se necessario.
        }


        // Alloca packet e frame
        packet = av_packet_alloc();
        decoded_frame = av_frame_alloc();
        if (!packet || !decoded_frame) {
            // av_free(avio_ctx->buffer);
            av_free(io_buffer);
            avio_context_free(&avio_ctx);
            av_packet_free(&packet);
            av_frame_free(&decoded_frame);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&format_ctx);
            throw Error("FFmpegAacDecoder: Impossibile allocare packet o frame.");
        }

        logDebug("FFmpegAacDecoder inizializzato: SR=%d Hz, Canali=%d, Formato FFmpeg=%s", 
                 this->sample_rate, this->channels, av_get_sample_fmt_name(this->sample_format));

    }

    ~FFmpegAacDecoder() override {
        av_packet_free(&packet);
        av_frame_free(&decoded_frame);
        avcodec_free_context(&codec_ctx); // Chiude anche il decodificatore se aperto
        // L'ordine di chiusura per custom I/O è importante:
        // Prima il format_ctx, che usa l'avio_ctx.
        avformat_close_input(&format_ctx); // Questo dovrebbe anche chiamare pb->close se definito, ma il nostro pb è avio_ctx
        // Poi l'avio_ctx, che usa il buffer.
        // av_free(avio_ctx->buffer); // Libera il buffer interno se FFmpeg lo ha allocato
        if (avio_ctx) { // avio_ctx->buffer è il nostro io_buffer se glielo abbiamo passato
             // av_free(avio_ctx->buffer); // Non fare questo se il buffer è il nostro io_buffer
        }
        av_free(io_buffer); // Liberiamo il nostro buffer allocato con av_malloc
        avio_context_free(&avio_ctx); // Libera la struttura AVIOContext stessa

        logDebug("FFmpegAacDecoder distrutto.");
    }

    unsigned long long writeSamplesInterleaved(unsigned long long num_frames_to_write, float *output_samples, unsigned int channels_req = 0) override {
        if (!codec_ctx || num_frames_to_write == 0) return 0;

        // Synthizer si aspetta che l'output sia nel numero di canali nativo del decodificatore
        // o gestisce il downmixing/upmixing altrove.
        // Qui, ch_out dovrebbe idealmente corrispondere a this->channels.
        // Se channels_req è specificato e diverso, questa funzione non fa channel conversion.
        unsigned int ch_out_internal = this->channels; 
        if (channels_req > 0 && channels_req != (unsigned int)this->channels) {
            logWarning("FFmpegAacDecoder: channels_req (%u) diverso dai canali del decodificatore (%d). L'output avrà %d canali.", channels_req, this->channels, this->channels);
            // Non gestiamo la conversione di canali qui. L'output sarà sempre con this->channels.
            // Il chiamante dovrà gestire la discrepanza.
        }


        unsigned long long frames_written_total = 0;
        std::fill(output_samples, output_samples + num_frames_to_write * ch_out_internal, 0.0f);

        while (frames_written_total < num_frames_to_write) {
            // Fase 1: Abbiamo campioni già decodificati e pronti per l'interleave?
            if (interleaved_buffer_frames_ready > 0) {
                unsigned long long frames_to_copy_from_buffer = std::min((unsigned long long)interleaved_buffer_frames_ready, num_frames_to_write - frames_written_total);
                
                float* src_ptr = interleaved_buffer.data() + (interleaved_buffer_pos * ch_out_internal);
                float* dst_ptr = output_samples + (frames_written_total * ch_out_internal);
                
                memcpy(dst_ptr, src_ptr, frames_to_copy_from_buffer * ch_out_internal * sizeof(float));
                
                frames_written_total += frames_to_copy_from_buffer;
                interleaved_buffer_pos += frames_to_copy_from_buffer;
                interleaved_buffer_frames_ready -= frames_to_copy_from_buffer;

                if (interleaved_buffer_frames_ready == 0) {
                    interleaved_buffer_pos = 0; // Resetta per il prossimo riempimento
                }
                if (frames_written_total == num_frames_to_write) break;
            }

            // Fase 2: Ricevi un frame decodificato da FFmpeg
            int ret = avcodec_receive_frame(codec_ctx, decoded_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                // Bisogno di più input (EAGAIN) o fine dello stream (EOF)
                // Fase 3: Leggi un pacchetto dallo stream e invialo al decodificatore
                av_packet_unref(packet); // Assicurati che il pacchetto sia pulito
                int read_ret = av_read_frame(format_ctx, packet);
                if (read_ret < 0) { // Errore o EOF
                    if (read_ret == AVERROR_EOF) {
                        logDebug("FFmpegAacDecoder: av_read_frame ha restituito EOF. Invio pacchetto nullo per flush.");
                        // Invia un pacchetto nullo per fare il flush del decodificatore
                        avcodec_send_packet(codec_ctx, nullptr); 
                        // Continua il loop per ricevere gli ultimi frame dal flush
                        // Se avcodec_receive_frame restituisce EOF di nuovo, allora abbiamo finito.
                    } else {
                        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                        av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, read_ret);
                        logError("FFmpegAacDecoder: Errore durante av_read_frame: %s", errbuf);
                    }
                    // Se av_read_frame fallisce (non EOF), o se è EOF e abbiamo già inviato nullptr,
                    // non c'è più nulla da fare.
                    if (ret == AVERROR_EOF && read_ret == AVERROR_EOF) break; // Doppio EOF, uscita
                    if (read_ret != AVERROR_EOF) break; // Errore di lettura, uscita
                } else { // Pacchetto letto con successo
                    if (packet->stream_index == audio_stream_index) {
                        if (avcodec_send_packet(codec_ctx, packet) < 0) {
                            logError("FFmpegAacDecoder: Errore durante l'invio del pacchetto al decodificatore.");
                            av_packet_unref(packet);
                            break; 
                        }
                    }
                    av_packet_unref(packet); // Il decodificatore ha fatto una copia se necessario
                }
                // Riprova a ricevere il frame dopo aver inviato un pacchetto (o nullptr per flush)
                continue; 
            } else if (ret < 0) {
                // Errore di decodifica fatale
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
                logError("FFmpegAacDecoder: Errore durante avcodec_receive_frame: %s", errbuf);
                break;
            }

            // Fase 4: Frame ricevuto con successo. Processalo.
            // decoded_frame->nb_samples è il numero di campioni PER CANALE nel frame.
            // decoded_frame->format è il formato dei campioni (es. AV_SAMPLE_FMT_FLTP).
            // decoded_frame->data[0], data[1], ... sono i puntatori ai piani dei canali (se planare).
            // decoded_frame->linesize[0] è la dimensione di un piano di campioni in byte.

            if (decoded_frame->nb_samples > 0) {
                // Assumiamo che il formato sia AV_SAMPLE_FMT_FLTP (float planar)
                // e che il numero di canali corrisponda a this->channels.
                if (sample_format == AV_SAMPLE_FMT_FLTP) {
                    interleaved_buffer.resize(decoded_frame->nb_samples * this->channels);
                    float *out_ptr = interleaved_buffer.data();
                    for (int i = 0; i < decoded_frame->nb_samples; i++) {
                        for (int ch = 0; ch < this->channels; ch++) {
                            // I dati in AVFrame per formati planari sono float* per canale
                            *out_ptr++ = ((float**)decoded_frame->data)[ch][i];
                        }
                    }
                    interleaved_buffer_frames_ready = decoded_frame->nb_samples;
                    interleaved_buffer_pos = 0;
                } else if (sample_format == AV_SAMPLE_FMT_FLT) { // Float interleaved (raro da un decodificatore AAC)
                     interleaved_buffer.resize(decoded_frame->nb_samples * this->channels);
                     memcpy(interleaved_buffer.data(), decoded_frame->data[0], decoded_frame->nb_samples * this->channels * sizeof(float));
                     interleaved_buffer_frames_ready = decoded_frame->nb_samples;
                     interleaved_buffer_pos = 0;
                } else {
                    logError("FFmpegAacDecoder: Formato campione non supportato %s per la conversione manuale.", av_get_sample_fmt_name(sample_format));
                    // Qui si dovrebbe usare libswresample per convertire a FLTP o FLT interleaved.
                    // Per ora, non produciamo campioni se il formato non è gestito.
                    av_frame_unref(decoded_frame);
                    continue; 
                }
            }
            av_frame_unref(decoded_frame); // Libera il frame per il prossimo utilizzo
        }
        return frames_written_total;
    }

    int getSr() override { return sample_rate; }
    int getChannels() override { return channels; }
    synthizer::AudioFormat getFormat() override { return synthizer::AudioFormat::Raw; } 

    void seekPcm(unsigned long long frame_pos) override {
        if (!format_ctx || !codec_ctx || audio_stream_index < 0) {
            throw Error("FFmpegAacDecoder: Seek tentato su decoder non inizializzato.");
        }
        if (!supportsSeek()) { // Basato sulla nostra implementazione di seek_callback
            throw Error("FFmpegAacDecoder: Seek non supportato da questo stream/configurazione.");
        }

        // Converte la posizione del frame PCM in timestamp dello stream
        // AVStream *st = format_ctx->streams[audio_stream_index];
        // int64_t target_ts = av_rescale_q(frame_pos, {1, sample_rate}, st->time_base);
        int64_t target_ts = av_rescale(frame_pos, format_ctx->streams[audio_stream_index]->time_base.den, 
                                       (int64_t)sample_rate * format_ctx->streams[audio_stream_index]->time_base.num);


        logDebug("FFmpegAacDecoder: Richiesta di seek al frame PCM %llu (timestamp %lld)", frame_pos, target_ts);

        // AVSEEK_FLAG_BACKWARD: cerca il keyframe più vicino prima del timestamp.
        // Altri flag: AVSEEK_FLAG_BYTE, AVSEEK_FLAG_ANY, AVSEEK_FLAG_FRAME
        int ret = av_seek_frame(format_ctx, audio_stream_index, target_ts, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
            logError("FFmpegAacDecoder: Errore durante av_seek_frame: %s", errbuf);
            // Non lanciare eccezione, ma il seek potrebbe non essere accurato.
        }
        
        // Dopo il seek, è necessario fare il flush dei buffer del decodificatore.
        avcodec_flush_buffers(codec_ctx);
        
        // Resetta lo stato del buffer interleaved
        interleaved_buffer.clear();
        interleaved_buffer_frames_ready = 0;
        interleaved_buffer_pos = 0;
    }

    bool supportsSeek() override { 
        // Dipende da come implementi seek_callback e se format_ctx->iformat->read_seek è definito.
        // Per ora, diciamo di sì se lo stream sottostante lo dice, ma seek_callback è un placeholder.
        return custom_io_ctx_data.stream && custom_io_ctx_data.stream->supportsSeek();
    }
    bool supportsSampleAccurateSeek() override { return false; } // Generalmente difficile con formati compressi
    
    unsigned long long getLength() override {
        if (format_ctx && format_ctx->duration != AV_NOPTS_VALUE) {
            // La durata è in unità AV_TIME_BASE (tipicamente microsecondi)
            // Converti in numero di frame PCM
            double duration_seconds = (double)format_ctx->duration / AV_TIME_BASE;
            return (unsigned long long)(duration_seconds * sample_rate);
        }
        return 0; // Lunghezza sconosciuta
    }
};


} // namespace aac_detail

// Funzione factory per creare il decoder FFmpeg
std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream_ptr) {
    // FFmpeg gestisce il rilevamento del formato, quindi non serve controllare l'header qui
    // sebbene per AAC ADTS grezzo, FFmpeg potrebbe aver bisogno di un hint.
    // Ma con avformat_open_input e custom I/O, dovrebbe funzionare.

    try {
        // Registra tutti i formati e codec (chiamata una sola volta, ma è sicuro chiamarla più volte)
        // Queste sono deprecate. avformat_open_input dovrebbe gestire la maggior parte delle cose.
        // av_register_all(); // Deprecato
        // avcodec_register_all(); // Deprecato
        // avdevice_register_all(); // Per dispositivi, non strettamente necessario qui
        // avformat_network_init(); // Per protocolli di rete, non strettamente necessario per ByteStream

        return std::make_shared<aac_detail::FFmpegAacDecoder>(stream_ptr);
    } catch (const std::exception &e) {
        logError("FFmpeg AAC Decoder: Fallimento creazione FFmpegAacDecoder: %s", e.what());
        // Non resettare lo stream qui, il costruttore del decoder non dovrebbe modificarlo se fallisce presto.
        return nullptr;
    } catch (...) {
        logError("FFmpeg AAC Decoder: Errore sconosciuto durante creazione FFmpegAacDecoder.");
        return nullptr;
    }
}

} // namespace synthizer