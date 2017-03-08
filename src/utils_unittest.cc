// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "gtest/gtest.h"

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/unittest_common.h"

namespace puffin {

using std::vector;

class UtilsTest : public ::testing::Test {
 public:
  void FindDeflatesInZlibBlocks(const Buffer& src,
                                const vector<ByteExtent>& zlibs,
                                const vector<ByteExtent>& deflates) {
    SharedBufferPtr src_buf(new Buffer(src));
    auto src_stream = MemoryStream::Create(src_buf, true, false);
    vector<ByteExtent> deflates_out;
    ASSERT_TRUE(LocateDeflatesInZlibBlocks(src_stream, zlibs, &deflates_out));
    ASSERT_EQ(deflates, deflates_out);
  }
};

TEST_F(UtilsTest, LocateDeflatesInZlibsTest) {
  Buffer empty;
  vector<ByteExtent> empty_zlibs;
  vector<ByteExtent> empty_deflates;
  FindDeflatesInZlibBlocks(empty, empty_zlibs, empty_deflates);
}

// TODO(ahassani): Test a proper zlib format.
// TODO(ahassani): Test zlib format with wrong header.

}  // namespace puffin
