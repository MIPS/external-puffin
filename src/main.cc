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
#include "puffin/src/include/puffin/stream.h"
#include "puffin/src/include/puffin/utils.h"
#include "puffin/src/puffin_stream.h"
#include "puffin/src/set_errors.h"

using std::vector;
using std::string;
using puffin::ByteExtent;
using puffin::UniqueStreamPtr;
using puffin::Error;
using puffin::FileStream;
using puffin::Puffer;
using puffin::Huffer;

namespace {

constexpr char kExtentDelimeter = ',';
constexpr char kOffsetLengthDelimeter = ':';

vector<ByteExtent> StringToByteExtents(const string& str) {
  vector<ByteExtent> extents;
  if (str.empty()) {
    return extents;
  }
  std::stringstream ss(str);
  string extent_str;
  while (getline(ss, extent_str, kExtentDelimeter)) {
    std::stringstream extent_ss(extent_str);
    string offset_str, length_str;
    getline(extent_ss, offset_str, kOffsetLengthDelimeter);
    getline(extent_ss, length_str, kOffsetLengthDelimeter);
    extents.emplace_back(stoull(offset_str), stoull(length_str));
  }
  return extents;
}

}  // namespace

#define SETUP_FLAGS                                                           \
  DEFINE_string(src_file, "", "Source file");                                 \
  DEFINE_string(dst_file, "", "Target file");                                 \
  DEFINE_string(patch_file, "", "patch file");                                \
  DEFINE_string(                                                              \
      src_deflates, "", "Deflate locations in the format offset:length,..."); \
  DEFINE_string(src_deflates_file,                                            \
                "",                                                           \
                "Deflate locations in the format offset:length,...");         \
  DEFINE_string(                                                              \
      dst_deflates, "", "Deflate locations in the format offset:length,..."); \
  DEFINE_string(                                                              \
      src_puffs, "", "Puff locations in the format offset:length,...");       \
  DEFINE_string(                                                              \
      dst_puffs, "", "Puff locations in the format offset:length,...");       \
  DEFINE_string(operation,                                                    \
                "",                                                           \
                "Type of the operation: puff, huff, puffdiff, puffpatch");    \
  DEFINE_uint64(puff_size, 0, "Size of the puff stream");

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
  auto src_deflates = StringToByteExtents(FLAGS_src_deflates);
  if (!src_deflates.empty()) {
    LOG(INFO) << "src_deflates: " << puffin::ByteExtentsToString(src_deflates);
  }
  auto dst_deflates = StringToByteExtents(FLAGS_dst_deflates);
  if (!dst_deflates.empty()) {
    LOG(INFO) << "dst_deflates: " << puffin::ByteExtentsToString(dst_deflates);
  }

  auto src_puffs = StringToByteExtents(FLAGS_src_puffs);
  if (!src_puffs.empty()) {
    LOG(INFO) << "src_puffs: " << puffin::ByteExtentsToString(src_puffs);
  }

  auto dst_puffs = StringToByteExtents(FLAGS_dst_puffs);
  if (!dst_puffs.empty()) {
    LOG(INFO) << "dst_puffs: " << puffin::ByteExtentsToString(dst_puffs);
  }

  auto src_stream = FileStream::Open(FLAGS_src_file, true, false);
  TEST_AND_RETURN_VALUE(src_stream, -1);

  vector<ByteExtent> puffs;
  if (FLAGS_operation == "puff") {
    auto puffer = std::make_shared<Puffer>();
    auto dst_stream = FileStream::Open(FLAGS_dst_file, false, true);
    TEST_AND_RETURN_VALUE(dst_stream, -1);
    if (dst_puffs.empty()) {
      size_t dst_puff_size;
      TEST_AND_RETURN_VALUE(FindPuffLocations(src_stream, src_deflates,
                                              &dst_puffs, &dst_puff_size),
                            -1);
      LOG(INFO) << "dst_puffs: " << puffin::ByteExtentsToString(dst_puffs);
    }
    // Puff using the given puff_size.
    auto reader = puffin::PuffinStream::CreateForPuff(std::move(src_stream),
                                                      puffer, FLAGS_puff_size,
                                                      src_deflates, dst_puffs);
    puffin::Buffer buffer(1024 * 1024);
    size_t bytes_wrote = 0;
    while (bytes_wrote < FLAGS_puff_size) {
      auto write_size = std::min(
          buffer.size(), static_cast<size_t>(FLAGS_puff_size - bytes_wrote));
      TEST_AND_RETURN_VALUE(reader->Read(buffer.data(), write_size), -1);
      TEST_AND_RETURN_VALUE(dst_stream->Write(buffer.data(), write_size), -1);
      bytes_wrote += write_size;
    }

  } else if (FLAGS_operation == "huff") {
    size_t src_stream_size;
    TEST_AND_RETURN_VALUE(src_stream->GetSize(&src_stream_size), -1);
    auto dst_file = FileStream::Open(FLAGS_dst_file, false, true);
    TEST_AND_RETURN_VALUE(dst_file, -1);

    auto huffer = std::make_shared<Huffer>();
    auto dst_stream = puffin::PuffinStream::CreateForHuff(
        std::move(dst_file), huffer, src_stream_size, dst_deflates, src_puffs);

    puffin::Buffer buffer(1024 * 1024);
    size_t bytes_read = 0;
    while (bytes_read < src_stream_size) {
      auto read_size = std::min(buffer.size(), src_stream_size - bytes_read);
      TEST_AND_RETURN_VALUE(src_stream->Read(buffer.data(), read_size), -1);
      TEST_AND_RETURN_VALUE(dst_stream->Write(buffer.data(), read_size), -1);
      bytes_read += read_size;
    }
  } else if (FLAGS_operation == "puffdiff") {
    auto dst_stream = FileStream::Open(FLAGS_dst_file, true, false);
    TEST_AND_RETURN_VALUE(dst_stream, -1);

    puffin::Buffer puffdiff_delta;
    TEST_AND_RETURN_VALUE(
        puffin::PuffDiff(std::move(src_stream), std::move(dst_stream),
                         src_deflates, dst_deflates, "/tmp/patch.tmp",
                         &puffdiff_delta),
        -1);

    LOG(INFO) << "patch size: " << puffdiff_delta.size();
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
    TEST_AND_RETURN_VALUE(puffin::PuffPatch(std::move(src_stream),
                                            std::move(dst_stream),
                                            puffdiff_delta.data(),
                                            puffdiff_delta.size()),
                          -1);
  }
  LOG(INFO) << "Finished! Exiting...";
  return 0;
}
