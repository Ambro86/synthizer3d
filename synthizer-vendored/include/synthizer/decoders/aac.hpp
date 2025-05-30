#pragma once

#include <memory> // Per std::shared_ptr

// Includi gli header di Synthizer necessari per le dichiarazioni sottostanti
#include "synthizer/decoding.hpp"     // Per AudioDecoder
#include "synthizer/byte_stream.hpp"  // Per LookaheadByteStream

namespace synthizer {

/**
 * @brief Crea un decodificatore audio per stream AAC.
 * * Questa funzione tenta di creare un decodificatore per il formato AAC.
 * L'implementazione interna ora utilizza FFmpeg.
 * * @param stream_ptr Un puntatore condiviso a un LookaheadByteStream che fornisce i dati AAC.
 * @return Un puntatore condiviso a un AudioDecoder se la decodifica può essere gestita,
 * altrimenti nullptr.
 */
std::shared_ptr<AudioDecoder> decodeAac(std::shared_ptr<LookaheadByteStream> stream_ptr);

} // namespace synthizer