// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/puffdiff.h"

#include <endian.h>
#include <inttypes.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "bsdiff/bsdiff.h"

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/puffin.pb.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::string;
using std::vector;

namespace {

constexpr char kMagic[] = "PUF1";
constexpr size_t kMagicLength = 4;

// Structure of a puffin patch
// +-------+------------------+-------------+--------------+
// |P|U|F|1| PatchHeader Size | PatchHeader | bsdiff_patch |
// +-------+------------------+-------------+--------------+
bool CreatePatch(const Buffer& bsdiff_patch,
                 const vector<ByteExtent>& src_deflates,
                 const vector<ByteExtent>& dst_deflates,
                 const vector<ByteExtent>& src_puffs,
                 const vector<ByteExtent>& dst_puffs,
                 size_t src_puff_size,
                 size_t dst_puff_size,
                 Buffer* patch) {
  PatchHeader header;
  header.set_version(1);

  auto copy_vector_to_rf =
      [](const vector<ByteExtent>& from,
         google::protobuf::RepeatedPtrField<ProtoByteExtent>* to) {
        to->Reserve(from.size());
        for (const auto& ext : from) {
          auto tmp = to->Add();
          tmp->set_offset(ext.offset);
          tmp->set_length(ext.length);
        }
      };

  copy_vector_to_rf(src_deflates, header.mutable_src()->mutable_deflates());
  copy_vector_to_rf(dst_deflates, header.mutable_dst()->mutable_deflates());
  copy_vector_to_rf(src_puffs, header.mutable_src()->mutable_puffs());
  copy_vector_to_rf(dst_puffs, header.mutable_dst()->mutable_puffs());

  header.mutable_src()->set_puff_length(src_puff_size);
  header.mutable_dst()->set_puff_length(dst_puff_size);

  const uint32_t header_size = header.ByteSize();

  size_t offset = 0;
  patch->resize(kMagicLength + sizeof(header_size) + header_size +
                bsdiff_patch.size());

  memcpy(patch->data() + offset, kMagic, kMagicLength);
  offset += kMagicLength;

  // Read header size from big-endian mode.
  uint32_t be_header_size = htobe32(header_size);
  memcpy(patch->data() + offset, &be_header_size, sizeof(be_header_size));
  offset += 4;

  TEST_AND_RETURN_FALSE(
      header.SerializeToArray(patch->data() + offset, header_size));
  offset += header_size;

  memcpy(patch->data() + offset, bsdiff_patch.data(), bsdiff_patch.size());

  if (bsdiff_patch.size() > patch->size()) {
    LOG(ERROR) << "Puffin patch is invalid";
  }
  return true;
}

}  // namespace

bool PuffDiff(const UniqueStreamPtr& src,
              const UniqueStreamPtr& dst,
              const vector<ByteExtent>& src_deflates,
              const vector<ByteExtent>& dst_deflates,
              const string& tmp_filepath,
              Buffer* patch) {
  Puffer puffer;
  Error error;
  size_t src_size;
  TEST_AND_RETURN_FALSE(src->GetSize(&src_size));
  SharedBufferPtr src_puff_buffer(new Buffer(src_size));
  auto src_puff = MemoryStream::Create(src_puff_buffer, false, true);
  vector<ByteExtent> src_puffs;
  TEST_AND_RETURN_FALSE(
      puffer.Puff(src, src_puff, src_deflates, &src_puffs, &error));
  size_t src_puff_size;
  TEST_AND_RETURN_FALSE(src_puff->GetSize(&src_puff_size));

  size_t dst_size;
  TEST_AND_RETURN_FALSE(dst->GetSize(&dst_size));
  SharedBufferPtr dst_puff_buffer(new Buffer(dst_size));
  auto dst_puff = MemoryStream::Create(dst_puff_buffer, false, true);
  vector<ByteExtent> dst_puffs;
  TEST_AND_RETURN_FALSE(
      puffer.Puff(dst, dst_puff, dst_deflates, &dst_puffs, &error));
  size_t dst_puff_size;
  TEST_AND_RETURN_FALSE(dst_puff->GetSize(&dst_puff_size));

  TEST_AND_RETURN_FALSE(0 == bsdiff::bsdiff(src_puff_buffer->data(),
                                            src_puff_size,
                                            dst_puff_buffer->data(),
                                            dst_puff_size,
                                            tmp_filepath.c_str(),
                                            nullptr));
  // Closing streams.
  TEST_AND_RETURN_FALSE(src_puff->Close());
  TEST_AND_RETURN_FALSE(dst_puff->Close());

  auto bsdiff_patch = FileStream::Open(tmp_filepath, true, false);
  TEST_AND_RETURN_FALSE(bsdiff_patch);
  size_t patch_size;
  TEST_AND_RETURN_FALSE(bsdiff_patch->GetSize(&patch_size));
  Buffer bsdiff_patch_buf(patch_size);
  TEST_AND_RETURN_FALSE(
      bsdiff_patch->Read(bsdiff_patch_buf.data(), bsdiff_patch_buf.size()));
  TEST_AND_RETURN_FALSE(bsdiff_patch->Close());

  TEST_AND_RETURN_FALSE(CreatePatch(bsdiff_patch_buf,
                                    src_deflates,
                                    dst_deflates,
                                    src_puffs,
                                    dst_puffs,
                                    src_puff_size,
                                    dst_puff_size,
                                    patch));
  return true;
}

}  // namespace puffin
