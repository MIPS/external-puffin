// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "gtest/gtest.h"

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/memory_stream.h"
#include "puffin/src/unittest_common.h"

namespace puffin {

using std::vector;

namespace {
void FindDeflatesInZlibBlocks(const Buffer& src,
                              const vector<ByteExtent>& zlibs,
                              const vector<BitExtent>& deflates) {
  auto src_stream = MemoryStream::CreateForRead(src);
  vector<BitExtent> deflates_out;
  ASSERT_TRUE(LocateDeflatesInZlibBlocks(src_stream, zlibs, &deflates_out));
  ASSERT_EQ(deflates, deflates_out);
}

void CheckFindPuffLocation(const Buffer& compressed,
                           const vector<BitExtent>& deflates,
                           const vector<ByteExtent>& expected_puffs,
                           size_t expected_puff_size) {
  auto src = MemoryStream::CreateForRead(compressed);
  vector<ByteExtent> puffs;
  size_t puff_size;
  ASSERT_TRUE(FindPuffLocations(src, deflates, &puffs, &puff_size));
  EXPECT_EQ(puffs, expected_puffs);
  EXPECT_EQ(puff_size, expected_puff_size);
}
}  // namespace

TEST(UtilsTest, LocateDeflatesInZlibsTest) {
  Buffer empty;
  vector<ByteExtent> empty_zlibs;
  vector<BitExtent> empty_deflates;
  FindDeflatesInZlibBlocks(empty, empty_zlibs, empty_deflates);
}

// Test Simple Puffing of the source.

TEST(UtilsTest, FindPuffLocations1Test) {
  CheckFindPuffLocation(kDeflates8, kSubblockDeflateExtents8, kPuffExtents8,
                        kPuffs8.size());
}

TEST(UtilsTest, FindPuffLocations2Test) {
  CheckFindPuffLocation(kDeflates9, kSubblockDeflateExtents9, kPuffExtents9,
                        kPuffs9.size());
}

// TODO(ahassani): Test a proper zlib format.
// TODO(ahassani): Test zlib format with wrong header.

}  // namespace puffin
