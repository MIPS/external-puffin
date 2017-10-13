// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/puffin_stream.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::vector;
using std::unique_ptr;
using std::shared_ptr;

UniqueStreamPtr PuffinStream::CreateForPuff(
    UniqueStreamPtr stream,
    std::shared_ptr<Puffer> puffer,
    size_t puff_size,
    const std::vector<ByteExtent>& deflates,
    const std::vector<ByteExtent>& puffs) {
  TEST_AND_RETURN_VALUE(puffs.size() == deflates.size(), nullptr);

  UniqueStreamPtr puffin_stream(new PuffinStream(
      std::move(stream), puffer, nullptr, puff_size, deflates, puffs));
  TEST_AND_RETURN_VALUE(puffin_stream->Seek(0), nullptr);
  return puffin_stream;
}

UniqueStreamPtr PuffinStream::CreateForHuff(
    UniqueStreamPtr stream,
    std::shared_ptr<Huffer> huffer,
    size_t puff_size,
    const std::vector<ByteExtent>& deflates,
    const std::vector<ByteExtent>& puffs) {
  TEST_AND_RETURN_VALUE(puffs.size() == deflates.size(), nullptr);

  UniqueStreamPtr puffin_stream(new PuffinStream(
      std::move(stream), nullptr, huffer, puff_size, deflates, puffs));
  TEST_AND_RETURN_VALUE(puffin_stream->Seek(0), nullptr);
  return puffin_stream;
}

PuffinStream::PuffinStream(UniqueStreamPtr stream,
                           shared_ptr<Puffer> puffer,
                           shared_ptr<Huffer> huffer,
                           size_t puff_size,
                           const vector<ByteExtent>& deflates,
                           const vector<ByteExtent>& puffs)
    : stream_(std::move(stream)),
      puffer_(puffer),
      huffer_(huffer),
      puff_stream_size_(puff_size),
      deflates_(deflates),
      puffs_(puffs),
      puff_pos_(0),
      deflate_pos_(0),
      is_for_puff_(puffer_ ? true : false),
      closed_(false) {
  // Look for the largest puff extent and get a cache with at least that size.
  size_t max_puff_length = 0;
  for (const auto& puff : puffs) {
    max_puff_length =
        std::max(max_puff_length, static_cast<size_t>(puff.length));
  }
  puff_buffer_.reset(new Buffer(max_puff_length));

  // Look for the largest deflate extent and get a cache with at least that
  // size.
  size_t max_deflate_length = 0;
  for (const auto& deflate : deflates) {
    max_deflate_length =
        std::max(max_deflate_length, static_cast<size_t>(deflate.length));
  }
  deflate_buffer_.reset(new Buffer(max_deflate_length));
}

bool PuffinStream::GetSize(size_t* size) const {
  *size = puff_stream_size_;
  return true;
}

bool PuffinStream::GetOffset(size_t* offset) const {
  *offset = puff_pos_;
  return true;
}

bool PuffinStream::Seek(size_t offset) {
  TEST_AND_RETURN_FALSE(!closed_);
  if (!is_for_puff_) {
    // For huffing we should not seek, only seek to zero is accepted or seek to
    // the same offset.
    TEST_AND_RETURN_FALSE(offset == 0 || puff_pos_ == offset);
  }

  TEST_AND_RETURN_FALSE(offset <= puff_stream_size_);
  auto next_puff = puffs_.size();
  if (!puffs_.empty()) {
    // We are searching backwards for the first available puff which either
    // includes the |offset| or it is the next available puff after |offset|.
    for (ssize_t idx = puffs_.size() - 1; idx >= 0; idx--) {
      if (offset < puffs_[idx].offset + puffs_[idx].length) {
        next_puff = idx;
      } else {
        break;
      }
    }
  }
  cur_puff_ = std::next(puffs_.begin(), next_puff);
  cur_deflate_ = std::next(deflates_.begin(), next_puff);

  puff_pos_ = offset;
  if (offset == 0) {
    TEST_AND_RETURN_FALSE(stream_->Seek(deflate_pos_));
  }
  return true;
}

bool PuffinStream::Close() {
  closed_ = true;
  return stream_->Close();
}

