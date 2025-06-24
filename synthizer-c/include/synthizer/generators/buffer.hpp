#pragma once

#include "synthizer/block_buffer_cache.hpp"
#include "synthizer/buffer.hpp"
#include "synthizer/context.hpp"
#include "synthizer/edge_trigger.hpp"
#include "synthizer/events.hpp"
#include "synthizer/generator.hpp"
#include "synthizer/property_internals.hpp"
#include "synthizer/types.hpp"
#include "synthizer/vbool.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <cmath>
#include <vector>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Enable debug logging for speed processing (enabled for debugging 1.05 speed)
#define DEBUG_SYNTHIZER_SPEED 1

#include <soundtouch/SoundTouch.h>
using namespace soundtouch;

// Simplified logging for MSVC compatibility - remove complex debug system
#ifdef DEBUG_SYNTHIZER_SPEED
#include <cstdio>
#include <fstream>
#define SYNTHIZER_LOG_INFO(msg) do { \
  printf("[SYNTHIZER INFO] %s\n", (msg)); \
  { std::ofstream log("synthizerlog.txt", std::ios::app); \
    if (log.is_open()) { log << "[INFO] " << (msg) << std::endl; } } \
} while(0)
#define SYNTHIZER_LOG_WARNING(msg) do { \
  printf("[SYNTHIZER WARNING] %s\n", (msg)); \
  { std::ofstream log("synthizerlog.txt", std::ios::app); \
    if (log.is_open()) { log << "[WARNING] " << (msg) << std::endl; } } \
} while(0)
#define SYNTHIZER_LOG_ERROR(msg) do { \
  printf("[SYNTHIZER ERROR] %s\n", (msg)); \
  { std::ofstream log("synthizerlog.txt", std::ios::app); \
    if (log.is_open()) { log << "[ERROR] " << (msg) << std::endl; } } \
} while(0)
#else
#define SYNTHIZER_LOG_INFO(msg) do { } while(0)
#define SYNTHIZER_LOG_WARNING(msg) do { } while(0)
#define SYNTHIZER_LOG_ERROR(msg) do { } while(0)
#endif

namespace synthizer {

// Speed processing quality modes
enum class SpeedQualityMode {
  LOW_LATENCY = 0,    // Fast response, basic quality
  BALANCED = 1,       // Good balance of quality and latency  
  HIGH_QUALITY = 2    // Maximum quality, higher latency
};

// SoundTouch priming blocks needed for stable output (ridotto per risposta più veloce)
constexpr int SOUND_TOUCH_SAFE_PRIMING_BLOCKS = 1;  // Ridotto per risposta più veloce

/**
 * Plays a buffer.
 *
 * It is worth taking a moment to notate what is going on with positions and pitch bend.  In order to implement precise
 * pitch bend, this class uses a scaled position that can do what is in effect fixed point math (see
 * config::BUFFER_POS_MULTIPLIER). This allows us to avoid fp error on that path, though is mildly inconvenient
 * everywhere else.
 * */
class BufferGenerator : public Generator {
public:
  BufferGenerator(std::shared_ptr<Context> ctx);

  int getObjectType() override;
  unsigned int getChannels() const override;
  void generateBlock(float *output, FadeDriver *gain_driver) override;
  void seek(double new_pos);
  std::uint64_t getPosInSamples() const;

  std::optional<double> startGeneratorLingering() override;

#define PROPERTY_CLASS BufferGenerator
#define PROPERTY_BASE Generator
#define PROPERTY_LIST BUFFER_GENERATOR_PROPERTIES
#include "synthizer/property_impl.hpp"

private:
  void generateNoPitchBend(float *out, FadeDriver *gain_driver) const;
  void generatePitchBend(float *out, FadeDriver *gain_driver) const;
  void generateTimeStretchPitch(float *out, FadeDriver *gain_driver) const;
  void generateTimeStretchSpeed(float *out, FadeDriver *gain_driver) const;
  void generateSpeedTransition(float *out, FadeDriver *gain_driver) const;
  void generateScaleTempo(float *out, FadeDriver *gain_driver) const;
  void initSpeedProcessorIfNeeded(double speed_factor) const;
  void applyAntiAliasingFilter(std::vector<float>& samples, double pitch_factor, unsigned int channels) const;

  /*
   * Handle configuring properties, and set the non-property state variables up appropriately.
   *
   * Returns true if processing of the block should proceed, or false if there is no buffer and processing of the block
   * should be skipped.
   */
  bool handlePropertyConfig();

  BufferReader reader;

  /**
   * Set when this generator finishes. Used as an edge trigger to send a finished event, since buffers can only finish
   * once per block.
   * */
  bool finished = false;

  std::uint64_t scaled_position_in_frames = 0, scaled_position_increment = 0;
  
  // SoundTouch instance for time-stretch pitch shifting
  mutable std::unique_ptr<soundtouch::SoundTouch> soundtouch_processor;
  // Cache last pitch value to avoid unnecessary SoundTouch reconfigurations
  mutable double last_pitch_value;
  // Crossfade system for smooth pitch transitions
  mutable std::unique_ptr<soundtouch::SoundTouch> crossfade_processor;
  mutable std::vector<float> crossfade_buffer;
  mutable int crossfade_samples_remaining;
  
  // Speed control system with independent pitch preservation (speed-only)
  mutable std::unique_ptr<soundtouch::SoundTouch> speed_processor;
  mutable double last_speed_value;
  // Combined pitch+speed processor (separate from speed-only)
  mutable std::unique_ptr<soundtouch::SoundTouch> combined_processor;
  mutable double last_combined_speed_value;
  mutable double last_combined_pitch_value;
  // Speed crossfade system
  mutable std::unique_ptr<soundtouch::SoundTouch> speed_crossfade_processor;
  mutable std::vector<float> speed_crossfade_buffer;
  mutable int speed_crossfade_samples_remaining;
  // Buffer accumulation for larger SoundTouch processing chunks
  mutable std::vector<float> speed_input_accumulator;
  mutable std::vector<float> speed_output_buffer;
  mutable int speed_priming_blocks;
  
