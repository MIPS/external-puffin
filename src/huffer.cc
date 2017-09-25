// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/include/puffin/huffer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "puffin/src/bit_writer.h"
#include "puffin/src/huffman_table.h"
#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/puff_data.h"
#include "puffin/src/puff_reader.h"
#include "puffin/src/set_errors.h"

namespace puffin {

using std::string;

Huffer::Huffer() : dyn_ht_(new HuffmanTable()), fix_ht_(new HuffmanTable()) {}

Huffer::~Huffer() {}

bool Huffer::HuffDeflate(const uint8_t* puff_buf,
                         size_t puff_size,
                         uint8_t* comp_buf,
                         size_t comp_size,
                         Error* error) const {
  BufferPuffReader pr(puff_buf, puff_size);
  BufferBitWriter bw(comp_buf, comp_size);

  TEST_AND_RETURN_FALSE(HuffDeflate(&pr, &bw, error));
  TEST_AND_RETURN_FALSE_SET_ERROR(bw.Flush(), Error::kInsufficientOutput);
  TEST_AND_RETURN_FALSE_SET_ERROR(pr.BytesLeft() == 0, Error::kInvalidInput);
  TEST_AND_RETURN_FALSE_SET_ERROR(comp_size == bw.Size(), Error::kInvalidInput);
  return true;
}

bool Huffer::HuffDeflate(PuffReaderInterface* pr,
                         BitWriterInterface* bw,
                         Error* error) const {
  *error = Error::kSuccess;
  bool reading_uncompressed_block = false;
  PuffData pd;
  HuffmanTable* cur_ht = nullptr;
  // If no bytes left for PuffReader to read, bail out.
  while (pr->BytesLeft() != 0) {
    TEST_AND_RETURN_FALSE(pr->GetNext(&pd, error));

    // The first data should be a metadata.
    TEST_AND_RETURN_FALSE_SET_ERROR(pd.type == PuffData::Type::kBlockMetadata,
                                    Error::kInvalidInput);
    auto header = pd.block_metadata[0];
    auto final_bit = (header & 0x80) >> 7;
    auto type = static_cast<BlockType>((header & 0x60) >> 5);
    DVLOG(2) << "Write block type: " << BlockTypeToString(type);

    auto skipped_bits = header & 0x1F;
    TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBits(1, final_bit),
                                    Error::kInsufficientInput);
    TEST_AND_RETURN_FALSE_SET_ERROR(
        bw->WriteBits(2, static_cast<uint8_t>(type)),
        Error::kInsufficientInput);
    switch (type) {
      case BlockType::kUncompressed:
        bw->WriteBoundaryBits(skipped_bits);
        reading_uncompressed_block = true;
        break;

      case BlockType::kFixed:
        fix_ht_->BuildFixedHuffmanTable();
        cur_ht = fix_ht_.get();
        reading_uncompressed_block = false;
        break;

      case BlockType::kDynamic:
        cur_ht = dyn_ht_.get();
        TEST_AND_RETURN_FALSE(dyn_ht_->BuildDynamicHuffmanTable(
            &pd.block_metadata[1], pd.length - 1, bw, error));
        reading_uncompressed_block = false;
        break;

      default:
        LOG(ERROR) << "Invalid block compression type: "
                   << static_cast<int>(type);
        *error = Error::kInvalidInput;
        return false;
    }

    // We read literal or distrance/lengths until and end of block or end of
    // stream is reached.
    bool block_ended = false;
    while (!block_ended) {
      TEST_AND_RETURN_FALSE(pr->GetNext(&pd, error));
      switch (pd.type) {
        case PuffData::Type::kLiteral:
        case PuffData::Type::kLiterals:
          if (!reading_uncompressed_block) {
            auto write_literal = [&cur_ht, &bw, &error](uint8_t literal) {
              uint16_t literal_huffman;
              size_t nbits;
              TEST_AND_RETURN_FALSE_SET_ERROR(
                  cur_ht->LitLenHuffman(literal, &literal_huffman, &nbits),
                  Error::kInvalidInput);
              TEST_AND_RETURN_FALSE_SET_ERROR(
                  bw->WriteBits(nbits, literal_huffman),
                  Error::kInsufficientOutput);
              return true;
            };

            if (pd.type == PuffData::Type::kLiteral) {
              TEST_AND_RETURN_FALSE(write_literal(pd.byte));
            } else {
              auto len = pd.length;
              while (len-- > 0) {
                uint8_t literal;
                pd.read_fn(&literal, 1);
                TEST_AND_RETURN_FALSE(write_literal(literal));
              }
            }
          } else {
            // This happens only once for each uncompressed block.
            TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBits(16, pd.length),
                                            Error::kInsufficientOutput);
            TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBits(16, ~pd.length),
                                            Error::kInsufficientOutput);
            TEST_AND_RETURN_FALSE_SET_ERROR(
                bw->WriteBytes(pd.length, pd.read_fn),
                Error::kInsufficientOutput);
          }
          break;

        case PuffData::Type::kLenDist: {
          auto len = pd.length;
          auto dist = pd.distance;
          TEST_AND_RETURN_FALSE_SET_ERROR(len >= 3 && len <= 258,
                                          Error::kInvalidInput);

          // Using a binary search here instead of the linear search may be (but
          // not necessarily) faster. Needs experiment to validate.
          size_t index = 0;
          while (len > kLengthBases[index]) {
            index++;
          }
          if (len < kLengthBases[index]) {
            index--;
          }
          auto extra_bits_len = kLengthExtraBits[index];
          uint16_t length_huffman;
          size_t nbits;
          TEST_AND_RETURN_FALSE_SET_ERROR(
              cur_ht->LitLenHuffman(index + 257, &length_huffman, &nbits),
              Error::kInvalidInput);

          TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBits(nbits, length_huffman),
                                          Error::kInsufficientInput);

          if (extra_bits_len > 0) {
            TEST_AND_RETURN_FALSE_SET_ERROR(
                bw->WriteBits(extra_bits_len, len - kLengthBases[index]),
                Error::kInsufficientInput);
          }

          // Same as above (binary search).
          index = 0;
          while (dist > kDistanceBases[index]) {
            index++;
          }
          if (dist < kDistanceBases[index]) {
            index--;
          }
          extra_bits_len = kDistanceExtraBits[index];
          uint16_t distance_huffman;
          TEST_AND_RETURN_FALSE_SET_ERROR(
              cur_ht->DistanceHuffman(index, &distance_huffman, &nbits),
              Error::kInvalidInput);

          TEST_AND_RETURN_FALSE_SET_ERROR(
              bw->WriteBits(nbits, distance_huffman),
              Error::kInsufficientInput);
          if (extra_bits_len > 0) {
            TEST_AND_RETURN_FALSE_SET_ERROR(
                bw->WriteBits(extra_bits_len, dist - kDistanceBases[index]),
                Error::kInsufficientInput);
          }
          break;
        }

