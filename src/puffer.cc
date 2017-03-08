// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/puffer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "puffin/src/bit_reader.h"
#include "puffin/src/huffman_table.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/puff_data.h"
#include "puffin/src/puff_writer.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::vector;
using std::string;

Puffer::Puffer() : dyn_ht_(new HuffmanTable()), fix_ht_(new HuffmanTable()) {}

Puffer::~Puffer() {}

// We assume |deflates| are sorted by their offset value.
bool Puffer::Puff(const UniqueStreamPtr& src,
                  const UniqueStreamPtr& dst,
                  const vector<ByteExtent>& deflates,
                  vector<ByteExtent>* puffs,
                  Error* error) const {
  *error = Error::kSuccess;
  size_t max_deflate_length = 0;
  for (const auto& deflate : deflates) {
    max_deflate_length =
        std::max(static_cast<uint64_t>(max_deflate_length), deflate.length);
  }
  // This should barely happen but for precaution we resize to bigger buffer.
  Buffer deflate_buffer(max_deflate_length);
  Buffer puff_buffer(max_deflate_length * 2 + 100);

  auto cur_deflate = deflates.cbegin();
  size_t src_size;
  TEST_AND_RETURN_FALSE_SET_ERROR(src->GetSize(&src_size), Error::kStreamIO);
  while (true) {
    size_t src_offset;
    TEST_AND_RETURN_FALSE_SET_ERROR(src->GetOffset(&src_offset),
                                    Error::kStreamIO);
    if (src_offset >= src_size) {
      break;
    }
    size_t next_offset =
        (cur_deflate == deflates.cend()) ? src_size : cur_deflate->offset;

    // Copy non-deflate data into puff buffer.
    auto len = next_offset - src_offset;
    while (len > 0) {
      auto min_len = std::min(len, puff_buffer.size());
      // Write min bytes from src into dst.
      TEST_AND_RETURN_FALSE_SET_ERROR(src->Read(puff_buffer.data(), min_len),
                                      Error::kStreamIO);
      TEST_AND_RETURN_FALSE_SET_ERROR(dst->Write(puff_buffer.data(), min_len),
                                      Error::kStreamIO);
      len -= min_len;
    }
    TEST_AND_RETURN_FALSE_SET_ERROR(src->GetOffset(&src_offset),
                                    Error::kStreamIO);
    if (src_offset >= src_size) {
      return true;
    }

    // Read from src into deflate_buffer;
    TEST_AND_RETURN_FALSE_SET_ERROR(
        src->Read(deflate_buffer.data(), cur_deflate->length),
        Error::kStreamIO);
    // Deflate the stream and retry if it fails.
    size_t deflate_size = cur_deflate->length;
    auto puff_size = puff_buffer.size();
    // Check if the error was insufficient output, retry.
    while (!PuffDeflate(deflate_buffer.data(),
                        deflate_size,
                        puff_buffer.data(),
                        &puff_size,
                        error)) {
      TEST_AND_RETURN_FALSE(*error == Error::kInsufficientOutput);
      // This will not fall into an infinite loop.
      LOG(WARNING) << "Insufficient puff buffer: " << puff_buffer.size()
                   << ". Retrying with: " << puff_buffer.size() * 2;
      puff_buffer.resize(puff_buffer.size() * 2);
      puff_size = puff_buffer.size();
    }

    // Add the location into puff.
    size_t offset;
    TEST_AND_RETURN_FALSE_SET_ERROR(dst->GetOffset(&offset), Error::kStreamIO);
    puffs->emplace_back(offset, puff_size);

    // Write into destination;
    TEST_AND_RETURN_FALSE_SET_ERROR(dst->Write(puff_buffer.data(), puff_size),
                                    Error::kStreamIO);
    // Move to next deflate;
    cur_deflate++;
  }
  return true;
}

bool Puffer::PuffDeflate(const uint8_t* comp_buf,
                         size_t comp_size,
                         uint8_t* puff_buf,
                         size_t* puff_size,
                         Error* error) const {
  BufferBitReader br(comp_buf, comp_size);
  BufferPuffWriter pw(puff_buf, *puff_size);

  TEST_AND_RETURN_FALSE(PuffDeflate(&br, &pw, error));
  TEST_AND_RETURN_FALSE_SET_ERROR(comp_size == br.Offset(),
                                  Error::kInvalidInput);

  TEST_AND_RETURN_FALSE(pw.Flush(error));
  *puff_size = pw.Size();
  return true;
}