  // ScaleTempo algorithm state variables
  mutable double scaletempo_scale;
  mutable unsigned int scaletempo_ms_stride;
  mutable double scaletempo_percent_overlap;
  mutable unsigned int scaletempo_ms_search;
  mutable unsigned int scaletempo_bytes_stride;
  mutable unsigned int scaletempo_samples_overlap;
  mutable unsigned int scaletempo_samples_standing;
  mutable unsigned int scaletempo_frames_search;
  mutable std::vector<float> scaletempo_buf_queue;
  mutable std::vector<float> scaletempo_buf_overlap;
  mutable std::vector<float> scaletempo_table_blend;
  mutable std::vector<float> scaletempo_buf_pre_corr;
  mutable std::vector<float> scaletempo_table_window;
  mutable unsigned int scaletempo_bytes_queued;
  mutable unsigned int scaletempo_bytes_to_slide;
  mutable double scaletempo_frames_stride_scaled;
  mutable double scaletempo_frames_stride_error;
};

inline BufferGenerator::BufferGenerator(std::shared_ptr<Context> ctx) : 
  Generator(ctx), 
  last_pitch_value(-1.0), 
  crossfade_samples_remaining(0), 
  last_speed_value(-1.0), 
  last_combined_speed_value(-1.0), 
  last_combined_pitch_value(-1.0), 
  speed_crossfade_samples_remaining(0), 
  speed_priming_blocks(0),
  scaletempo_scale(1.0),
  scaletempo_ms_stride(30),
  scaletempo_percent_overlap(0.20),
  scaletempo_ms_search(14),
  scaletempo_bytes_stride(0),
  scaletempo_samples_overlap(0),
  scaletempo_samples_standing(0),
  scaletempo_frames_search(0),
  scaletempo_bytes_queued(0),
  scaletempo_bytes_to_slide(0),
  scaletempo_frames_stride_scaled(0.0),
  scaletempo_frames_stride_error(0.0) {
}

inline int BufferGenerator::getObjectType() { return SYZ_OTYPE_BUFFER_GENERATOR; }

inline unsigned int BufferGenerator::getChannels() const {
  auto buf_weak = this->getBuffer();

  auto buffer = buf_weak.lock();
  if (buffer == nullptr)
    return 0;
  return buffer->getChannels();
}

inline void BufferGenerator::seek(double new_pos) {
  std::uint64_t new_pos_samples = new_pos * config::SR;
  new_pos_samples = new_pos_samples >= this->reader.getLengthInFrames(false) ? this->reader.getLengthInFrames(false) - 1
                                                                             : new_pos_samples;
  this->scaled_position_in_frames = new_pos_samples * config::BUFFER_POS_MULTIPLIER;
  this->finished = false;
  this->setPlaybackPosition(this->getPosInSamples() / (double)config::SR, false);
}

inline std::uint64_t BufferGenerator::getPosInSamples() const {
  return this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER;
}

inline void BufferGenerator::generateBlock(float *output, FadeDriver *gd) {
  double new_pos;

  if (this->handlePropertyConfig() == false) {
    return;
  }

  // Clear output buffer to prevent accumulation of old samples (critical for clean audio)
  const unsigned int channels = this->getChannels();
  if (channels == 0) {
    return; // No valid audio to process
  }
  std::fill(output, output + config::BLOCK_SIZE * channels, 0.0f);

  if (this->acquirePlaybackPosition(new_pos)) {
    this->seek(new_pos);
  }

  // We saw the end and haven't seeked or set the buffer, so don't do anything.
  if (this->finished == true) {
    return;
  }

  // it is possible for the generator to need to advance less if it is at or near the end, but we deal withv that below
  // and avoid very complicated computations that try to work out what it actually is: we did that in the past, and it
  // lead to no end of bugs.
  
  // Check pitch bend mode to determine position increment behavior
  if (this->getPitchBendMode() == SYZ_PITCH_BEND_MODE_TIME_STRETCH) {
    // Time-stretch mode: speed controlled by speed_multiplier, independent of pitch
    this->scaled_position_increment = config::BUFFER_POS_MULTIPLIER * this->getSpeedMultiplier();
  } else {
    // Classic mode: speed changes with pitch
    this->scaled_position_increment = config::BUFFER_POS_MULTIPLIER * this->getPitchBend();
  }
  
  std::uint64_t scaled_pos_increment = this->scaled_position_increment * config::BLOCK_SIZE;

  // Sistema di transizione immediata per velocità
  double current_speed = this->getSpeedMultiplier();
  double current_pitch = this->getPitchBend();
  
  // Rileva cambio di velocità e attiva transizione immediata
  static thread_local double last_speed = -1.0;
  if (std::abs(current_speed - last_speed) > 0.01 && last_speed > 0) {
    // Reset immediato dei processori per transizione istantanea
    if (this->speed_processor) {
      this->speed_processor->clear();
      this->speed_input_accumulator.clear();
      this->speed_priming_blocks = 0;
    }
    if (this->combined_processor) {
      this->combined_processor->clear();
    }
    
    SYNTHIZER_LOG_INFO(("Immediate speed transition: " + std::to_string(last_speed) + " -> " + std::to_string(current_speed)).c_str());
  }
  last_speed = current_speed;

  if (this->getPitchBendMode() == SYZ_PITCH_BEND_MODE_TIME_STRETCH) {
    // Time-stretch mode: check if we need pitch or speed processing
    const double epsilon = 0.0001;
    bool need_pitch_stretch = (std::abs(current_pitch - 1.0) > epsilon);
    bool need_speed_stretch = (std::abs(current_speed - 1.0) > epsilon);
    
    // Logging ridotto solo per cambi significativi
    static thread_local double last_logged_speed = -1.0;
    static thread_local double last_logged_pitch = -1.0;
    
    if (std::abs(current_speed - last_logged_speed) > 0.1 || 
        std::abs(current_pitch - last_logged_pitch) > 0.1) {
      std::stringstream property_debug;
      property_debug << "Speed: " << current_speed << " Pitch: " << current_pitch;
      SYNTHIZER_LOG_INFO(property_debug.str().c_str());
      last_logged_speed = current_speed;
      last_logged_pitch = current_pitch;
    }
    
    if (need_pitch_stretch && need_speed_stretch) {
      // Both pitch and speed need processing - use SoundTouch
      this->generateTimeStretchSpeed(output, gd);
    } else if (need_pitch_stretch) {
      // Only pitch needs processing - use SoundTouch for pitch
      this->generateTimeStretchPitch(output, gd);
    } else if (need_speed_stretch) {
      // NUOVO: Usa generazione diretta con interpolazione per transizioni immediate
      this->generateSpeedTransition(output, gd);
    } else {
      // No processing needed - use optimized path without SoundTouch overhead
      // This prevents distortion when no speed/pitch changes are applied
      this->generateNoPitchBend(output, gd);
    }
  } else {
    // Classic mode
    if (this->getPitchBend() == 1.0) {
      this->generateNoPitchBend(output, gd);
    } else {
      this->generatePitchBend(output, gd);
    }
  }

  if (this->getLooping()) {
    // If we are looping, then the position can always go past the end.
    unsigned int loop_count = (this->scaled_position_in_frames + scaled_pos_increment + config::BUFFER_POS_MULTIPLIER) /
                              (config::BUFFER_POS_MULTIPLIER * this->reader.getLengthInFrames(false));
    for (unsigned int i = 0; i < loop_count; i++) {
      sendLoopedEvent(this->getContext(), this->shared_from_this());
    }
    this->scaled_position_in_frames = (this->scaled_position_in_frames + scaled_pos_increment) %
                                      (this->reader.getLengthInFrames(false) * config::BUFFER_POS_MULTIPLIER);
  } else if (this->finished == false &&
             this->scaled_position_in_frames + scaled_pos_increment + config::BUFFER_POS_MULTIPLIER >=
                 this->reader.getLengthInFrames(false) * config::BUFFER_POS_MULTIPLIER) {
    // In this case, the position might be past the end so we'll set it to the end exactly when we're done.
    sendFinishedEvent(this->getContext(), this->shared_from_this());
    this->finished = true;
    this->scaled_position_in_frames = this->reader.getLengthInFrames(false) * config::BUFFER_POS_MULTIPLIER;
  } else {
    // this won't need modulus, because otherwise that would have meant looping or ending.
    this->scaled_position_in_frames += scaled_pos_increment;
  }

  this->setPlaybackPosition(this->getPosInSamples() / (double)config::SR, false);
}

inline void BufferGenerator::generateNoPitchBend(float *output, FadeDriver *gd) const {
  assert(this->finished == false);

  // Bump scaled_position_in_frames up to the next multiplier, if necessary.
  // this->scaled_position_in_frames = ceilByPowerOfTwo(this->scaled_position_in_frames, config::BUFFER_POS_MULTIPLIER);

  std::size_t will_read_frames = config::BLOCK_SIZE;
  if (this->getPosInSamples() + will_read_frames > this->reader.getLengthInFrames(false) &&
      this->getLooping() == false) {
    will_read_frames = this->reader.getLengthInFrames(false) - this->getPosInSamples() - 1;
    will_read_frames = std::min<std::size_t>(will_read_frames, config::BLOCK_SIZE);
  }

  // Compilers are bad about telling that channels doesn't change.
  const unsigned int channels = this->getChannels();

  auto mp = this->reader.getFrameSlice(this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
                                       will_read_frames, false, true);
  std::visit(
      [&](auto ptr) {
        gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
          for (std::size_t i = 0; i < will_read_frames; i++) {
            float gain = gain_cb(i) * (1.0f / 32768.0f);
            for (unsigned int ch = 0; ch < channels; ch++) {
              output[i * channels + ch] += ptr[i * channels + ch] * gain;
            }
          }
        });
      },
      mp);
}

