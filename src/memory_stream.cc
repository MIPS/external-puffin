// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/memory_stream.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/set_errors.h"

namespace puffin {

MemoryStream::MemoryStream(SharedBufferPtr memory, bool read, bool write)
    : memory_(memory), read_(read), write_(write) {
  if (write_ && !read) {
    memory_->clear();
  }
  closed_ = false;
}

UniqueStreamPtr MemoryStream::Create(SharedBufferPtr memory,
                                     bool read,
                                     bool write) {
  TEST_AND_RETURN_VALUE(read || write, nullptr);
  auto stream = UniqueStreamPtr(new MemoryStream(memory, read, write));
  TEST_AND_RETURN_VALUE(stream->Seek(0), nullptr);
  return stream;
}

bool MemoryStream::GetSize(size_t* size) const {
  *size = memory_->size();
  return true;
}

bool MemoryStream::GetOffset(size_t* offset) const {
  *offset = pos_;
  return true;
}

bool MemoryStream::Seek(size_t offset) {
  TEST_AND_RETURN_FALSE(!closed_);
  TEST_AND_RETURN_FALSE(offset <= memory_->size());
  pos_ = offset;
  return true;
}

bool MemoryStream::Read(void* buffer, size_t length) {
  TEST_AND_RETURN_FALSE(!closed_);
  TEST_AND_RETURN_FALSE(read_);
  TEST_AND_RETURN_FALSE(pos_ + length <= memory_->size());
  memcpy(buffer, memory_->data() + pos_, length);
  pos_ += length;
  return true;
}

bool MemoryStream::Write(const void* buffer, size_t length) {
  // TODO(ahassani): Add a maximum size limit to prevent malicious attacks.
  TEST_AND_RETURN_FALSE(!closed_);
  TEST_AND_RETURN_FALSE(write_);
  if (pos_ + length > memory_->size()) {
    memory_->resize(pos_ + length);
  }
  memcpy(memory_->data() + pos_, buffer, length);
  pos_ += length;
  return true;
}

bool MemoryStream::Close() {
  closed_ = true;
  return true;
}

}  // namespace puffin
