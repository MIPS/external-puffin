// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PUFFIN_STREAM_H_
#define SRC_PUFFIN_STREAM_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/stream.h"

namespace puffin {

// A class for puffing a deflate stream and huffing into a deflate stream. The
// puff stream is "imaginary", which means it doesn't really exists; It is build
// and used on demand. This class uses a given deflate stream, and puffs the
// deflate buffers in the stream as needed or vice versa.  An object of this
// class can be used for reading and writing puff data but should not be used
// for both reading and writing using the same instance. In theory we can
// separate this class into two classes, namely |PuffStream| and |HuffStream|,
// but they are sharing a lot of codes which might be inconvenient and
// unnecessary to do so. In this implementation, there is no protection against
// reading and writing at the same time.
class PuffinStream : public StreamInterface {
 public:
  ~PuffinStream() override = default;

  // Creates a |PuffinStream| for reading puff buffers from a deflate stream.
  // |stream|    IN  The deflate stream.
  // |puffer|    IN  The |Puffer| used for puffing the stream.
  // |puff_size| IN  The size of the puff stream (assuming |stream| has been
  //                 completely puffed.
  // |deflates|  IN  The location of deflates in |stream|.
  // |puffs|     IN  The location of puffs into the final puff stream.
  static UniqueStreamPtr CreateForPuff(UniqueStreamPtr stream,
                                       std::shared_ptr<Puffer> puffer,
                                       size_t puff_size,
                                       const std::vector<ByteExtent>& deflates,
                                       const std::vector<ByteExtent>& puffs);

  // Creates a |PuffinStream| for writing puff buffers into a deflate stream.
  // |stream|    IN  The deflate stream.
  // |huffer|    IN  The |Huffer| used for huffing into the |stream|.
  // |puff_size| IN  The size of the puff stream (assuming |stream| has been
  //                 completely puffed.
  // |deflates|  IN  The location of deflates in |stream|.
  // |puffs|     IN  The location of puffs into the input puff stream.
  static UniqueStreamPtr CreateForHuff(UniqueStreamPtr stream,
                                       std::shared_ptr<Huffer> huffer,
                                       size_t puff_size,
                                       const std::vector<ByteExtent>& deflates,
                                       const std::vector<ByteExtent>& puffs);

  bool GetSize(size_t* size) const override;

  // Returns the current offset in the imaginary puff stream.
  bool GetOffset(size_t* offset) const override;

  // Sets the current offset in the imaginary puff stream.
  bool Seek(size_t offset) override;

  // Reads from the deflate stream |stream_| and writes the puff stream into
  // |buffer|.
  bool Read(void* buffer, size_t length) override;

  // Reads the puff stream from |buffer|, huffs it and writes it into the
  // deflate stream |stream_|. The current assumption for write is that data is
  // read from beginning to end with no retraction or random change of offset.
  bool Write(const void* buffer, size_t length) override;

  bool Close() override;

 protected:
  // The non-public internal Ctor.
  PuffinStream(UniqueStreamPtr stream,
               std::shared_ptr<Puffer> puffer,
               std::shared_ptr<Huffer> huffer,
               size_t puff_size,
               const std::vector<ByteExtent>& deflates,
               const std::vector<ByteExtent>& puffs);

 private:
  UniqueStreamPtr stream_;

  std::shared_ptr<Puffer> puffer_;
  std::shared_ptr<Huffer> huffer_;

  // The size of the imaginary puff stream.
  size_t puff_stream_size_;
  std::vector<ByteExtent> deflates_;
  std::vector<ByteExtent> puffs_;

  // The current offset in the imaginary puff stream.
  size_t puff_pos_;

  // The current offset in |stream_|.
  size_t deflate_pos_;

  // True if the stream is only for puffing. False if for huffing.
  bool is_for_puff_;

  // True if the |Close()| is called.
  bool closed_;

  // The current puff is being processed.
  std::vector<ByteExtent>::iterator cur_puff_;

  // The current deflate is being processed.
  std::vector<ByteExtent>::iterator cur_deflate_;

  UniqueBufferPtr deflate_buffer_;
  UniqueBufferPtr puff_buffer_;

  DISALLOW_COPY_AND_ASSIGN(PuffinStream);
};

}  // namespace puffin

#endif  // SRC_PUFFIN_STREAM_H_