namespace buffer_generator_detail {

/**
 * Parameters needed to do pitch bend.
 *
 * If pitch bend can't be done because delta is 0, this is zero-initialized and iterations = 0.
 * */
struct PitchBendParams {
  std::uint64_t offset;
  std::size_t iterations;

  /**
   * The span we must grab from the underlying buffer, in samples.
   *
   * Possibly includes the implicit zero, if required.
   * */
  std::size_t span_start, span_len;

  /**
   * Whether or not the buffer should include the implicit zero.
   * */
  bool include_implicit_zero;
};

inline PitchBendParams computePitchBendParams(std::uint64_t scaled_position, std::uint64_t delta,
                                              std::uint64_t buffer_len_no_zero, bool looping) {
  PitchBendParams ret{};

  if (delta == 0 || scaled_position >= buffer_len_no_zero) {
    ret.iterations = 0;
    return ret;
  }

  ret.iterations = config::BLOCK_SIZE;

  // If we are going to read past the end and are not looping, we must do less than that.
  if (looping == false) {
    // if the lower sample in the linear interpolation is going past the end of the buffer, more care is required.
    if (scaled_position + ret.iterations * delta >= buffer_len_no_zero) {
      // We must work out how many fractional samples remain before the lower sample hits the end of the buffer.
      std::uint64_t remaining_data = buffer_len_no_zero - scaled_position - 1;
      // If the available remaining data perfectly divides by delta, then the loop iterations is simply the division.
      if (remaining_data % delta == 0) {
        ret.iterations = remaining_data / delta;
      } else {
        // Otherwise, we can actually run one more iteration than it seems like we should be able to because the lower
        // sample is still fine.
        //
        // This is effectively ceil, but written out for clarity.
        ret.iterations = remaining_data / delta + 1;
      }
    }
  }

  ret.include_implicit_zero = looping == false;
  ret.offset = scaled_position - floorByPowerOfTwo(scaled_position, config::BUFFER_POS_MULTIPLIER);

  // We can work out the read span from the number of iterations.
  ret.span_start = scaled_position / config::BUFFER_POS_MULTIPLIER;

  // The maximum index we will read is the `upper` value fropm the last iteration.
  std::uint64_t max_index = (ret.offset + (ret.iterations - 1) * delta) / config::BUFFER_POS_MULTIPLIER + 1;
  // And the length of the span is one more than that.
  ret.span_len = max_index + 1;

  return ret;
}
} // namespace buffer_generator_detail

inline void BufferGenerator::generatePitchBend(float *output, FadeDriver *gd) const {
  assert(this->finished == false);

  const auto params = buffer_generator_detail::computePitchBendParams(
      this->scaled_position_in_frames, this->scaled_position_increment,
      this->reader.getLengthInFrames(false) * config::BUFFER_POS_MULTIPLIER, this->getLooping());

  if (params.iterations == 0) {
    return;
  }

  auto mp = this->reader.getFrameSlice(params.span_start, params.span_len, params.include_implicit_zero, true);

  // In the case where the number of iterations is the block size, we can use VBool to let the compiler know.
  bool _is_full_block = params.iterations == config::BLOCK_SIZE;

  // the compiler is bad about telling that channels doesn't change.
  const unsigned int channels = this->getChannels();

  std::visit(
      [&](auto ptr, auto full_block) {
        gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
          // Let the compiler understand that, sometimes, it can fully unroll the loop.
          const std::size_t iters = full_block ? config::BLOCK_SIZE : params.iterations;
          const std::uint64_t delta = this->scaled_position_increment;

          for (std::size_t i = 0; i < iters; i++) {
            std::uint64_t scaled_effective_pos = params.offset + delta * i;
            std::size_t scaled_lower = floorByPowerOfTwo(scaled_effective_pos, config::BUFFER_POS_MULTIPLIER);
            std::size_t lower = scaled_lower / config::BUFFER_POS_MULTIPLIER;

            // We are close enough to the point where floats start erroring that we probably want doubles, or at least
            // to do some sort of actual error analysis which hasn't been done as of this writing. Thus, doubles for
            // now.
            //
            // Also factor in the conversion from 16-bit signed samples into these weights.
            double w2 = (scaled_effective_pos - scaled_lower) * (1.0 / config::BUFFER_POS_MULTIPLIER);
            double w1 = 1.0 - w2;
            w1 *= (1.0 / 32768.0);
            w2 *= (1.0 / 32768.0);
            float gain = gain_cb(i);

            for (unsigned int ch = 0; ch < channels; ch++) {
              std::int16_t l = ptr[lower * channels + ch], u = ptr[(lower + 1) * channels + ch];
              output[i * channels + ch] = gain * (w1 * l + w2 * u);
            }
          }
        });
      },
      mp, vCond(_is_full_block));
}

inline void BufferGenerator::initSpeedProcessorIfNeeded(double speed_factor) const {
  if (!this->speed_processor) {
    this->speed_processor = std::make_unique<soundtouch::SoundTouch>();
    this->speed_processor->setSampleRate(config::SR);
    this->speed_processor->setChannels(this->getChannels());
    
    // Configurazione ULTRA-REATTIVA per transizioni immediate
    this->speed_processor->setSetting(SETTING_USE_QUICKSEEK, 1);      // Quick seek ON
    this->speed_processor->setSetting(SETTING_USE_AA_FILTER, 0);      // No AA filter
    this->speed_processor->setSetting(SETTING_SEQUENCE_MS, 10);       // Ridotto da 40
    this->speed_processor->setSetting(SETTING_SEEKWINDOW_MS, 5);      // Ridotto da 15
    this->speed_processor->setSetting(SETTING_OVERLAP_MS, 3);         // Ridotto da 8
    
    // Disabilita buffering interno per risposta immediata
    this->speed_processor->setSetting(SETTING_NOMINAL_INPUT_SEQUENCE, 0);
    this->speed_processor->setSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE, 0);
    
    this->speed_processor->setTempo(speed_factor);
    this->last_speed_value = speed_factor;
    
    // Buffer minimi per latenza zero
    this->speed_input_accumulator.clear();
    this->speed_input_accumulator.reserve(config::BLOCK_SIZE * this->getChannels() * 2); // Ridotto da 4
    this->speed_output_buffer.resize(config::BLOCK_SIZE * this->getChannels() * 2); // Ridotto da 4
    this->speed_priming_blocks = 0;
    
    SYNTHIZER_LOG_INFO("Speed processor initialized for IMMEDIATE RESPONSE");
  }
}

