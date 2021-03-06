// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_INCLUDE_PUFFIN_UTILS_H_
#define SRC_INCLUDE_PUFFIN_UTILS_H_

#include <string>
#include <vector>

#include "puffin/common.h"
#include "puffin/stream.h"

namespace puffin {

// Converts an array of |ByteExtens| or |BitExtents| to a string. Each extent
// has format "offset:length" and are comma separated.
template <typename T>
PUFFIN_EXPORT std::string ExtentsToString(const T& extents) {
  std::string str;
  for (const auto& extent : extents) {
    str += std::to_string(extent.offset) + ":" + std::to_string(extent.length) +
           ",";
  }
  return str;
}

// Locates deflate locations for a zlib buffer |data|. It locates by removing
// header and footer bytes from the zlib stream.
bool LocateDeflatesInZlib(const Buffer& data,
                          std::vector<ByteExtent>* deflate_blocks);

// Similar to the function above, except that it accepts the file path to the
// source and a list of zlib blocks and returns the deflate addresses in bit
// extents.
PUFFIN_EXPORT
bool LocateDeflatesInZlibBlocks(const std::string& file_path,
                                const std::vector<ByteExtent>& zlibs,
                                std::vector<BitExtent>* deflates);

// Searches for deflate locations in a gzip file. The results are
// saved in |deflate_blocks|.
bool LocateDeflatesInGzip(const Buffer& data,
                          std::vector<ByteExtent>* deflate_blocks);

// Search for the deflates in a zip archive, and put the result in
// |deflate_blocks|.
bool LocateDeflatesInZipArchive(const Buffer& data,
                                std::vector<ByteExtent>* deflate_blocks);

PUFFIN_EXPORT
// Create a list of deflate subblock locations from the deflate blocks in a
// zip archive.
bool LocateDeflateSubBlocksInZipArchive(const Buffer& data,
                                        std::vector<BitExtent>* deflates);

// Reads the deflates in from |deflates| and returns a list of its subblock
// locations. Each subblock in practice is a deflate stream by itself.
// Assumption is that the first subblock in each deflate in |deflates| start in
// byte boundary.
bool FindDeflateSubBlocks(const UniqueStreamPtr& src,
                          const std::vector<ByteExtent>& deflates,
                          std::vector<BitExtent>* subblock_deflates);

// Finds the location of puffs in the deflate stream |src| based on the location
// of |deflates| and populates the |puffs|. We assume |deflates| are sorted by
// their offset value. |out_puff_size| will be the size of the puff stream.
bool FindPuffLocations(const UniqueStreamPtr& src,
                       const std::vector<BitExtent>& deflates,
                       std::vector<ByteExtent>* puffs,
                       uint64_t* out_puff_size);

// Removes any BitExtents from both |extents1| and |extents2| if the data it
// points to is found in both |extents1| and |extents2|. The order of the
// remaining BitExtents is preserved.
PUFFIN_EXPORT
void RemoveEqualBitExtents(const Buffer& data1,
                           const Buffer& data2,
                           std::vector<BitExtent>* extents1,
                           std::vector<BitExtent>* extents2);

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_UTILS_H_
