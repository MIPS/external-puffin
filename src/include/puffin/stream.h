// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_STREAM_H_
#define SRC_INCLUDE_PUFFIN_STREAM_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "puffin/common.h"

namespace puffin {

// The base stream interface used by puffin for all operations. This interface
// is designed to be as simple as possible.
class StreamInterface {
 public:
  virtual ~StreamInterface() = default;

  // Returns the size of the stream.
  virtual bool GetSize(size_t* size) const = 0;

  // Returns the current offset in the stream where next read or write will
  // happen.
  virtual bool GetOffset(size_t* offset) const = 0;

  // Sets the offset in the stream for the next read or write. On error
  // returns |false|.
  virtual bool Seek(size_t offset) = 0;

  // Reads |length| bytes of data into |buffer|. On error, returns |false|.
  virtual bool Read(void* buffer, size_t length) = 0;

  // Writes |length| bytes of data into |buffer|. On error, returns |false|.
  virtual bool Write(const void* buffer, size_t length) = 0;

  // Closes the stream and cleans up all associated resources. On error, returns
  // |false|.
  virtual bool Close() = 0;
};

using UniqueStreamPtr = std::unique_ptr<StreamInterface>;
using SharedStreamPtr = std::shared_ptr<StreamInterface>;

// A very simple class for reading and writing data into a file descriptor.
class PUFFIN_EXPORT FileStream : public StreamInterface {
 public:
  explicit FileStream(int fd) : fd_(fd) { Seek(0); }
  ~FileStream() override = default;

  static UniqueStreamPtr Open(const std::string& path, bool read, bool write);

  bool GetSize(size_t* size) const override;
  bool GetOffset(size_t* offset) const override;
  bool Seek(size_t offset) override;
  bool Read(void* buffer, size_t length) override;
  bool Write(const void* buffer, size_t length) override;
  bool Close() override;

 protected:
  FileStream() = default;

 private:
  // The file descriptor.
  int fd_;

  DISALLOW_COPY_AND_ASSIGN(FileStream);
};

// A very simple class for reading and writing into memory.
class PUFFIN_EXPORT MemoryStream : public StreamInterface {
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

#endif  // SRC_INCLUDE_PUFFIN_STREAM_H_