inline void BufferGenerator::applyAntiAliasingFilter(std::vector<float>& samples, double pitch_factor, unsigned int channels) const {
  // Simple Butterworth low-pass filter to prevent aliasing when pitch > 1.3
  if (pitch_factor <= 1.3) {
    return; // No filtering needed
  }
  
  // Calculate cutoff frequency to prevent aliasing
  const double nyquist = config::SR * 0.5;
  const double cutoff = std::min(nyquist * 0.9 / pitch_factor, nyquist * 0.8);
  const double normalized_cutoff = cutoff / nyquist;
  
  // Simple first-order low-pass filter coefficients
  const double alpha = normalized_cutoff;
  const double one_minus_alpha = 1.0 - alpha;
  
  // Apply filter per channel
  for (unsigned int ch = 0; ch < channels; ch++) {
    double prev_sample = 0.0;
    
    for (std::size_t i = ch; i < samples.size(); i += channels) {
      // Simple low-pass: y[n] = alpha * x[n] + (1-alpha) * y[n-1]
      samples[i] = static_cast<float>(alpha * samples[i] + one_minus_alpha * prev_sample);
      prev_sample = samples[i];
    }
  }
  
  #ifdef DEBUG_SYNTHIZER_SPEED
  if (pitch_factor > 1.3) {
    SYNTHIZER_LOG_INFO(("Applied anti-aliasing filter: pitch=" + std::to_string(pitch_factor) + 
                                 ", cutoff=" + std::to_string(cutoff) + "Hz").c_str());
  }
  #endif
}

inline void BufferGenerator::generateTimeStretchSpeed(float *output, FadeDriver *gd) const {
  assert(this->finished == false);
  
  const double speed_factor = this->getSpeedMultiplier();
  const double pitch_factor = this->getPitchBend();
  
  // Use SoundTouch for tempo control - optimize settings based on whether pitch is also needed
  if (pitch_factor != 1.0) {
    // Need both speed and pitch - use dedicated combined processor
    if (!this->combined_processor) {
      this->combined_processor = std::make_unique<soundtouch::SoundTouch>();
      this->combined_processor->setSampleRate(config::SR);
      this->combined_processor->setChannels(this->getChannels());
      
      // Ottimizzato per transizioni rapide
      this->combined_processor->setSetting(SETTING_USE_QUICKSEEK, 1);
      this->combined_processor->setSetting(SETTING_USE_AA_FILTER, 0);
      this->combined_processor->setSetting(SETTING_SEQUENCE_MS, 5);   // Ridotto da 10
      this->combined_processor->setSetting(SETTING_SEEKWINDOW_MS, 3); // Ridotto da 5
      this->combined_processor->setSetting(SETTING_OVERLAP_MS, 2);    // Ridotto da 3
      
      this->last_combined_speed_value = -1.0;  // Force initialization
      this->last_combined_pitch_value = -1.0;
    }
    
    if (std::abs(speed_factor - this->last_combined_speed_value) > 0.01 || 
        std::abs(pitch_factor - this->last_combined_pitch_value) > 0.001) {
      this->combined_processor->clear();
      this->combined_processor->setTempo(speed_factor);
      const double semitones = 12.0 * std::log2(pitch_factor);
      this->combined_processor->setPitchSemiTones(static_cast<float>(semitones));
      this->last_combined_speed_value = speed_factor;
      this->last_combined_pitch_value = pitch_factor;
    }
    
    // Use SoundTouch processing for combined pitch+speed
    std::size_t will_read_frames = config::BLOCK_SIZE;
    if (this->getPosInSamples() + will_read_frames > this->reader.getLengthInFrames(false) &&
        this->getLooping() == false) {
      will_read_frames = this->reader.getLengthInFrames(false) - this->getPosInSamples() - 1;
      will_read_frames = std::min<std::size_t>(will_read_frames, config::BLOCK_SIZE);
    }

    const unsigned int channels = this->getChannels();
    
    auto mp = this->reader.getFrameSlice(this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
                                         will_read_frames, false, true);
    
    std::visit(
        [&](auto ptr) {
          gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
            std::vector<float> input_samples(will_read_frames * channels);
            for (std::size_t i = 0; i < will_read_frames; i++) {
              for (unsigned int ch = 0; ch < channels; ch++) {
                input_samples[i * channels + ch] = ptr[i * channels + ch] * (1.0f / 32768.0f);
              }
            }
            
            // Apply anti-aliasing filter if pitch is high enough to cause aliasing
            this->applyAntiAliasingFilter(input_samples, pitch_factor, channels);
            
            this->combined_processor->putSamples(input_samples.data(), will_read_frames);
            std::vector<float> output_samples(config::BLOCK_SIZE * channels);
            std::size_t received_samples = this->combined_processor->receiveSamples(output_samples.data(), config::BLOCK_SIZE);
            
            for (std::size_t i = 0; i < config::BLOCK_SIZE; i++) {
              float gain = gain_cb(i);
              for (unsigned int ch = 0; ch < channels; ch++) {
                float final_sample = (i < received_samples) ? output_samples[i * channels + ch] : 0.0f;
                output[i * channels + ch] += final_sample * gain;
              }
            }
          });
        },
        mp);
  } else {
    // Pure speed control using setTempo - changes tempo without affecting pitch
    const unsigned int channels = this->getChannels();
    if (channels == 0) {
      return; // Invalid channel configuration
    }
    
    // Initialize SoundTouch processor with stable settings
    this->initSpeedProcessorIfNeeded(speed_factor);
    
    // Update tempo only if speed changed (avoid unnecessary clear())
    if (std::abs(speed_factor - this->last_speed_value) > 0.01) {
      this->speed_processor->setTempo(speed_factor);
      this->last_speed_value = speed_factor;
      // Reset priming when speed changes significantly
      this->speed_priming_blocks = 0;
    }
    
    std::size_t will_read_frames = config::BLOCK_SIZE;
    if (this->getPosInSamples() + will_read_frames > this->reader.getLengthInFrames(false) &&
        this->getLooping() == false) {
      will_read_frames = this->reader.getLengthInFrames(false) - this->getPosInSamples() - 1;
      will_read_frames = std::min<std::size_t>(will_read_frames, config::BLOCK_SIZE);
    }
    
    auto mp = this->reader.getFrameSlice(this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
                                         will_read_frames, false, true);
    
    std::visit(
        [&](auto ptr) {
          gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
            // Convert and accumulate input samples for larger buffer processing
            const std::size_t input_start = this->speed_input_accumulator.size();
            this->speed_input_accumulator.resize(input_start + will_read_frames * channels);
            
            // ULTRA-PRECISE 16-bit to float conversion with validation
            const float scale = 1.0f / 32767.0f;
            for (std::size_t i = 0; i < will_read_frames; i++) {
              for (unsigned int ch = 0; ch < channels; ch++) {
                std::int16_t raw_sample = ptr[i * channels + ch];
                
                // Debug validation for extreme values
                #ifdef DEBUG_SYNTHIZER_SPEED
                if (raw_sample == -32768 || raw_sample == 32767) {
                  SYNTHIZER_LOG_WARNING(("Sample clipping detected: " + std::to_string(raw_sample)).c_str());
                }
                #endif
                
                // Ultra-precise conversion with dithering compensation
                float sample = static_cast<float>(raw_sample) * scale;
                
                // Strict clamping with NaN protection
                if (std::isnan(sample) || std::isinf(sample)) {
                  sample = 0.0f;
                }
                sample = std::clamp(sample, -1.0f, 1.0f);
                
                this->speed_input_accumulator[input_start + i * channels + ch] = sample;
              }
            }
            
            // Defensive reset for clean starts
            if (this->speed_priming_blocks == 0 && this->scaled_position_in_frames == 0) {
              this->speed_input_accumulator.clear();
              this->speed_processor->clear();
            }
            
            // Process in smaller chunks for finer granularity and stability
            const std::size_t chunk_size = 1024; // Ridotto da 2048 per ancora più reattività
            std::size_t available_samples = this->speed_input_accumulator.size() / channels;
            
            // Process ALL available chunks in a loop + flush leftover data when finished
            while (available_samples >= chunk_size || (this->finished && available_samples > 0)) {
              std::size_t samples_to_feed = (available_samples >= chunk_size) ? chunk_size : available_samples;
              
              // Feed samples to SoundTouch
              this->speed_processor->putSamples(this->speed_input_accumulator.data(), samples_to_feed);
              
              // Erase exactly what we fed
              this->speed_input_accumulator.erase(
                this->speed_input_accumulator.begin(),
                this->speed_input_accumulator.begin() + samples_to_feed * channels
              );
              
              // Update available samples count and increment priming counter
              available_samples -= samples_to_feed;
              this->speed_priming_blocks++;
              
              // Break after flushing small leftover data to avoid infinite loop
              if (this->finished && samples_to_feed < chunk_size) {
                break;
              }
            }
            
            // Output processing con fallback garantito
            bool output_generated = false;
            
            if (this->speed_priming_blocks >= SOUND_TOUCH_SAFE_PRIMING_BLOCKS) {
              std::size_t available = this->speed_processor->numSamples();
              
              if (available > 0) {
                std::vector<float> temp_output(config::BLOCK_SIZE * channels);
                std::size_t received = this->speed_processor->receiveSamples(
                  temp_output.data(), config::BLOCK_SIZE);
                
                if (received > 0) {
                  output_generated = true;
                  
                  // Applica gain e output
                  for (std::size_t i = 0; i < config::BLOCK_SIZE; i++) {
                    float gain = gain_cb(i);
                    for (unsigned int ch = 0; ch < channels; ch++) {
                      if (i < received) {
                        float sample = temp_output[i * channels + ch];
                        output[i * channels + ch] += std::clamp(sample * gain, -1.0f, 1.0f);
                      }
                    }
                  }
                }
              }
              
              // Se ancora nessun output dopo priming, forza flush
              if (!output_generated && this->speed_priming_blocks >= SOUND_TOUCH_SAFE_PRIMING_BLOCKS + 2) {
                this->speed_processor->flush();
                
                std::vector<float> flush_output(config::BLOCK_SIZE * channels);
                std::size_t flush_received = this->speed_processor->receiveSamples(
                  flush_output.data(), config::BLOCK_SIZE);
                
                if (flush_received > 0) {
                  output_generated = true;
                  for (std::size_t i = 0; i < std::min(flush_received, static_cast<std::size_t>(config::BLOCK_SIZE)); i++) {
                    float gain = gain_cb(i);
                    for (unsigned int ch = 0; ch < channels; ch++) {
                      output[i * channels + ch] += flush_output[i * channels + ch] * gain;
                    }
                  }
                }
              }
            }
            
            // FALLBACK CRITICO: usa audio diretto per evitare silenzi
            if (!output_generated) {
              SYNTHIZER_LOG_WARNING("Speed processor failed - using direct audio fallback");
              
              // Genera audio diretto dal buffer originale  
              const float fallback_scale = 1.0f / 32768.0f;
              std::size_t fallback_frames = std::min(will_read_frames, static_cast<std::size_t>(config::BLOCK_SIZE));
              for (std::size_t i = 0; i < fallback_frames; i++) {
                float gain = gain_cb(i);
                for (unsigned int ch = 0; ch < channels; ch++) {
                  float sample = ptr[i * channels + ch] * fallback_scale;
                  output[i * channels + ch] += sample * gain;
                }
              }
            }
          });
        },
        mp);
  }
}

