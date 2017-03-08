// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_PUFFER_H_
#define SRC_INCLUDE_PUFFIN_PUFFER_H_

#include <memory>
#include <vector>

#include "puffin/common.h"
#include "puffin/errors.h"
#include "puffin/stream.h"

namespace puffin {

class BitReaderInterface;
class PuffWriterInterface;
class HuffmanTable;

class PUFFIN_EXPORT Puffer {
 public:
  Puffer();
  ~Puffer();

  // It creates a puff buffer from a deflate buffer.
  //
  // |comp_buf|  IN     The input deflate buffer.
  // |comp_size| IN     The size of the deflate buffer.
  // |puff_buf|  IN     The output puffed buffer.
  // |puff_size| IN/OUT The size of the output puffed buffer. On return it will
  //                    be the size of the actual puff buffer.
  // |error|     OUT    The error code.
  bool PuffDeflate(const uint8_t* comp_buf,
                   size_t comp_size,
                   uint8_t* puff_buf,
                   size_t* puff_size,
                   Error* error) const;

  // Puffs the deflate stream |src| into puff stream |dst|, This code runs on
  // the server and uses bsdiff internally to create the patch.
  //
  // |src|       IN   Source deflate stream.
  // |dst|       IN   Destination puff stream.
  // |deflates|  IN   Deflate locations in |src_stream|.
  // |puffs|     OUT  Puff locations in |puff_stream|.
  // |error|     OUT    The error code.
  bool Puff(const UniqueStreamPtr& src,
            const UniqueStreamPtr& dst,
            const std::vector<ByteExtent>& deflates,
            std::vector<ByteExtent>* puffs,
            Error* error) const;

 private:
  // An internal function that creates a puffed buffer from a deflate buffer.
  bool PuffDeflate(BitReaderInterface* br,
                   PuffWriterInterface* pw,
                   Error* error) const;

  std::unique_ptr<HuffmanTable> dyn_ht_;
  std::unique_ptr<HuffmanTable> fix_ht_;

  DISALLOW_COPY_AND_ASSIGN(Puffer);
};

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_PUFFER_H_
