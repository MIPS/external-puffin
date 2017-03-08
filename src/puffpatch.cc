// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/puffpatch.h"

#include <endian.h>
#include <inttypes.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "bsdiff/bspatch.h"
#include "bsdiff/file_interface.h"

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/puffin.pb.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::string;
using std::vector;
using std::shared_ptr;

namespace {

constexpr char kMagic[] = "PUF1";
constexpr size_t kMagicLength = 4;

bool DecodePatch(const uint8_t* patch,
                 size_t patch_length,
                 size_t* bsdiff_patch_offset,
                 size_t* bsdiff_patch_size,
                 vector<ByteExtent>* src_deflates,
                 vector<ByteExtent>* src_puffs,
                 vector<ByteExtent>* dst_deflates,
                 vector<ByteExtent>* dst_puffs,
                 size_t* src_puff_size,
                 size_t* dst_puff_size) {
  size_t offset = 0;
  string patch_magic(reinterpret_cast<const char*>(patch), kMagicLength);
  if (patch_magic != kMagic) {
    LOG(ERROR) << "Magic number for Puffin patch is incorrect: " << patch_magic;
    return false;
  }
  offset += kMagicLength;

  // Read the header size from big-endian mode.
  uint32_t header_size;
  memcpy(&header_size, patch + offset, sizeof(header_size));
  header_size = be32toh(header_size);
  offset += sizeof(header_size);

  PatchHeader header;
  TEST_AND_RETURN_FALSE(header.ParseFromArray(patch + offset, header_size));
  offset += header_size;

  auto copy_rf_to_vector =
      [](const google::protobuf::RepeatedPtrField<ProtoByteExtent>& from,
         vector<ByteExtent>* to) {
        to->reserve(from.size());
        for (const auto& ext : from) {
          to->emplace_back(ext.offset(), ext.length());
        }
      };

  copy_rf_to_vector(header.src().deflates(), src_deflates);
  copy_rf_to_vector(header.dst().deflates(), dst_deflates);
  copy_rf_to_vector(header.src().puffs(), src_puffs);
  copy_rf_to_vector(header.dst().puffs(), dst_puffs);

  *src_puff_size = header.src().puff_length();
  *dst_puff_size = header.dst().puff_length();

  *bsdiff_patch_offset = offset;
  *bsdiff_patch_size = patch_length - offset;
  return true;
}

class BsdiffStream : public bsdiff::FileInterface {
 public:
  explicit BsdiffStream(UniqueStreamPtr stream) : stream_(std::move(stream)) {}
  ~BsdiffStream() override = default;

  bool Read(void* buf, size_t count, size_t* bytes_read) override {
    *bytes_read = 0;
    if (stream_->Read(buf, count)) {
      *bytes_read = count;
      return true;
    }
    return false;
  }

  bool Write(const void* buf, size_t count, size_t* bytes_written) override {
    *bytes_written = 0;
    if (stream_->Write(buf, count)) {
      *bytes_written = count;
      return true;
    }
    return false;
  }

  bool Seek(off_t pos) override { return stream_->Seek(pos); }

  bool Close() override { return stream_->Close(); }

  bool GetSize(uint64_t* size) override {
    size_t my_size;
    TEST_AND_RETURN_FALSE(stream_->GetSize(&my_size));
    *size = my_size;
    return true;
  }

 private:
  UniqueStreamPtr stream_;

  DISALLOW_COPY_AND_ASSIGN(BsdiffStream);
};

}  // namespace

bool PuffPatch(UniqueStreamPtr src,
               UniqueStreamPtr dst,
               const uint8_t* patch,
               size_t patch_length) {
  size_t bsdiff_patch_offset;  // bsdiff offset in |patch|.
  size_t bsdiff_patch_size = 0;
  vector<ByteExtent> src_deflates;
  vector<ByteExtent> src_puffs;
  vector<ByteExtent> dst_deflates;
  vector<ByteExtent> dst_puffs;
  size_t src_puff_size;
  size_t dst_puff_size;
  // Decode the patch and get the bsdiff_patch.
  TEST_AND_RETURN_FALSE(DecodePatch(patch,
                                    patch_length,
                                    &bsdiff_patch_offset,
                                    &bsdiff_patch_size,
                                    &src_deflates,
                                    &src_puffs,
                                    &dst_deflates,
                                    &dst_puffs,
                                    &src_puff_size,
                                    &dst_puff_size));
  shared_ptr<Puffer> puffer(new Puffer());
  shared_ptr<Huffer> huffer(new Huffer());

  // For reading from source.
  auto puffin_reader = PuffinStream::CreateForPuff(
      std::move(src), puffer, src_puff_size, src_deflates, src_puffs);
  SharedBufferPtr buffer(new Buffer(src_puff_size));
  TEST_AND_RETURN_FALSE(puffin_reader->Read(buffer->data(), src_puff_size));
  std::unique_ptr<bsdiff::FileInterface> reader(
      new BsdiffStream(MemoryStream::Create(buffer, true, false)));

  // For writing into destination.
  std::unique_ptr<bsdiff::FileInterface> writer(
      new BsdiffStream(PuffinStream::CreateForHuff(
          std::move(dst), huffer, dst_puff_size, dst_deflates, dst_puffs)));

  // Running bspatch itself.
  TEST_AND_RETURN_FALSE(
      0 ==
      bspatch(reader, writer, &patch[bsdiff_patch_offset], bsdiff_patch_size));
  return true;
}

}  // namespace puffin