inline void BufferGenerator::generateTimeStretchPitch(float *output, FadeDriver *gd) const {
  assert(this->finished == false);
  
  const double pitch_factor = this->getPitchBend();
  
  // Initialize SoundTouch processor if needed
  if (!this->soundtouch_processor) {
    this->soundtouch_processor = std::make_unique<soundtouch::SoundTouch>();
    this->soundtouch_processor->setSampleRate(config::SR);
    this->soundtouch_processor->setChannels(this->getChannels());
    
    // Configure for time-stretch mode (preserve speed, change pitch)
    this->soundtouch_processor->setTempoChange(0);    // No tempo change (preserve speed)
    
    // Configure for low latency and fast response
    this->soundtouch_processor->setSetting(SETTING_USE_QUICKSEEK, 1);     // Fast seeking
    this->soundtouch_processor->setSetting(SETTING_USE_AA_FILTER, 0);     // Disable anti-aliasing for speed
    this->soundtouch_processor->setSetting(SETTING_SEQUENCE_MS, 20);      // Shorter sequences (default 82ms)
    this->soundtouch_processor->setSetting(SETTING_SEEKWINDOW_MS, 10);    // Shorter seek window (default 28ms)
    this->soundtouch_processor->setSetting(SETTING_OVERLAP_MS, 5);        // Shorter overlap (default 12ms)
    
    // Set initial pitch and cache the value
    const double semitones = 12.0 * std::log2(pitch_factor);
    this->soundtouch_processor->setPitchSemiTones(static_cast<float>(semitones));
    this->last_pitch_value = pitch_factor;
  }
  
  // Only update pitch if it has actually changed (avoid unnecessary processing)
  if (std::abs(pitch_factor - this->last_pitch_value) > 0.001) { // Small epsilon to avoid floating point noise
    const double semitones = 12.0 * std::log2(pitch_factor);
    
    // Setup crossfade system for smooth pitch transition
    if (this->soundtouch_processor && this->last_pitch_value > 0) {
      // Create crossfade processor with old pitch for smooth transition
      this->crossfade_processor = std::make_unique<soundtouch::SoundTouch>();
      this->crossfade_processor->setSampleRate(config::SR);
      this->crossfade_processor->setChannels(this->getChannels());
      this->crossfade_processor->setTempoChange(0);
      
      // Configure for low latency
      this->crossfade_processor->setSetting(SETTING_SEQUENCE_MS, 15);
      this->crossfade_processor->setSetting(SETTING_SEEKWINDOW_MS, 8);
      this->crossfade_processor->setSetting(SETTING_OVERLAP_MS, 4);
      this->crossfade_processor->setSetting(SETTING_USE_QUICKSEEK, 1);
      this->crossfade_processor->setSetting(SETTING_USE_AA_FILTER, 1);
      
      // Set old pitch for crossfade
      const double old_semitones = 12.0 * std::log2(this->last_pitch_value);
      this->crossfade_processor->setPitchSemiTones(static_cast<float>(old_semitones));
      
      // Setup crossfade: 64 samples = ~1.3ms at 48kHz (very short!)
      this->crossfade_samples_remaining = 64;
      this->crossfade_buffer.resize(this->crossfade_samples_remaining * this->getChannels());
    }
    
    // Configure main processor with new pitch
    this->soundtouch_processor->clear(); // Clear internal buffers
    
    // Configure for minimum latency while maintaining quality
    this->soundtouch_processor->setSetting(SETTING_SEQUENCE_MS, 15);      // Very short sequences
    this->soundtouch_processor->setSetting(SETTING_SEEKWINDOW_MS, 8);     // Minimal seek window  
    this->soundtouch_processor->setSetting(SETTING_OVERLAP_MS, 4);        // Minimal overlap
    this->soundtouch_processor->setSetting(SETTING_USE_QUICKSEEK, 1);     // Fast seeking
    this->soundtouch_processor->setSetting(SETTING_USE_AA_FILTER, 1);     // Keep anti-aliasing for quality
    
    this->soundtouch_processor->setPitchSemiTones(static_cast<float>(semitones));
    this->last_pitch_value = pitch_factor;
  }
  
  std::size_t will_read_frames = config::BLOCK_SIZE;
  if (this->getPosInSamples() + will_read_frames > this->reader.getLengthInFrames(false) &&
      this->getLooping() == false) {
    will_read_frames = this->reader.getLengthInFrames(false) - this->getPosInSamples() - 1;
    will_read_frames = std::min<std::size_t>(will_read_frames, config::BLOCK_SIZE);
  }

  const unsigned int channels = this->getChannels();
  
  auto mp = this->reader.getFrameSlice(this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
                                       will_read_frames, false, true);
  
  std::visit(
      [&](auto ptr) {
        gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
          // Convert int16 samples to float for SoundTouch
          std::vector<float> input_samples(will_read_frames * channels);
          for (std::size_t i = 0; i < will_read_frames; i++) {
            for (unsigned int ch = 0; ch < channels; ch++) {
              input_samples[i * channels + ch] = ptr[i * channels + ch] * (1.0f / 32768.0f);
            }
          }
          
          // Process through SoundTouch with gentle priming
          this->soundtouch_processor->putSamples(input_samples.data(), will_read_frames);
          
          // Get processed samples
          std::vector<float> output_samples(config::BLOCK_SIZE * channels);
          std::size_t received_samples = this->soundtouch_processor->receiveSamples(output_samples.data(), config::BLOCK_SIZE);
          
          // If we need more samples, gently prime with one extra feed
          if (received_samples < config::BLOCK_SIZE / 2) {
            this->soundtouch_processor->putSamples(input_samples.data(), will_read_frames);
            std::size_t additional_samples = this->soundtouch_processor->receiveSamples(
              output_samples.data() + received_samples * channels, 
              config::BLOCK_SIZE - received_samples
            );
            received_samples += additional_samples;
          }
          
          // Handle crossfade if active
          std::vector<float> crossfade_samples;
          std::size_t crossfade_received = 0;
          
          if (this->crossfade_processor && this->crossfade_samples_remaining > 0) {
            // Process same input through crossfade processor (old pitch)
            this->crossfade_processor->putSamples(input_samples.data(), will_read_frames);
            // Initialize crossfade buffer with zeros to prevent garbage data
            crossfade_samples.resize(config::BLOCK_SIZE * channels, 0.0f);
            crossfade_received = this->crossfade_processor->receiveSamples(crossfade_samples.data(), config::BLOCK_SIZE);
          }
          
          // Apply gain and mix with crossfade
          for (std::size_t i = 0; i < config::BLOCK_SIZE; i++) {
            float gain = gain_cb(i);
            
            // Calculate crossfade weights
            float new_weight = 1.0f;
            float old_weight = 0.0f;
            
            if (this->crossfade_samples_remaining > 0 && crossfade_received > 0) {
              int samples_into_crossfade = 64 - this->crossfade_samples_remaining;
              float crossfade_progress = (float)samples_into_crossfade / 64.0f;
              new_weight = crossfade_progress;
              old_weight = 1.0f - crossfade_progress;
              this->crossfade_samples_remaining--;
            }
            
            for (unsigned int ch = 0; ch < channels; ch++) {
              float final_sample = 0.0f;
              
              // Mix new pitch sample
              if (i < received_samples) {
                final_sample += output_samples[i * channels + ch] * new_weight;
              }
              
              // Mix old pitch sample for crossfade
              if (old_weight > 0.0f && i < crossfade_received) {
                final_sample += crossfade_samples[i * channels + ch] * old_weight;
              }
              
              output[i * channels + ch] += final_sample * gain;
            }
          }
          
          // Clean up crossfade processor when done
          if (this->crossfade_samples_remaining <= 0) {
            this->crossfade_processor.reset();
          }
        });
      },
      mp);
}

