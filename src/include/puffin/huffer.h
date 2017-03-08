// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_HUFFER_H_
#define SRC_INCLUDE_PUFFIN_HUFFER_H_

#include <cstddef>
#include <memory>

#include "puffin/common.h"
#include "puffin/errors.h"

namespace puffin {

class BitWriterInterface;
class PuffReaderInterface;
class HuffmanTable;

class PUFFIN_EXPORT Huffer {
 public:
  Huffer();
  ~Huffer();

  // It creates a deflate buffer from a puffed buffer. It is the reverse of
  // |PuffDeflate|.
  //
  // |puff_buf|  IN  The puff buffer.
  // |puff_size| IN  The size of the input puffed buffer.
  // |comp_buf|  IN  The output deflate buffer.
  // |comp_size| IN  The size of the deflate buffer.
  // |error|     OUT The error code.
  bool HuffDeflate(const uint8_t* puff_buf,
                   size_t puff_size,
                   uint8_t* comp_buf,
                   size_t comp_size,
                   Error* error) const;

 private:
  // Internal method for creating deflate buffer from a puff buffer.
  bool HuffDeflate(PuffReaderInterface* pr,
                   BitWriterInterface* bw,
                   Error* error) const;

  std::unique_ptr<HuffmanTable> dyn_ht_;
  std::unique_ptr<HuffmanTable> fix_ht_;

  DISALLOW_COPY_AND_ASSIGN(Huffer);
};

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_HUFFER_H_
