// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef USE_BRILLO
#include "brillo/flag_helper.h"
#else
#include "gflags/gflags.h"
#endif

#include "puffin/src/include/puffin/common.h"
#include "puffin/src/include/puffin/huffer.h"
#include "puffin/src/include/puffin/puffdiff.h"
#include "puffin/src/include/puffin/puffer.h"
#include "puffin/src/include/puffin/puffpatch.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/extent_stream.h"
#include "puffin/src/file_stream.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/set_errors.h"

using std::vector;
using std::string;
using puffin::BitExtent;
using puffin::ByteExtent;
using puffin::ExtentStream;
using puffin::Error;
using puffin::FileStream;
using puffin::Huffer;
using puffin::Puffer;
using puffin::UniqueStreamPtr;

namespace {

constexpr char kExtentDelimeter = ',';
constexpr char kOffsetLengthDelimeter = ':';

template <typename T>
vector<T> StringToExtents(const string& str) {
  vector<T> extents;
  if (!str.empty()) {
    std::stringstream ss(str);
    string extent_str;
    while (getline(ss, extent_str, kExtentDelimeter)) {
      std::stringstream extent_ss(extent_str);
      string offset_str, length_str;
      getline(extent_ss, offset_str, kOffsetLengthDelimeter);
      getline(extent_ss, length_str, kOffsetLengthDelimeter);
      extents.emplace_back(stoull(offset_str), stoull(length_str));
    }
  }
  return extents;
}

const size_t kDefaultPuffCacheSize = 50 * 1024 * 1024;  // 50 MB

}  // namespace

#define SETUP_FLAGS                                                        \
  DEFINE_string(src_file, "", "Source file");                              \
  DEFINE_string(dst_file, "", "Target file");                              \
  DEFINE_string(patch_file, "", "patch file");                             \
  DEFINE_string(                                                           \
      src_deflates_byte, "",                                               \
      "Source deflate byte locations in the format offset:length,...");    \
  DEFINE_string(                                                           \
      dst_deflates_byte, "",                                               \
      "Target deflate byte locations in the format offset:length,...");    \
  DEFINE_string(                                                           \
      src_deflates_bit, "",                                                \
      "Source deflate bit locations in the format offset:length,...");     \
  DEFINE_string(                                                           \
      dst_deflates_bit, "",                                                \
      "Target deflatebit locations in the format offset:length,...");      \
  DEFINE_string(src_puffs, "",                                             \
                "Source puff locations in the format offset:length,...");  \
  DEFINE_string(dst_puffs, "",                                             \
                "Target puff locations in the format offset:length,...");  \
  DEFINE_string(src_extents, "",                                           \
                "Source extents in the format of offset:length,...");      \
  DEFINE_string(dst_extents, "",                                           \
                "Target extents in the format of offset:length,...");      \
  DEFINE_string(operation, "",                                             \
                "Type of the operation: puff, huff, puffdiff, puffpatch"); \
  DEFINE_bool(verbose, false,                                              \
              "Logs all the given parameters including internally "        \
              "generated ones");                                           \
  DEFINE_uint64(cache_size, kDefaultPuffCacheSize,                         \
                "Maximum size to cache the puff stream. Used in puffpatch");

#ifndef USE_BRILLO
SETUP_FLAGS;
#endif