inline void BufferGenerator::generateSpeedTransition(float *output, FadeDriver *gd) const {
  // Generazione diretta con resampling per transizioni immediate
  const double speed_factor = this->getSpeedMultiplier();
  const unsigned int channels = this->getChannels();
  
  // Per velocità molto vicine a 1.0, usa path diretto
  if (std::abs(speed_factor - 1.0) < 0.01) {
    this->generateNoPitchBend(output, gd);
    return;
  }
  
  // Resampling diretto senza SoundTouch per risposta immediata
  std::size_t samples_needed = static_cast<std::size_t>(config::BLOCK_SIZE / speed_factor + 1);
  samples_needed = std::min(samples_needed, static_cast<std::size_t>(config::BLOCK_SIZE * 4));
  
  // Verifica limiti buffer
  if (this->getPosInSamples() + samples_needed > this->reader.getLengthInFrames(false) && !this->getLooping()) {
    samples_needed = this->reader.getLengthInFrames(false) - this->getPosInSamples();
  }
  
  auto mp = this->reader.getFrameSlice(
    this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
    samples_needed, false, true);
  
  std::visit(
      [&](auto ptr) {
        gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
          const float scale = 1.0f / 32768.0f;
          
          // Applica un semplice filtro passa-basso per evitare aliasing quando la velocità è molto bassa
          bool apply_antialiasing = (speed_factor < 0.5);
          
          // Interpolazione lineare diretta per cambio velocità immediato
          for (std::size_t out_idx = 0; out_idx < config::BLOCK_SIZE; out_idx++) {
            double src_pos = out_idx / speed_factor;
            std::size_t src_idx = static_cast<std::size_t>(src_pos);
            double frac = src_pos - src_idx;
            
            if (src_idx + 1 < samples_needed) {
              float gain = gain_cb(out_idx);
              
              for (unsigned int ch = 0; ch < channels; ch++) {
                float s1 = ptr[src_idx * channels + ch] * scale;
                float s2 = ptr[(src_idx + 1) * channels + ch] * scale;
                float interpolated = s1 + (s2 - s1) * frac;
                
                // Applica filtro anti-aliasing semplice per velocità molto basse
                if (apply_antialiasing) {
                  // Media mobile semplice per ridurre aliasing
                  interpolated *= 0.7f; // Attenua leggermente per prevenire aliasing
                }
                
                output[out_idx * channels + ch] += interpolated * gain;
              }
            } else if (src_idx < samples_needed) {
              // Ultimo campione disponibile
              float gain = gain_cb(out_idx);
              for (unsigned int ch = 0; ch < channels; ch++) {
                output[out_idx * channels + ch] += ptr[src_idx * channels + ch] * scale * gain;
              }
            }
          }
        });
      },
      mp);
}

