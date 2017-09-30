// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "puffin/src/bit_reader.h"
#include "puffin/src/bit_writer.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffdiff.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/puffpatch.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/puff_reader.h"
#include "puffin/src/puff_writer.h"
#include "puffin/src/sample_generator.h"
#include "puffin/src/set_errors.h"
#include "puffin/src/unittest_common.h"

#define PRINT_SAMPLE 0  // Set to 1 if you want to print the generated samples.

namespace puffin {

using std::vector;
using std::string;

class PuffinTest : public ::testing::Test {
 public:
  // Utility for decompressing a puff stream.
  bool DecompressPuff(const uint8_t* puff_buf,
                      size_t* puff_size,
                      uint8_t* out_buf,
                      size_t* out_size) {
    BufferPuffReader puff_reader(static_cast<const uint8_t*>(puff_buf),
                                 *puff_size);
    auto start = static_cast<uint8_t*>(out_buf);

    PuffData pd;
    uint8_t final_bit = 0;
    Error error;
    while (puff_reader.BytesLeft() != 0) {
      TEST_AND_RETURN_FALSE(puff_reader.GetNext(&pd, &error));
      switch (pd.type) {
        case PuffData::Type::kLiteral:
          *start = pd.byte;
          start++;

        case PuffData::Type::kLiterals:
          pd.read_fn(start, pd.length);
          start += pd.length;
          break;

        case PuffData::Type::kLenDist: {
          while (pd.length-- > 0) {
            *start = *(start - pd.distance);
            start++;
          }
          break;
        }

        case PuffData::Type::kBlockMetadata:
          final_bit = pd.block_metadata[0] >> 7;
          break;

        case PuffData::Type::kEndOfBlock:
          break;

        default:
          LOG(ERROR) << "Invalid block data type";
          break;
      }
    }
    *out_size = start - static_cast<uint8_t*>(out_buf);
    *puff_size = *puff_size - puff_reader.BytesLeft();
    return true;
  }

  bool PuffDeflate(const uint8_t* comp_buf,
                   size_t comp_size,
                   uint8_t* puff_buf,
                   size_t puff_size,
                   Error* error) const {
    BufferBitReader bit_reader(comp_buf, comp_size);
    BufferPuffWriter puff_writer(puff_buf, puff_size);

    TEST_AND_RETURN_FALSE(
        puffer_.PuffDeflate(&bit_reader, &puff_writer, error));
    TEST_AND_RETURN_FALSE_SET_ERROR(comp_size == bit_reader.Offset(),
                                    Error::kInvalidInput);
    TEST_AND_RETURN_FALSE_SET_ERROR(puff_size = puff_writer.Size(),
                                    Error::kInvalidInput);
    return true;
  }

  bool HuffDeflate(const uint8_t* puff_buf,
                   size_t puff_size,
                   uint8_t* comp_buf,
                   size_t comp_size,
                   Error* error) const {
    BufferPuffReader puff_reader(puff_buf, puff_size);
    BufferBitWriter bit_writer(comp_buf, comp_size);

    TEST_AND_RETURN_FALSE(
        huffer_.HuffDeflate(&puff_reader, &bit_writer, error));
    TEST_AND_RETURN_FALSE_SET_ERROR(comp_size == bit_writer.Size(),
                                    Error::kInvalidInput);
    TEST_AND_RETURN_FALSE_SET_ERROR(puff_reader.BytesLeft() == 0,
                                    Error::kInvalidInput);
    return true;
  }

  // Puffs |compressed| into |out_puff| and checks its equality with
  // |expected_puff|.
  void TestPuffDeflate(const Buffer& compressed,
                       const Buffer& expected_puff,
                       Buffer* out_puff) {
    out_puff->resize(expected_puff.size());
    auto comp_size = compressed.size();
    auto puff_size = out_puff->size();
    Error error;
    ASSERT_TRUE(PuffDeflate(compressed.data(), comp_size, out_puff->data(),
                            puff_size, &error));
    ASSERT_EQ(puff_size, expected_puff.size());
    out_puff->resize(puff_size);
    ASSERT_EQ(expected_puff, *out_puff);
  }

