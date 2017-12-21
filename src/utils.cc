// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/utils.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include "puffin/src/bit_reader.h"
#include "puffin/src/file_stream.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/errors.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/puff_writer.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::string;
using std::vector;

size_t BytesInByteExtents(const vector<ByteExtent>& extents) {
  size_t bytes = 0;
  for (const auto& extent : extents) {
    bytes += extent.length;
  }
  return bytes;
}

// This function uses RFC1950 (https://www.ietf.org/rfc/rfc1950.txt) for the
// definition of a zlib stream.
bool LocateDeflatesInZlibBlocks(const UniqueStreamPtr& src,
                                const vector<ByteExtent>& zlibs,
                                vector<BitExtent>* deflates) {
  for (auto& zlib : zlibs) {
    TEST_AND_RETURN_FALSE(src->Seek(zlib.offset));
    uint16_t zlib_header;
    TEST_AND_RETURN_FALSE(src->Read(&zlib_header, 2));
    BufferBitReader bit_reader(reinterpret_cast<uint8_t*>(&zlib_header), 2);

    TEST_AND_RETURN_FALSE(bit_reader.CacheBits(8));
    auto cmf = bit_reader.ReadBits(8);
    auto cm = bit_reader.ReadBits(4);
    if (cm != 8 && cm != 15) {
      LOG(ERROR) << "Invalid compression method! cm: " << cm;
      return false;
    }
    bit_reader.DropBits(4);
    auto cinfo = bit_reader.ReadBits(4);
    if (cinfo > 7) {
      LOG(ERROR) << "cinfo greater than 7 is not allowed in deflate";
      return false;
    }
    bit_reader.DropBits(4);

    TEST_AND_RETURN_FALSE(bit_reader.CacheBits(8));
    auto flg = bit_reader.ReadBits(8);
    if (((cmf << 8) + flg) % 31) {
      LOG(ERROR) << "Invalid zlib header on offset: " << zlib.offset;
      return false;
    }
    bit_reader.ReadBits(5);  // FCHECK
    bit_reader.DropBits(5);

    auto fdict = bit_reader.ReadBits(1);
    bit_reader.DropBits(1);

    bit_reader.ReadBits(2);  // FLEVEL
    bit_reader.DropBits(2);

    auto header_len = 2;
    if (fdict) {
      TEST_AND_RETURN_FALSE(bit_reader.CacheBits(32));
      bit_reader.DropBits(32);
      header_len += 4;
    }

    ByteExtent deflate(zlib.offset + header_len, zlib.length - header_len - 4);
    TEST_AND_RETURN_FALSE(FindDeflateSubBlocks(src, {deflate}, deflates));
  }
  return true;
}

bool FindDeflateSubBlocks(const UniqueStreamPtr& src,
                          const vector<ByteExtent>& deflates,
                          vector<BitExtent>* subblock_deflates) {
  Puffer puffer;
  Buffer deflate_buffer;
  for (const auto& deflate : deflates) {
    TEST_AND_RETURN_FALSE(src->Seek(deflate.offset));
    // Read from src into deflate_buffer.
    deflate_buffer.resize(deflate.length);
    TEST_AND_RETURN_FALSE(src->Read(deflate_buffer.data(), deflate.length));

    // Find all the subblocks.
    BufferBitReader bit_reader(deflate_buffer.data(), deflate.length);
    BufferPuffWriter puff_writer(nullptr, 0);
    Error error;
    vector<BitExtent> subblocks;
    TEST_AND_RETURN_FALSE(
        puffer.PuffDeflate(&bit_reader, &puff_writer, &subblocks, &error));
    TEST_AND_RETURN_FALSE(deflate.length == bit_reader.Offset());
    for (const auto& subblock : subblocks) {
      subblock_deflates->emplace_back(subblock.offset + deflate.offset * 8,
                                      subblock.length);
    }
  }
  return true;
}

bool LocateDeflatesInZlibBlocks(const string& file_path,
                                const vector<ByteExtent>& zlibs,
                                vector<BitExtent>* deflates) {
  auto src = FileStream::Open(file_path, true, false);
  TEST_AND_RETURN_FALSE(src);
  return LocateDeflatesInZlibBlocks(src, zlibs, deflates);
}

bool FindPuffLocations(const UniqueStreamPtr& src,
                       const vector<BitExtent>& deflates,
                       vector<ByteExtent>* puffs,
                       size_t* out_puff_size) {
  Puffer puffer;
  Buffer deflate_buffer;

  // Here accumulate the size difference between each corresponding deflate and
  // puff. At the end we add this cummulative size difference to the size of the
  // deflate stream to get the size of the puff stream. We use signed size
  // because puff size could be smaller than deflate size.
  ssize_t total_size_difference = 0;
  for (auto deflate = deflates.begin(); deflate != deflates.end(); ++deflate) {
    // Read from src into deflate_buffer.
    auto start_byte = deflate->offset / 8;
    auto end_byte = (deflate->offset + deflate->length + 7) / 8;
    deflate_buffer.resize(end_byte - start_byte);
    TEST_AND_RETURN_FALSE(src->Seek(start_byte));
    TEST_AND_RETURN_FALSE(
        src->Read(deflate_buffer.data(), deflate_buffer.size()));
    // Find the size of the puff.
    BufferBitReader bit_reader(deflate_buffer.data(), deflate_buffer.size());
    size_t bits_to_skip = deflate->offset % 8;
    TEST_AND_RETURN_FALSE(bit_reader.CacheBits(bits_to_skip));
    bit_reader.DropBits(bits_to_skip);

    BufferPuffWriter puff_writer(nullptr, 0);
    Error error;
    TEST_AND_RETURN_FALSE(
        puffer.PuffDeflate(&bit_reader, &puff_writer, nullptr, &error));
    TEST_AND_RETURN_FALSE(deflate_buffer.size() == bit_reader.Offset());

    // 1 if a deflate ends at the same byte that the next deflate starts and
    // there is a few bits gap between them. In practice this may never happen,
    // but it is a good idea to support it anyways. If there is a gap, the value
    // of the gap will be saved as an integer byte to the puff stream. The parts
    // of the byte that belogs to the deflates are shifted out.
    int gap = 0;
    if (deflate != deflates.begin()) {
      auto prev_deflate = std::prev(deflate);
      if ((prev_deflate->offset + prev_deflate->length == deflate->offset)
          // If deflates are on byte boundary the gap will not be counted later,
          // so we won't worry about it.
          && (deflate->offset % 8 != 0)) {
        gap = 1;
      }
    }

    start_byte = ((deflate->offset + 7) / 8);
    end_byte = (deflate->offset + deflate->length) / 8;
    ssize_t deflate_length_in_bytes = end_byte - start_byte;

    // If there was no gap bits between the current and previous deflates, there
    // will be no extra gap byte, so the offset will be shifted one byte back.
    auto puff_offset = start_byte - gap + total_size_difference;
    auto puff_size = puff_writer.Size();
    // Add the location into puff.
    puffs->emplace_back(puff_offset, puff_size);
    total_size_difference +=
        static_cast<ssize_t>(puff_size) - deflate_length_in_bytes - gap;
  }

  size_t src_size;
  TEST_AND_RETURN_FALSE(src->GetSize(&src_size));
  auto final_size = static_cast<ssize_t>(src_size) + total_size_difference;
  TEST_AND_RETURN_FALSE(final_size >= 0);
  *out_puff_size = final_size;
  return true;
}

}  // namespace puffin
