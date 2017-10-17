// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEMORY_STREAM_H_
#define SRC_MEMORY_STREAM_H_

#include <string>
#include <utility>

#include "puffin/common.h"
#include "puffin/stream.h"

namespace puffin {

// A very simple class for reading and writing into memory.
class MemoryStream : public StreamInterface {
 public:
  ~MemoryStream() override = default;

  // The input buffer |memory| can grow as we write into it.
  static UniqueStreamPtr Create(SharedBufferPtr memory, bool read, bool write);

  bool GetSize(size_t* size) const override;
  bool GetOffset(size_t* offset) const override;
  bool Seek(size_t offset) override;
  bool Read(void* buffer, size_t length) override;
  bool Write(const void* buffer, size_t length) override;
  bool Close() override;

 protected:
  MemoryStream(SharedBufferPtr memory, bool read, bool write);

 private:
  // The memory buffer.
  SharedBufferPtr memory_;

  // The current offset.
  size_t pos_;

  // True if this stream is opened for reading.
  bool read_;

  // True if this stream is opened for writing.
  bool write_;

  // True if the |Close()| is called.
  bool closed_;

  DISALLOW_COPY_AND_ASSIGN(MemoryStream);
};

}  // namespace puffin

#endif  // SRC_MEMORY_STREAM_H_
