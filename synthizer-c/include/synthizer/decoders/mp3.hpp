#pragma once

#include "synthizer/byte_stream.hpp"
#include "synthizer/channel_mixing.hpp"
#include "synthizer/config.hpp" 
#include "synthizer/decoding.hpp"
#include "synthizer/error.hpp"
#include "synthizer/types.hpp"

#include "dr_mp3.h" 

#include <algorithm> 
#include <array>     
#include <cstdint>   
#include <memory>    

namespace synthizer {

std::shared_ptr<AudioDecoder> decodeMp3(std::shared_ptr<LookaheadByteStream> stream);

namespace mp3_detail {

extern "C" {

    // read_cb COMPLETO
    static std::size_t read_cb(void *user_data, void *out, std::size_t count) {
      ByteStream *stream = static_cast<ByteStream *>(user_data);
      return stream->read(count, static_cast<char *>(out));
    }

    // seek_cb ORA COMPLETO E RIVEDUTO ATTENTAMENTE
    static drmp3_bool32 seek_cb(void *user_data, drmp3_int64 offset_from_origin, drmp3_seek_origin origin) {
      ByteStream *stream = static_cast<ByteStream *>(user_data);
      std::size_t new_pos = 0; 
      std::size_t current_pos;

      switch (origin) {
        case drmp3_seek_origin_start:
          if (offset_from_origin < 0) {
            return DRMP3_FALSE; // Non si può cercare prima dell'inizio
          }
          new_pos = static_cast<std::size_t>(offset_from_origin);
          break; 
        case drmp3_seek_origin_current:
          current_pos = stream->getPosition();
          if (offset_from_origin >= 0) {
            // Controllo overflow per l'addizione current_pos + offset_from_origin
            if (static_cast<drmp3_uint64>(offset_from_origin) > (DRMP3_UINT64_MAX - current_pos)) {
                 return DRMP3_FALSE; // Overflow
            }
            new_pos = current_pos + static_cast<std::size_t>(offset_from_origin);
          } else {
            // offset_from_origin è negativo
            drmp3_uint64 abs_offset_magnitude = static_cast<drmp3_uint64>(-offset_from_origin);
            if (abs_offset_magnitude > current_pos) {
              return DRMP3_FALSE; // Non si può cercare prima dell'inizio (underflow)
            }
            new_pos = current_pos - abs_offset_magnitude;
          }
          break;
        case drmp3_seek_origin_end:
          // ByteStream di Synthizer probabilmente non supporta il seek relativo dalla fine.
          return DRMP3_FALSE; 
        default: // Non dovrebbe accadere con le origini definite da dr_mp3
          return DRMP3_FALSE;
      } // Chiusura switch

      // Esegui il seek effettivo sullo ByteStream
try {
    stream->seek(new_pos);
    return DRMP3_TRUE;
} catch (...) {
    return DRMP3_FALSE;
}
    } // Chiusura seek_cb

    // tell_cb COMPLETO
    static drmp3_bool32 tell_cb(void *user_data, drmp3_int64* pCursor) {
      ByteStream *stream = static_cast<ByteStream *>(user_data);
      *pCursor = static_cast<drmp3_int64>(stream->getPosition());
      return DRMP3_TRUE;
    }

} // Fine del blocco extern "C" 

class Mp3Decoder : public AudioDecoder { 
public:
  Mp3Decoder(std::shared_ptr<LookaheadByteStream> stream_in);
  ~Mp3Decoder() override;
  unsigned long long writeSamplesInterleaved(unsigned long long num, float *samples, unsigned int channels = 0) override;
  int getSr() override;
  int getChannels() override;
  AudioFormat getFormat() override;
  void seekPcm(unsigned long long pos) override;
  bool supportsSeek() override;
  bool supportsSampleAccurateSeek() override;
  unsigned long long getLength() override;

private:
  drmp3 mp3;
  std::shared_ptr<ByteStream> stream;
  unsigned long long frame_count = 0;
  std::array<float, config::BLOCK_SIZE * config::MAX_CHANNELS> tmp_buf{{0.0f}};
};

inline Mp3Decoder::Mp3Decoder(std::shared_ptr<LookaheadByteStream> stream_in) {
  stream_in->resetFinal();
  this->stream = stream_in;

  drmp3_read_proc current_read_cb = (drmp3_read_proc)read_cb; 
  drmp3_seek_proc local_seek_cb_for_init = nullptr; 
  drmp3_tell_proc current_tell_cb = nullptr;

  if (this->stream->supportsSeek()) {
    local_seek_cb_for_init = (drmp3_seek_proc)seek_cb; // Ora punta al seek_cb completo
    current_tell_cb = (drmp3_tell_proc)tell_cb;
  }

  if (drmp3_init(&this->mp3, current_read_cb, local_seek_cb_for_init, current_tell_cb,
                 nullptr, this->stream.get(), nullptr) == DRMP3_FALSE) {
    throw Error("Unable to initialize Mp3 stream");
  }

  if (this->mp3.channels == 0) {
    drmp3_uninit(&this->mp3);
    throw Error("Got a MP3 file with 0 channels.");
  }
  if (this->mp3.channels > config::MAX_CHANNELS) {
    drmp3_uninit(&this->mp3);
    throw Error("File has too many channels for Synthizer's configured MAX_CHANNELS.");
  }
  
  // Logica del frame_count RIPRISTINATA con gestione errori completa.
  if (this->mp3.totalPCMFrameCount != DRMP3_UINT64_MAX && this->mp3.totalPCMFrameCount != 0) {
    this->frame_count = this->mp3.totalPCMFrameCount;
  } else if (this->stream->supportsSeek() && local_seek_cb_for_init != nullptr) { 
    this->frame_count = drmp3_get_pcm_frame_count(&this->mp3);
    if (this->frame_count == 0) { 
      // Ora che seek_cb è completo, se supportiamo il seek e otteniamo 0 frame, è un errore.
      drmp3_uninit(&this->mp3);
      throw Error("Stream supports seek, but unable to compute frame count for Mp3 stream (drmp3_get_pcm_frame_count returned 0).");
    }
  } else {
    this->frame_count = 0; 
  }
}

inline Mp3Decoder::~Mp3Decoder() { 
  drmp3_uninit(&this->mp3); 
}

inline unsigned long long Mp3Decoder::writeSamplesInterleaved(unsigned long long num_frames_to_write, float *samples_out,
                                                               unsigned int output_channels_requested) {
  unsigned int actual_output_channels = (output_channels_requested < 1 || output_channels_requested > config::MAX_CHANNELS) 
                                        ? this->mp3.channels 
                                        : output_channels_requested;

  if (actual_output_channels == this->mp3.channels) {
    return drmp3_read_pcm_frames_f32(&this->mp3, num_frames_to_write, samples_out);
  }

  std::fill(samples_out, samples_out + num_frames_to_write * actual_output_channels, 0.0f);
  
  unsigned long long total_frames_written_to_output = 0;
  unsigned long long tmp_buf_capacity_in_frames = 0;
  
  if (this->mp3.channels > 0) {
      tmp_buf_capacity_in_frames = this->tmp_buf.size() / this->mp3.channels;
  }

  if (tmp_buf_capacity_in_frames == 0 && num_frames_to_write > 0) {
      return 0;
  }

  while (total_frames_written_to_output < num_frames_to_write) {
    unsigned long long frames_to_process_this_iteration = std::min(num_frames_to_write - total_frames_written_to_output, tmp_buf_capacity_in_frames);
    
    if (frames_to_process_this_iteration == 0) { 
        break; 
    }

    unsigned long long frames_read_from_decoder = drmp3_read_pcm_frames_f32(&this->mp3, frames_to_process_this_iteration, &this->tmp_buf[0]);
    
    if (frames_read_from_decoder == 0) {
      break; 
    }
    
    mixChannels(frames_read_from_decoder, 
                &this->tmp_buf[0], 
                this->mp3.channels, 
                samples_out + total_frames_written_to_output * actual_output_channels, 
                actual_output_channels);
                
    total_frames_written_to_output += frames_read_from_decoder;
  }
  return total_frames_written_to_output;
}

inline int Mp3Decoder::getSr() { return this->mp3.sampleRate; }
inline int Mp3Decoder::getChannels() { return this->mp3.channels; }
inline AudioFormat Mp3Decoder::getFormat() { return AudioFormat::Mp3; }

inline void Mp3Decoder::seekPcm(unsigned long long pcm_frame_index) {
  if (!this->supportsSeek()) { 
    throw Error("Seek operation called on a non-seekable MP3 stream/decoder.");
  }

  unsigned long long actual_pos = pcm_frame_index;
  if (this->frame_count > 0) { 
      actual_pos = std::min(this->frame_count, pcm_frame_index);
  }
  
  if (drmp3_seek_to_pcm_frame(&this->mp3, actual_pos) == DRMP3_FALSE) {
    // Ora che seek_cb è completo, un fallimento qui è un vero errore.
    throw Error("drmp3_seek_to_pcm_frame failed internally.");
  }
}

inline bool Mp3Decoder::supportsSeek() { 
  return this->stream->supportsSeek(); 
}

inline bool Mp3Decoder::supportsSampleAccurateSeek() { 
  return this->supportsSeek(); 
}

inline unsigned long long Mp3Decoder::getLength() { 
  return this->frame_count; 
}

} // namespace mp3_detail

inline std::shared_ptr<AudioDecoder> decodeMp3(std::shared_ptr<LookaheadByteStream> stream) {
  drmp3 test_mp3; 

  if (drmp3_init(&test_mp3, (drmp3_read_proc)mp3_detail::read_cb, 
                 nullptr, 
                 nullptr, 
                 nullptr, 
                 static_cast<void*>(stream.get()),
                 nullptr 
                 ) == DRMP3_FALSE) {
    return nullptr;
  }
  
  if (test_mp3.channels == 0 || test_mp3.sampleRate == 0) {
      drmp3_uninit(&test_mp3); 
      return nullptr;
  }
  
  drmp3_uninit(&test_mp3); 

  try {
    return std::make_shared<mp3_detail::Mp3Decoder>(stream);
  } catch (const std::exception &) {
    return nullptr;
  }
}

} // namespace synthizer