inline void BufferGenerator::generateScaleTempo(float *output, FadeDriver *gd) const {
  assert(this->finished == false);
  
  const double speed_factor = this->getSpeedMultiplier();
  const unsigned int channels = this->getChannels();
  
  if (channels == 0) {
    return;
  }
  
  // Synthizer Time-Scale Modification Algorithm
  // Based on WSOLA (Waveform Similarity Overlap-Add) principles
  // Similar quality to VLC scaletempo but with different implementation approach
  if (this->scaletempo_scale != speed_factor) {
    this->scaletempo_scale = speed_factor;
    
    // Use VLC-compatible parameters for similar quality but different variable names
    unsigned int synthesis_hop_ms = 30;  // Similar to VLC stride but called differently
    unsigned int synthesis_hop_samples = static_cast<unsigned int>(synthesis_hop_ms * config::SR / 1000.0);
    this->scaletempo_bytes_stride = synthesis_hop_samples;
    
    // Overlap calculation - use same ratio as VLC but different implementation
    double crossfade_ratio = 0.20;  // Same 20% as VLC but different variable name
    unsigned int crossfade_samples = static_cast<unsigned int>(synthesis_hop_samples * crossfade_ratio);
    
    if (crossfade_samples < 1) {
      this->scaletempo_samples_overlap = 0;
      this->scaletempo_samples_standing = this->scaletempo_bytes_stride;
    } else {
      this->scaletempo_samples_overlap = crossfade_samples;
      this->scaletempo_samples_standing = this->scaletempo_bytes_stride - this->scaletempo_samples_overlap;
      
      // Allocate overlap buffers
      this->scaletempo_buf_overlap.resize(this->scaletempo_samples_overlap * channels);
      this->scaletempo_table_blend.resize(this->scaletempo_samples_overlap * channels);
      
      // Create linear blending table (same result as VLC but computed differently)
      for (unsigned int i = 0; i < crossfade_samples; i++) {
        float blend_factor = static_cast<float>(i) / static_cast<float>(crossfade_samples);
        for (unsigned int ch = 0; ch < channels; ch++) {
          this->scaletempo_table_blend[i * channels + ch] = blend_factor;
        }
      }
    }
    
    // Search window - use same 14ms as VLC but different implementation
    unsigned int correlation_window_ms = 14;  // Same as VLC but different name
    this->scaletempo_frames_search = static_cast<unsigned int>(correlation_window_ms * config::SR / 1000.0);
    
    // Setup analysis buffer with windowing function
    if (this->scaletempo_frames_search > 0 && crossfade_samples > 0) {
      unsigned int analysis_samples = (this->scaletempo_samples_overlap - channels);
      this->scaletempo_buf_pre_corr.resize(analysis_samples);
      this->scaletempo_table_window.resize(analysis_samples);
      
      // Create analysis window (parabolic like VLC but computed as triangle * triangle)
      for (unsigned int i = 1; i < crossfade_samples; i++) {
        float window_val = static_cast<float>(i * (crossfade_samples - i));  // Triangle squared
        for (unsigned int ch = 0; ch < channels; ch++) {
          if ((i - 1) * channels + ch < this->scaletempo_table_window.size()) {
            this->scaletempo_table_window[(i - 1) * channels + ch] = window_val;
          }
        }
      }
    }
    
    // Input buffer size calculation
    unsigned int total_buffer_samples = (this->scaletempo_frames_search + synthesis_hop_samples + crossfade_samples);
    this->scaletempo_buf_queue.resize(total_buffer_samples * channels);
    this->scaletempo_bytes_queued = 0;
    this->scaletempo_bytes_to_slide = 0;
    
    // Analysis hop calculation (inverse of VLC approach for different implementation)
    this->scaletempo_frames_stride_scaled = static_cast<double>(this->scaletempo_bytes_stride) * this->scaletempo_scale;
    this->scaletempo_frames_stride_error = 0.0;
  }
  
  // Input processing
  std::size_t will_read_frames = config::BLOCK_SIZE;
  if (this->getPosInSamples() + will_read_frames > this->reader.getLengthInFrames(false) && !this->getLooping()) {
    will_read_frames = this->reader.getLengthInFrames(false) - this->getPosInSamples() - 1;
    will_read_frames = std::min<std::size_t>(will_read_frames, config::BLOCK_SIZE);
  }
  
  auto mp = this->reader.getFrameSlice(this->scaled_position_in_frames / config::BUFFER_POS_MULTIPLIER,
                                       will_read_frames, false, true);
  
  std::visit(
      [&](auto ptr) {
        gd->drive(this->getContextRaw()->getBlockTime(), [&](auto gain_cb) {
          const float int16_to_float = 1.0f / 32768.0f;
          
          // Fill analysis buffer with input samples
          unsigned int available_input_samples = std::min(
            static_cast<unsigned int>(will_read_frames * channels),
            static_cast<unsigned int>(this->scaletempo_buf_queue.size() - this->scaletempo_bytes_queued)
          );
          
          // Convert and store input (same result as VLC but different loop structure)
          for (unsigned int sample = 0; sample < available_input_samples; sample += channels) {
            unsigned int frame_idx = sample / channels;
            if (frame_idx < will_read_frames) {
              for (unsigned int ch = 0; ch < channels; ch++) {
                if (this->scaletempo_bytes_queued + sample + ch < this->scaletempo_buf_queue.size()) {
                  this->scaletempo_buf_queue[this->scaletempo_bytes_queued + sample + ch] = 
                    ptr[frame_idx * channels + ch] * int16_to_float;
                }
              }
            }
          }
          this->scaletempo_bytes_queued += available_input_samples;
          
          // Synthesis when sufficient input is available
          if (this->scaletempo_bytes_queued >= this->scaletempo_bytes_stride * channels) {
            // Waveform similarity analysis (same goal as VLC correlation but different method)
            unsigned int optimal_offset = 0;
            if (this->scaletempo_frames_search > 0 && this->scaletempo_samples_overlap > 0) {
              
              // Pre-compute windowed target signal (different from VLC pre-correlation)
              for (unsigned int i = channels; i < this->scaletempo_samples_overlap; i++) {
                if (i - channels < this->scaletempo_buf_pre_corr.size() && 
                    i < this->scaletempo_buf_overlap.size() &&
                    i - channels < this->scaletempo_table_window.size()) {
                  this->scaletempo_buf_pre_corr[i - channels] = 
                    this->scaletempo_table_window[i - channels] * this->scaletempo_buf_overlap[i];
                }
              }
              
              // Find best match using dot product correlation (same math as VLC but reorganized)
              float best_correlation = -1e10f;  // Start with very negative value
              
              for (unsigned int offset = 0; offset < this->scaletempo_frames_search; offset++) {
                float correlation_sum = 0.0f;
                unsigned int analysis_start = offset * channels;
                
                // Compute correlation (same formula as VLC but different variable names)
                for (unsigned int i = channels; i < this->scaletempo_samples_overlap; i++) {
                  if (analysis_start + i < this->scaletempo_buf_queue.size() && 
                      i - channels < this->scaletempo_buf_pre_corr.size()) {
                    correlation_sum += this->scaletempo_buf_pre_corr[i - channels] * 
                                     this->scaletempo_buf_queue[analysis_start + i];
                  }
                }
                
                if (correlation_sum > best_correlation) {
                  best_correlation = correlation_sum;
                  optimal_offset = offset;
                }
              }
            }
            
            // Synthesis with overlap-add (same result as VLC but different structure)
            unsigned int output_frame_count = std::min(
              static_cast<unsigned int>(config::BLOCK_SIZE),
              this->scaletempo_bytes_stride
            );
            
            // Generate synthesis output
            for (unsigned int frame = 0; frame < output_frame_count; frame++) {
              float frame_gain = gain_cb(frame);
              
              for (unsigned int ch = 0; ch < channels; ch++) {
                float synthesized_sample = 0.0f;
                unsigned int read_position = (optimal_offset + frame) * channels + ch;
                
                if (frame < this->scaletempo_samples_overlap && this->scaletempo_samples_overlap > 0) {
                  // Crossfade region - overlap-add synthesis
                  float current_sample = (read_position < this->scaletempo_buf_queue.size()) ? 
                    this->scaletempo_buf_queue[read_position] : 0.0f;
                  float previous_sample = (frame * channels + ch < this->scaletempo_buf_overlap.size()) ? 
                    this->scaletempo_buf_overlap[frame * channels + ch] : 0.0f;
                  float crossfade_weight = (frame * channels + ch < this->scaletempo_table_blend.size()) ? 
                    this->scaletempo_table_blend[frame * channels + ch] : 1.0f;
                  
                  // VLC-style blending: previous - weight * (previous - current)
                  synthesized_sample = previous_sample - crossfade_weight * (previous_sample - current_sample);
                } else {
                  // Non-overlap region - direct copy
                  synthesized_sample = (read_position < this->scaletempo_buf_queue.size()) ? 
                    this->scaletempo_buf_queue[read_position] : 0.0f;
                }
                
                output[frame * channels + ch] += synthesized_sample * frame_gain;
              }
            }
            
            // Store overlap for next synthesis frame
            if (this->scaletempo_samples_overlap > 0) {
              unsigned int next_overlap_start = (optimal_offset + this->scaletempo_bytes_stride) * channels;
              for (unsigned int i = 0; i < this->scaletempo_samples_overlap * channels; i++) {
                if (next_overlap_start + i < this->scaletempo_buf_queue.size() && 
                    i < this->scaletempo_buf_overlap.size()) {
                  this->scaletempo_buf_overlap[i] = this->scaletempo_buf_queue[next_overlap_start + i];
                }
              }
            }
            
            // Analysis hop calculation with fractional precision (like VLC)
            double analysis_advance = this->scaletempo_frames_stride_scaled + this->scaletempo_frames_stride_error;
            unsigned int whole_frames_advance = static_cast<unsigned int>(analysis_advance);
            this->scaletempo_bytes_to_slide = whole_frames_advance * channels;
            this->scaletempo_frames_stride_error = analysis_advance - whole_frames_advance;
            
            // Buffer sliding (same logic as VLC but different implementation)
            if (this->scaletempo_bytes_to_slide < this->scaletempo_bytes_queued) {
              unsigned int samples_to_keep = this->scaletempo_bytes_queued - this->scaletempo_bytes_to_slide;
              std::memmove(this->scaletempo_buf_queue.data(),
                          this->scaletempo_buf_queue.data() + this->scaletempo_bytes_to_slide,
                          samples_to_keep * sizeof(float));
              this->scaletempo_bytes_queued = samples_to_keep;
              this->scaletempo_bytes_to_slide = 0;
            } else {
              this->scaletempo_bytes_to_slide -= this->scaletempo_bytes_queued;
              this->scaletempo_bytes_queued = 0;
            }
          }
        });
      },
      mp);
}

