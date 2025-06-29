#pragma once

#include "synthizer/byte_stream.hpp"
#include "synthizer/error.hpp"
#include "synthizer/memory.hpp"

#include <atomic>
#include <memory>

namespace synthizer {

/**
 * A StreamHandle combines the ByteStream interface with the CExposable interface, and forwards ByteStream
 * calls to an underlying ByteStream.
 *
 * This may seem pointless, but it allows users to associate userdata with their streams, and have that userdata freed
 * only when the underlying stream is no longer needed.  Users of this stream hold this object instead of the underlying
 * stream, relying on the forwarding interface.
 *
 * This lets users e.g. create memory streams and associate the lifetime of the buffer with the stream through userdata.
 * */
class StreamHandleBase : public CExposable, public ByteStream {
public:
  /**
   * Mark the stream as consumed, throwing if it already was.
   *
   * This is used to prevent users from using stream handles more than onec.
   * */
  void markConsumed();

  int getObjectType() override;

private:
  /* Stream handles can only be used once. */
  std::atomic<int> consumed = 0;
};

using StreamHandle = ForwardingStream<StreamHandleBase>;

std::shared_ptr<ByteStream> consumeStreamHandle(const std::shared_ptr<StreamHandle> &handle);

inline void StreamHandleBase::markConsumed() {
  /* technically the user can re-consume a handle with 2**32 calls that try to do it.  Don't worry about that case and
   * avoid a cas loop. */
  if (this->consumed.fetch_add(1, std::memory_order_relaxed) != 0) {
    throw EValidation("Cannot use StreamHandle twice");
  }
}

inline int StreamHandleBase::getObjectType() { return SYZ_OTYPE_STREAM_HANDLE; }

inline std::shared_ptr<ByteStream> consumeStreamHandle(const std::shared_ptr<StreamHandle> &handle) {
  handle->markConsumed();
  return std::static_pointer_cast<ByteStream>(handle);
}

} // namespace synthizer