bool Puffer::PuffDeflate(BitReaderInterface* br,
                         PuffWriterInterface* pw,
                         Error* error) const {
  *error = Error::kSuccess;
  PuffData pd;
  HuffmanTable* cur_ht;
  uint8_t final_bit = 0;
  while (final_bit == 0) {
    TEST_AND_RETURN_FALSE_SET_ERROR(br->CacheBits(3),
                                    Error::kInsufficientInput);
    final_bit = br->ReadBits(1);  // BFINAL
    br->DropBits(1);
    auto type = static_cast<BlockType>(br->ReadBits(2));  // BTYPE
    br->DropBits(2);
    DVLOG(2) << "Read block type: " << BlockTypeToString(type);

    // Header structure
    // +-+-+-+-+-+-+-+-+
    // |F| TP|   SKIP  |
    // +-+-+-+-+-+-+-+-+
    // F -> final_bit
    // TP -> type
    // SKIP -> skipped_bits (only in kUncompressed type)
    uint8_t block_header = (final_bit << 7) | (static_cast<uint8_t>(type) << 5);
    switch (type) {
      case BlockType::kUncompressed: {
        auto skipped_bits = br->ReadBoundaryBits();
        TEST_AND_RETURN_FALSE_SET_ERROR(br->CacheBits(32),
                                        Error::kInsufficientInput);
        auto len = br->ReadBits(16);  // LEN
        br->DropBits(16);
        auto nlen = br->ReadBits(16);  // NLEN
        br->DropBits(16);

        if ((len ^ nlen) != 0xFFFF) {
          LOG(ERROR) << "Length of uncompressed data is invalid;"
                     << " LEN(" << len << ") NLEN(" << nlen << ")";
          *error = Error::kInvalidInput;
          return false;
        }

        // Put skipped bits into header.
        block_header |= skipped_bits;

        // Insert block header.
        pd.type = PuffData::Type::kBlockMetadata;
        pd.block_metadata[0] = block_header;
        pd.length = 1;
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));

        // Insert all the raw literals.
        pd.type = PuffData::Type::kLiterals;
        pd.length = len;
        TEST_AND_RETURN_FALSE_SET_ERROR(
            br->GetByteReaderFn(pd.length, &pd.read_fn),
            Error::kInsufficientInput);
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));

        pd.type = PuffData::Type::kEndOfBlock;
        pd.byte = 0;
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));

        // continue the loop. Do not read any literal/length/distance.
        continue;
      }

      case BlockType::kFixed:
        fix_ht_->BuildFixedHuffmanTable();
        cur_ht = fix_ht_.get();
        pd.type = PuffData::Type::kBlockMetadata;
        pd.block_metadata[0] = block_header;
        pd.length = 1;
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));
        break;

      case BlockType::kDynamic:
        pd.type = PuffData::Type::kBlockMetadata;
        pd.block_metadata[0] = block_header;
        pd.length = sizeof(pd.block_metadata) - 1;
        TEST_AND_RETURN_FALSE(dyn_ht_->BuildDynamicHuffmanTable(
            br, &pd.block_metadata[1], &pd.length, error));
        pd.length += 1;  // For the header.
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));
        cur_ht = dyn_ht_.get();
        break;

      default:
        LOG(ERROR) << "Invalid block compression type: "
                   << static_cast<int>(type);
        *error = Error::kInvalidInput;
        return false;
    }

    while (true) {  // Breaks when the end of block is reached.
      auto max_bits = cur_ht->LitLenMaxBits();
      if (!br->CacheBits(max_bits)) {
        // It could be the end of buffer and the bit length of the end_of_block
        // symbol has less than maximum bit length of current Huffman table. So
        // only asking for the size of end of block symbol (256).
        TEST_AND_RETURN_FALSE_SET_ERROR(cur_ht->EndOfBlockBitLength(&max_bits),
                                        Error::kInvalidInput);
      }
      TEST_AND_RETURN_FALSE_SET_ERROR(br->CacheBits(max_bits),
                                      Error::kInsufficientInput);
      auto bits = br->ReadBits(max_bits);
      uint16_t lit_len_alphabet;
      size_t nbits;
      TEST_AND_RETURN_FALSE_SET_ERROR(
          cur_ht->LitLenAlphabet(bits, &lit_len_alphabet, &nbits),
          Error::kInvalidInput);
      br->DropBits(nbits);
      if (lit_len_alphabet < 256) {
        pd.type = PuffData::Type::kLiteral;
        pd.byte = lit_len_alphabet;
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));

      } else if (256 == lit_len_alphabet) {
        pd.type = PuffData::Type::kEndOfBlock;
        if (final_bit == 1) {
          pd.byte = br->ReadBoundaryBits();
        }
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));
        break;  // Breaks the loop.
      } else {
        TEST_AND_RETURN_FALSE_SET_ERROR(lit_len_alphabet <= 285,
                                        Error::kInvalidInput);
        // Reading length.
        auto len_code_start = lit_len_alphabet - 257;
        auto extra_bits_len = kLengthExtraBits[len_code_start];
        uint16_t extra_bits_value = 0;
        if (extra_bits_len) {
          TEST_AND_RETURN_FALSE_SET_ERROR(br->CacheBits(extra_bits_len),
                                          Error::kInsufficientInput);
          extra_bits_value = br->ReadBits(extra_bits_len);
          br->DropBits(extra_bits_len);
        }
        auto length = kLengthBases[len_code_start] + extra_bits_value;

        TEST_AND_RETURN_FALSE_SET_ERROR(
            br->CacheBits(cur_ht->DistanceMaxBits()),
            Error::kInsufficientInput);
        auto bits = br->ReadBits(cur_ht->DistanceMaxBits());
        uint16_t distance_alphabet;
        size_t nbits;
        TEST_AND_RETURN_FALSE_SET_ERROR(
            cur_ht->DistanceAlphabet(bits, &distance_alphabet, &nbits),
            Error::kInvalidInput);
        br->DropBits(nbits);

        // Reading distance.
        extra_bits_len = kDistanceExtraBits[distance_alphabet];
        extra_bits_value = 0;
        if (extra_bits_len) {
          TEST_AND_RETURN_FALSE_SET_ERROR(br->CacheBits(extra_bits_len),
                                          Error::kInsufficientInput);
          extra_bits_value = br->ReadBits(extra_bits_len);
          br->DropBits(extra_bits_len);
        }

        pd.type = PuffData::Type::kLenDist;
        pd.length = length;
        pd.distance = kDistanceBases[distance_alphabet] + extra_bits_value;
        TEST_AND_RETURN_FALSE(pw->Insert(pd, error));
      }
    }
  }
  return true;
}

}  // namespace puffin
