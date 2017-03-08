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

// Counts the number of bytes in a list of |ByteExtent|s.
PUFFIN_EXPORT
size_t BytesInByteExtents(const std::vector<ByteExtent>& extents);

// Converts an array of |ByteExtens| to a string. Each |ByteExtent| has format
// "offset:length" and |ByteExtent|s are comma separated.
PUFFIN_EXPORT
std::string ByteExtentsToString(const std::vector<ByteExtent>& extents);

// Locates deflate buffer locations for a set of zlib buffers |zlibs| in
// |src|. It performs by removing header and footer bytes from the zlib stream.
PUFFIN_EXPORT
bool LocateDeflatesInZlibBlocks(const UniqueStreamPtr& src,
                                const std::vector<ByteExtent>& zlibs,
                                std::vector<ByteExtent>* deflates);

}  // namespace puffin

#endif  // SRC_INCLUDE_PUFFIN_UTILS_H_