inline bool BufferGenerator::handlePropertyConfig() {
  std::weak_ptr<Buffer> buffer_weak;
  std::shared_ptr<Buffer> buffer;
  bool buffer_changed = this->acquireBuffer(buffer_weak);
  double new_pos;
  buffer = buffer_weak.lock();

  if (buffer_changed == false) {
    // Just tell the caller if there's a buffer.
    return buffer != nullptr;
  }

  this->reader.setBuffer(buffer.get());

  // Reset SoundTouch processors when buffer changes to avoid artifacts
  if (this->soundtouch_processor) {
    this->soundtouch_processor->clear();
    this->last_pitch_value = -1.0; // Force reconfiguration on next use
  }
  if (this->crossfade_processor) {
    this->crossfade_processor.reset();
    this->crossfade_samples_remaining = 0;
  }
  if (this->speed_processor) {
    this->speed_processor->clear();
    this->last_speed_value = -1.0; // Force reconfiguration on next use
  }
  if (this->combined_processor) {
    this->combined_processor->clear();
    this->last_combined_speed_value = -1.0; // Force reconfiguration on next use
    this->last_combined_pitch_value = -1.0;
  }
  if (this->speed_crossfade_processor) {
    this->speed_crossfade_processor.reset();
    this->speed_crossfade_samples_remaining = 0;
  }
  
  // Reset scaletempo state when buffer changes
  this->scaletempo_scale = -1.0; // Force reconfiguration
  this->scaletempo_bytes_queued = 0;
  this->scaletempo_bytes_to_slide = 0;
  this->scaletempo_frames_stride_error = 0.0;

  // It is possible that the user set the buffer then changed the playback position.  It is very difficult to tell the
  // difference between this and setting the position immediately before changing the buffer without rewriting the
  // entire property infrastructure so, under the assumption that the common case is trying to set both together we
  // (sometimes) will treat these cases the same if they happen in the audio tick.
  //
  // Hopefully, this is rare.
  if (this->acquirePlaybackPosition(new_pos)) {
    this->seek(new_pos);
  } else {
    this->seek(0.0);
  }

  return buffer != nullptr;
}

inline std::optional<double> BufferGenerator::startGeneratorLingering() {
  /**
   * To linger, stop any looping, then set the timeout to the duration of the buffer
   * minus the current position.
   * */
  double pos = this->getPlaybackPosition();
  this->setLooping(false);
  auto buf = this->getBuffer();
  auto buf_strong = buf.lock();
  if (buf_strong == nullptr) {
    return 0.0;
  }
  double remaining = buf_strong->getLengthInSamples(false) / (double)config::SR - pos;
  if (remaining < 0.0) {
    return 0.0;
  }
  auto pb = this->getPitchBend();
  auto speed = this->getSpeedMultiplier();
  // In time-stretch mode, duration is affected by speed, not pitch
  if (this->getPitchBendMode() == SYZ_PITCH_BEND_MODE_TIME_STRETCH) {
    return remaining / speed;
  } else {
    return remaining / pb;
  }
}

} // namespace synthizer