bool PuffinStream::Read(void* buffer, size_t length) {
  TEST_AND_RETURN_FALSE(!closed_);
  TEST_AND_RETURN_FALSE(is_for_puff_);
  TEST_AND_RETURN_FALSE(puff_pos_ + length <= puff_stream_size_);
  auto c_bytes = static_cast<uint8_t*>(buffer);
  size_t start_byte_in_first_puff = 0;

  // Figure out where in the puff stream the |puff_pos_| and |length| lies and
  // find the corresponding deflate location.
  if (cur_puff_ == puffs_.end()) {
    // Take care when |puffs_| is empty.
    if (puffs_.empty()) {
      deflate_pos_ = puff_pos_;
    } else {
      deflate_pos_ = puff_pos_ - (puffs_.back().offset + puffs_.back().length) +
                     (deflates_.back().offset + deflates_.back().length);
    }
  } else if (puff_pos_ < cur_puff_->offset) {
    // Between two puffs_.
    deflate_pos_ = cur_deflate_->offset - (cur_puff_->offset - puff_pos_);
  } else {
    // Inside a puff.
    deflate_pos_ = cur_deflate_->offset;
    start_byte_in_first_puff = puff_pos_ - cur_puff_->offset;
  }
  TEST_AND_RETURN_FALSE(stream_->Seek(deflate_pos_));

  size_t bytes_read = 0;
  while (bytes_read < length) {
    size_t cur_puff_offset =
        (cur_puff_ != puffs_.end()) ? cur_puff_->offset : puff_stream_size_;

    if ((puff_pos_ + bytes_read) < cur_puff_offset) {
      // Reading from between two deflate buffers.
      auto bytes_to_read = std::min(length - bytes_read,
                                    cur_puff_offset - (puff_pos_ + bytes_read));
      TEST_AND_RETURN_FALSE(stream_->Read(c_bytes + bytes_read, bytes_to_read));
      bytes_read += bytes_to_read;
    } else {
      // Puff directly to buffer if it has space.
      bool puff_directly_into_buffer =
          (start_byte_in_first_puff == 0) &&
          (length - bytes_read >= cur_puff_->length);

      TEST_AND_RETURN_FALSE(
          stream_->Read(deflate_buffer_->data(), cur_deflate_->length));
      // We already know the puff size, so if it fails, we bail out.
      Error error;
      size_t puff_size = cur_puff_->length;
      TEST_AND_RETURN_FALSE(puffer_->PuffDeflate(
          deflate_buffer_->data(),
          cur_deflate_->length,
          (puff_directly_into_buffer ? c_bytes + bytes_read
                                     : puff_buffer_->data()),
          &puff_size,
          &error));
      TEST_AND_RETURN_FALSE(puff_size == cur_puff_->length);

      auto bytes_to_copy = std::min(
          length - bytes_read,
          static_cast<size_t>(cur_puff_->length) - start_byte_in_first_puff);
      if (!puff_directly_into_buffer) {
        memcpy(c_bytes + bytes_read,
               puff_buffer_->data() + start_byte_in_first_puff,
               bytes_to_copy);
      }

      start_byte_in_first_puff = 0;
      bytes_read += bytes_to_copy;
      // Move to next puff.
      if (puff_pos_ + bytes_read >= cur_puff_->offset + cur_puff_->length) {
        cur_puff_++;
        cur_deflate_++;
      }
    }
  }
  TEST_AND_RETURN_FALSE(bytes_read == length);
  puff_pos_ += length;
  return true;
}

// This function, writes non-puff data directly to |stream_| and caches the puff
// data into |puff_buffer_|. When |puff_buffer_| is full, it huffs it into
// |deflate_buffer_| and writes it to |stream_|.
bool PuffinStream::Write(const void* buffer, size_t length) {
  TEST_AND_RETURN_FALSE(!closed_);
  TEST_AND_RETURN_FALSE(!is_for_puff_);
  TEST_AND_RETURN_FALSE(puff_pos_ + length <= puff_stream_size_);
  auto c_bytes = static_cast<const uint8_t*>(buffer);
  bool passed_all_puffs =
      puffs_.empty() ||
      (puff_pos_ >= puffs_.back().offset + puffs_.back().length);

  // Here we are assuming that data is coming in stream with no retract. Find
  // out if we are in a puffed deflate location and huff it if necessary.
  size_t cur_puff_bytes_wrote = 0;
  if (!passed_all_puffs && puff_pos_ >= cur_puff_->offset) {
    cur_puff_bytes_wrote = puff_pos_ - cur_puff_->offset;
  }

  size_t bytes_wrote = 0;
  size_t copy_len = 0;
  while (bytes_wrote < length) {
    if (passed_all_puffs) {
      // After all puffs are processed.
      copy_len = length - bytes_wrote;
      TEST_AND_RETURN_FALSE(stream_->Write(c_bytes + bytes_wrote, copy_len));
    } else if (puff_pos_ < cur_puff_->offset) {
      // Between two puffs or before the first puff.
      copy_len = std::min(static_cast<size_t>(cur_puff_->offset - puff_pos_),
                          length - bytes_wrote);
      TEST_AND_RETURN_FALSE(stream_->Write(c_bytes + bytes_wrote, copy_len));
    } else if (puff_pos_ >= cur_puff_->offset &&
               puff_pos_ < (cur_puff_->offset + cur_puff_->length)) {
      // Copy |c_bytes| into |puff_buffer_|.
      copy_len = std::min(
          length - bytes_wrote,
          static_cast<size_t>(cur_puff_->length - cur_puff_bytes_wrote));
      memcpy(puff_buffer_->data() + cur_puff_bytes_wrote,
             c_bytes + bytes_wrote,
             copy_len);
      cur_puff_bytes_wrote += copy_len;
      if (cur_puff_bytes_wrote == cur_puff_->length) {
        // |puff_buffer_| is full, now huff into the |deflate_buffer_|.
        Error error;
        TEST_AND_RETURN_FALSE(huffer_->HuffDeflate(puff_buffer_->data(),
                                                   cur_puff_->length,
                                                   deflate_buffer_->data(),
                                                   cur_deflate_->length,
                                                   &error));
        // Write |deflate_buffer_| into output.
        TEST_AND_RETURN_FALSE(
            stream_->Write(deflate_buffer_->data(), cur_deflate_->length));
        // Move to next deflate/puff and if it was the last puff then mark
        // passed_all_puffs as true.
        cur_puff_++;
        cur_deflate_++;
        cur_puff_bytes_wrote = 0;
        if (cur_puff_ == puffs_.end()) {
          passed_all_puffs = true;
        }
      }
    } else {
      TEST_AND_RETURN_FALSE(puff_pos_ <= cur_puff_->offset);
    }
    bytes_wrote += copy_len;
    puff_pos_ += copy_len;
  }
  TEST_AND_RETURN_FALSE(bytes_wrote == length);
  return true;
}

}  // namespace puffin
