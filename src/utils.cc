// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/utils.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include "puffin/src/bit_reader.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/stream.h"
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
    BufferBitReader br(reinterpret_cast<uint8_t*>(&zlib_header), 2);

    TEST_AND_RETURN_FALSE(br.CacheBits(8));
    auto cmf = br.ReadBits(8);
    auto cm = br.ReadBits(4);
    if (cm != 8 && cm != 15) {
      LOG(ERROR) << "Invalid compression method! cm: " << cm;
      return false;
    }
    br.DropBits(4);
    auto cinfo = br.ReadBits(4);
    if (cinfo > 7) {
      LOG(ERROR) << "cinfo greater than 7 is not allowed in deflate";
      return false;
    }
    br.DropBits(4);

    TEST_AND_RETURN_FALSE(br.CacheBits(8));
    auto flg = br.ReadBits(8);
    if (((cmf << 8) + flg) % 31) {
      LOG(ERROR) << "Invalid zlib header on offset: " << zlib.offset;
      return false;
    }
    br.ReadBits(5);  // FCHECK
    br.DropBits(5);

    auto fdict = br.ReadBits(1);
    br.DropBits(1);

    br.ReadBits(2);  // FLEVEL
    br.DropBits(2);

    auto header_len = 2;
    if (fdict) {
      TEST_AND_RETURN_FALSE(br.CacheBits(32));
      br.DropBits(32);
      header_len += 4;
    }

    ByteExtent deflate;
    deflate.offset = zlib.offset + header_len;
    deflate.length = zlib.length - header_len - 4;
    deflates->push_back(deflate);
  }
  return true;
}

}  // namespace puffin