  // Should fail when trying to puff |compressed|.
  void FailPuffDeflate(const Buffer& compressed,
                       Error expected_error,
                       Buffer* out_puff) {
    out_puff->resize(compressed.size() * 2 + 10);
    auto comp_size = compressed.size();
    auto puff_size = out_puff->size();
    Error error;
    ASSERT_FALSE(PuffDeflate(compressed.data(), comp_size, out_puff->data(),
                             puff_size, &error));
    ASSERT_EQ(error, expected_error);
  }

  // Huffs |puffed| into |out_huff| and checks its equality with
  // |expected_huff|.|
  void TestHuffDeflate(const Buffer& puffed,
                       const Buffer& expected_huff,
                       Buffer* out_huff) {
    out_huff->resize(expected_huff.size());
    auto huff_size = out_huff->size();
    auto puffed_size = puffed.size();
    Error error;
    ASSERT_TRUE(HuffDeflate(puffed.data(), puffed_size, out_huff->data(),
                            huff_size, &error));
    ASSERT_EQ(expected_huff, *out_huff);
  }

  // Should fail while huffing |puffed|
  void FailHuffDeflate(const Buffer& puffed,
                       Error expected_error,
                       Buffer* out_compress) {
    out_compress->resize(puffed.size());
    auto comp_size = out_compress->size();
    auto puff_size = puffed.size();
    Error error;
    ASSERT_TRUE(HuffDeflate(puffed.data(), puff_size, out_compress->data(),
                            comp_size, &error));
    ASSERT_EQ(error, expected_error);
  }

  // Decompresses from |puffed| into |uncompress| and checks its equality with
  // |original|.
  void Decompress(const Buffer& puffed,
                  const Buffer& original,
                  Buffer* uncompress) {
    uncompress->resize(original.size());
    auto uncomp_size = uncompress->size();
    auto puffed_size = puffed.size();
    ASSERT_TRUE(DecompressPuff(
        puffed.data(), &puffed_size, uncompress->data(), &uncomp_size));
    ASSERT_EQ(puffed_size, puffed.size());
    ASSERT_EQ(uncomp_size, original.size());
    uncompress->resize(uncomp_size);
    ASSERT_EQ(original, *uncompress);
  }

  void CheckSample(const Buffer original,
                   const Buffer compressed,
                   const Buffer puffed) {
    Buffer puff, uncompress, huff;
    TestPuffDeflate(compressed, puffed, &puff);
    TestHuffDeflate(puffed, compressed, &huff);
    Decompress(puffed, original, &uncompress);
  }

  void TestPatching(const Buffer& src_buf,
                    const Buffer& dst_buf,
                    const vector<ByteExtent>& src_deflates,
                    const vector<ByteExtent>& dst_deflates,
                    const Buffer patch) {
    SharedBufferPtr src_buf_ptr(new Buffer(src_buf));
    SharedBufferPtr dst_buf_ptr(new Buffer(dst_buf));
    auto src_stream = MemoryStream::Create(src_buf_ptr, true, false);
    auto dst_stream = MemoryStream::Create(dst_buf_ptr, true, false);

    Buffer patch_out;
    string patch_path = "/tmp/patch.tmp";
    ScopedPathUnlinker scoped_unlinker(patch_path);
    ASSERT_TRUE(PuffDiff(std::move(src_stream), std::move(dst_stream),
                         src_deflates, dst_deflates, patch_path, &patch_out));

#if PRINT_SAMPLE
    sample_generator::PrintArray("kPatchXXXXX", patch_out);
#endif

    ASSERT_EQ(patch_out, patch);

    src_stream = MemoryStream::Create(src_buf_ptr, true, false);
    SharedBufferPtr dst_buf_ptr2(new Buffer());
    auto dst_stream2 = MemoryStream::Create(dst_buf_ptr2, false, true);
    ASSERT_TRUE(PuffPatch(std::move(src_stream),
                          std::move(dst_stream2),
                          patch.data(),
                          patch.size()));
    ASSERT_EQ(*dst_buf_ptr2, dst_buf);
  }