// Main entry point to the application.
int main(int argc, char** argv) {
#ifdef USE_BRILLO
  SETUP_FLAGS;
  brillo::FlagHelper::Init(argc, argv, "Puffin tool");
#else
  // google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
#endif

  TEST_AND_RETURN_VALUE(!FLAGS_operation.empty(), -1);
  TEST_AND_RETURN_VALUE(!FLAGS_src_file.empty(), -1);
  TEST_AND_RETURN_VALUE(!FLAGS_dst_file.empty(), -1);

  auto src_deflates_byte = StringToExtents<ByteExtent>(FLAGS_src_deflates_byte);
  auto dst_deflates_byte = StringToExtents<ByteExtent>(FLAGS_dst_deflates_byte);
  auto src_deflates_bit = StringToExtents<BitExtent>(FLAGS_src_deflates_bit);
  auto dst_deflates_bit = StringToExtents<BitExtent>(FLAGS_dst_deflates_bit);
  auto src_puffs = StringToExtents<ByteExtent>(FLAGS_src_puffs);
  auto dst_puffs = StringToExtents<ByteExtent>(FLAGS_dst_puffs);
  auto src_extents = StringToExtents<ByteExtent>(FLAGS_src_extents);
  auto dst_extents = StringToExtents<ByteExtent>(FLAGS_dst_extents);

  if (FLAGS_verbose) {
    LOG(INFO) << "src_deflates_byte: "
              << puffin::ExtentsToString(src_deflates_byte);
    LOG(INFO) << "dst_deflates_byte: "
              << puffin::ExtentsToString(dst_deflates_byte);
    LOG(INFO) << "src_deflates_bit: "
              << puffin::ExtentsToString(src_deflates_bit);
    LOG(INFO) << "dst_deflates_bit: "
              << puffin::ExtentsToString(dst_deflates_bit);
    LOG(INFO) << "src_puffs: " << puffin::ExtentsToString(src_puffs);
    LOG(INFO) << "dst_puffs: " << puffin::ExtentsToString(dst_puffs);
    LOG(INFO) << "src_extents: " << puffin::ExtentsToString(src_extents);
    LOG(INFO) << "dst_extents: " << puffin::ExtentsToString(dst_extents);
  }

  auto src_stream = FileStream::Open(FLAGS_src_file, true, false);
  TEST_AND_RETURN_VALUE(src_stream, -1);
  if (!src_extents.empty()) {
    src_stream =
        ExtentStream::CreateForRead(std::move(src_stream), src_extents);
    TEST_AND_RETURN_VALUE(src_stream, -1);
  }

  vector<ByteExtent> puffs;
  if (FLAGS_operation == "puff") {
    auto puffer = std::make_shared<Puffer>();
    auto dst_stream = FileStream::Open(FLAGS_dst_file, false, true);
    TEST_AND_RETURN_VALUE(dst_stream, -1);
    if (src_deflates_bit.empty() && src_deflates_byte.empty()) {
      LOG(WARNING) << "You should pass source deflates, is this intentional?";
    }
    if (src_deflates_bit.empty()) {
      TEST_AND_RETURN_VALUE(FindDeflateSubBlocks(src_stream, src_deflates_byte,
                                                 &src_deflates_bit),
                            -1);
    }
    TEST_AND_RETURN_VALUE(dst_puffs.empty(), -1);
    size_t dst_puff_size;
    TEST_AND_RETURN_VALUE(FindPuffLocations(src_stream, src_deflates_bit,
                                            &dst_puffs, &dst_puff_size),
                          -1);
    if (FLAGS_verbose) {
      LOG(INFO) << "out_dst_puffs: " << puffin::ExtentsToString(dst_puffs);
    }
    // Puff using the given puff_size.
    auto reader = puffin::PuffinStream::CreateForPuff(
        std::move(src_stream), puffer, dst_puff_size, src_deflates_bit,
        dst_puffs);
    puffin::Buffer buffer(1024 * 1024);
    size_t bytes_wrote = 0;
    while (bytes_wrote < dst_puff_size) {
      auto write_size = std::min(
          buffer.size(), static_cast<size_t>(dst_puff_size - bytes_wrote));
      TEST_AND_RETURN_VALUE(reader->Read(buffer.data(), write_size), -1);
      TEST_AND_RETURN_VALUE(dst_stream->Write(buffer.data(), write_size), -1);
      bytes_wrote += write_size;
    }

  } else if (FLAGS_operation == "huff") {
    if (dst_deflates_bit.empty() && src_puffs.empty()) {
      LOG(WARNING) << "You should pass source puffs and destination deflates"
                   << ", is this intentional?";
    }
    TEST_AND_RETURN_VALUE(src_puffs.size() == dst_deflates_bit.size(), -1);
    size_t src_stream_size;
    TEST_AND_RETURN_VALUE(src_stream->GetSize(&src_stream_size), -1);
    auto dst_file = FileStream::Open(FLAGS_dst_file, false, true);
    TEST_AND_RETURN_VALUE(dst_file, -1);

    auto huffer = std::make_shared<Huffer>();
    auto dst_stream = puffin::PuffinStream::CreateForHuff(
        std::move(dst_file), huffer, src_stream_size, dst_deflates_bit,
        src_puffs);

    puffin::Buffer buffer(1024 * 1024);
    size_t bytes_read = 0;
    while (bytes_read < src_stream_size) {
      auto read_size = std::min(buffer.size(), src_stream_size - bytes_read);
      TEST_AND_RETURN_VALUE(src_stream->Read(buffer.data(), read_size), -1);
      TEST_AND_RETURN_VALUE(dst_stream->Write(buffer.data(), read_size), -1);
      bytes_read += read_size;
    }
  } else if (FLAGS_operation == "puffdiff") {
    if (src_deflates_bit.empty() && src_deflates_byte.empty()) {
      LOG(WARNING) << "You should pass source deflates, is this intentional?";
    }
    if (dst_deflates_bit.empty() && dst_deflates_byte.empty()) {
      LOG(WARNING) << "You should pass target deflates, is this intentional?";
    }
    auto dst_stream = FileStream::Open(FLAGS_dst_file, true, false);
    TEST_AND_RETURN_VALUE(dst_stream, -1);
    if (!dst_extents.empty()) {
      dst_stream =
          ExtentStream::CreateForWrite(std::move(dst_stream), dst_extents);
      TEST_AND_RETURN_VALUE(dst_stream, -1);
    }

    if (src_deflates_bit.empty()) {
      TEST_AND_RETURN_VALUE(FindDeflateSubBlocks(src_stream, src_deflates_byte,
                                                 &src_deflates_bit),
                            -1);
    }

    if (dst_deflates_bit.empty()) {
      TEST_AND_RETURN_VALUE(FindDeflateSubBlocks(dst_stream, dst_deflates_byte,
                                                 &dst_deflates_bit),
                            -1);
    }

    puffin::Buffer puffdiff_delta;
    TEST_AND_RETURN_VALUE(
        puffin::PuffDiff(std::move(src_stream), std::move(dst_stream),
                         src_deflates_bit, dst_deflates_bit, "/tmp/patch.tmp",
                         &puffdiff_delta),
        -1);
    if (FLAGS_verbose) {
      LOG(INFO) << "patch_size: " << puffdiff_delta.size();
    }
    auto patch_stream = FileStream::Open(FLAGS_patch_file, false, true);
    TEST_AND_RETURN_VALUE(patch_stream, -1);
    TEST_AND_RETURN_VALUE(
        patch_stream->Write(puffdiff_delta.data(), puffdiff_delta.size()), -1);
  } else if (FLAGS_operation == "puffpatch") {
    auto patch_stream = FileStream::Open(FLAGS_patch_file, true, false);
    TEST_AND_RETURN_VALUE(patch_stream, -1);
    size_t patch_size;
    TEST_AND_RETURN_VALUE(patch_stream->GetSize(&patch_size), -1);

    puffin::Buffer puffdiff_delta(patch_size);
    TEST_AND_RETURN_VALUE(
        patch_stream->Read(puffdiff_delta.data(), puffdiff_delta.size()), -1);
    auto dst_stream = FileStream::Open(FLAGS_dst_file, false, true);
    TEST_AND_RETURN_VALUE(dst_stream, -1);
    if (!dst_extents.empty()) {
      dst_stream =
          ExtentStream::CreateForWrite(std::move(dst_stream), dst_extents);
      TEST_AND_RETURN_VALUE(dst_stream, -1);
    }
    // Apply the patch. Use 50MB cache, it should be enough for most of the
    // operations.
    TEST_AND_RETURN_VALUE(
        puffin::PuffPatch(std::move(src_stream), std::move(dst_stream),
                          puffdiff_delta.data(), puffdiff_delta.size(),
                          FLAGS_cache_size),  // max_cache_size
        -1);
  }
  return 0;
}