        case PuffData::Type::kEndOfBlock:
          if (!reading_uncompressed_block) {
            uint16_t eos_huffman;
            size_t nbits;
            TEST_AND_RETURN_FALSE_SET_ERROR(
                cur_ht->LitLenHuffman(256, &eos_huffman, &nbits),
                Error::kInvalidInput);
            TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBits(nbits, eos_huffman),
                                            Error::kInsufficientInput);
          }
          if (final_bit == 1) {
            if (!reading_uncompressed_block) {
              TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBoundaryBits(pd.byte),
                                              Error::kInsufficientInput);
            }
          }

          block_ended = true;
          break;

        case PuffData::Type::kBlockMetadata:
          LOG(ERROR) << "Not expecing a metadata!";
          *error = Error::kInvalidInput;
          return false;

        default:
          LOG(ERROR) << "Invalid block data type!";
          *error = Error::kInvalidInput;
          return false;
      }
    }

    // Now block ended so we have to see if we don't have anything else to read,
    // then write the boundary bits if it is not an uncompressed blocks.
    if (pr->BytesLeft() == 0) {
      if (!reading_uncompressed_block) {
        // |pd| is still valid here so we can read it.
        TEST_AND_RETURN_FALSE_SET_ERROR(bw->WriteBoundaryBits(pd.byte),
                                        Error::kInsufficientInput);
      }
    }
  }

  return true;
}

}  // namespace puffin
