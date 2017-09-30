// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/utils.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include "puffin/src/bit_reader.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/errors.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/puff_writer.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::string;
using std::vector;

string ByteExtentsToString(const vector<ByteExtent>& extents) {
  string str;
  for (const auto& extent : extents)
    str += std::to_string(extent.offset) + ":" + std::to_string(extent.length) +
           ",";
  return str;
}

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
                                vector<ByteExtent>* deflates) {
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

    ByteExtent deflate;
    deflate.offset = zlib.offset + header_len;
    deflate.length = zlib.length - header_len - 4;
    deflates->push_back(deflate);
  }
  return true;
}

bool FindPuffLocations(const UniqueStreamPtr& src,
                       const vector<ByteExtent>& deflates,
                       vector<ByteExtent>* puffs,
                       size_t* out_puff_size) {
  Puffer puffer;
  Buffer deflate_buffer;

  // Here accumulate the size difference between each corresponding deflate and
  // puff. At the end we add this cummulative size difference to the size of the
  // deflate stream to get the size of the puff stream. We use signed size
  // because puff size could be smaller than deflate size.
  ssize_t total_size_difference = 0;
  for (const auto& deflate : deflates) {
    TEST_AND_RETURN_FALSE(src->Seek(deflate.offset));
    // Read from src into deflate_buffer.
    deflate_buffer.resize(deflate.length);
    TEST_AND_RETURN_FALSE(src->Read(deflate_buffer.data(), deflate.length));

    // Find the size of the puff.
    BufferBitReader bit_reader(deflate_buffer.data(), deflate.length);
    BufferPuffWriter puff_writer(nullptr, 0);
    Error error;
    TEST_AND_RETURN_FALSE(
        puffer.PuffDeflate(&bit_reader, &puff_writer, &error));
    TEST_AND_RETURN_FALSE(deflate.length == bit_reader.Offset());

    // Add the location into puff.
    auto puff_size = puff_writer.Size();
    puffs->emplace_back(deflate.offset + total_size_difference, puff_size);
    total_size_difference +=
        static_cast<ssize_t>(puff_size) - static_cast<ssize_t>(deflate.length);
  }

  size_t src_size;
  TEST_AND_RETURN_FALSE(src->GetSize(&src_size));
  auto final_size = static_cast<ssize_t>(src_size) + total_size_difference;
  TEST_AND_RETURN_FALSE(final_size >= 0);
  *out_puff_size = final_size;
  return true;
}

}  // namespace puffin