 protected:
  Puffer puffer_;
  Huffer huffer_;
};

// Tests a simple buffer with uncompressed deflate block.
TEST_F(PuffinTest, UncompressedTest) {
  CheckSample(kRaw1, kDeflate1, kPuff1);
}

// Tests a simple buffer with uncompressed deflate block with length zero.
TEST_F(PuffinTest, ZeroLengthUncompressedTest) {
  CheckSample(kRaw1_1, kDeflate1_1, kPuff1_1);
}

// Tests a dynamically compressed buffer with only one literal.
TEST_F(PuffinTest, CompressedOneTest) {
  CheckSample(kRaw2, kDeflate2, kPuff2);
}

// Tests deflate of an empty buffer.
TEST_F(PuffinTest, EmptyTest) {
  CheckSample(kRaw3, kDeflate3, kPuff3);
}

// Tests a simple buffer with compress deflate block using fixed Huffman table.
TEST_F(PuffinTest, FixedCompressedTest) {
  CheckSample(kRaw4, kDeflate4, kPuff4);
}

// Tests a compressed deflate block using dynamic Huffman table.
TEST_F(PuffinTest, DynamicHuffmanTest) {
  CheckSample(kRaw10, kDeflate10, kPuff10);
}

// Tests an uncompressed deflate block with invalid LEN/NLEN.
TEST_F(PuffinTest, PuffDeflateFailedTest) {
  Buffer puffed;
  FailPuffDeflate(kDeflate5, Error::kInvalidInput, &puffed);
}

// Tests puffing a block with invalid block header.
TEST_F(PuffinTest, PuffDeflateHeaderFailedTest) {
  Buffer puffed;
  FailPuffDeflate(kDeflate6, Error::kInvalidInput, &puffed);
}

// Tests puffing a block with final block bit unset so it returns
// Error::kInsufficientInput.
TEST_F(PuffinTest, PuffDeflateNoFinalBlockBitTest) {
  CheckSample(kRaw7, kDeflate7, kPuff7);
}

TEST_F(PuffinTest, MultipleDeflateBufferNoFinabBitsTest) {
  CheckSample(kRaw7_2, kDeflate7_2, kPuff7_2);
}

TEST_F(PuffinTest, MultipleDeflateBufferOneFinalBitTest) {
  CheckSample(kRaw7_3, kDeflate7_3, kPuff7_3);
}

TEST_F(PuffinTest, MultipleDeflateBufferBothFinalBitTest) {
  CheckSample(kRaw7_4, kDeflate7_4, kPuff7_4);
}

// TODO(ahassani): Add unittests for Failhuff too.

// TODO(ahassani): Add unittest for a deflate stream with multiple blocks.

// TODO(ahassani): Add unittest for testing end of stream and end of stream
// bits.

TEST_F(PuffinTest, Patching8To9Test) {
  TestPatching(
      kDeflates8, kDeflates9, kDeflateExtents8, kDeflateExtents9, kPatch8To9);
}

TEST_F(PuffinTest, Patching9To8Test) {
  TestPatching(
      kDeflates9, kDeflates8, kDeflateExtents9, kDeflateExtents8, kPatch9To8);
}

TEST_F(PuffinTest, Patching8ToEmptyTest) {
  TestPatching(kDeflates8, {}, kDeflateExtents8, {}, kPatch8ToEmpty);
}

TEST_F(PuffinTest, Patching8ToNoDeflateTest) {
  TestPatching(
      kDeflates8, {11, 22, 33, 44}, kDeflateExtents8, {}, kPatch8ToNoDeflate);
}

// TODO(ahassani): add tests for:
//   TestPatchingEmptyTo9
//   TestPatchingNoDeflateTo9

// TODO(ahassani): Change tests data if you decided to compress the header of
// the patch.

}  // namespace puffin
