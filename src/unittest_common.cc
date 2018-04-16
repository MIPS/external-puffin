// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/unittest_common.h"

using std::string;

namespace puffin {

bool MakeTempFile(string* filename, int* fd) {
#ifdef __ANDROID__
  char tmp_template[] = "/data/local/tmp/puffin-XXXXXX";
#else
  char tmp_template[] = "/tmp/puffin-XXXXXX";
#endif  // __ANDROID__
  int mkstemp_fd = mkstemp(tmp_template);
  TEST_AND_RETURN_FALSE(mkstemp_fd >= 0);
  if (filename) {
    *filename = tmp_template;
  }
  if (fd) {
    *fd = mkstemp_fd;
  } else {
    close(mkstemp_fd);
  }
  return true;
}

// clang-format off
const Buffer kDeflatesSample1 = {
    /* raw   0 */ 0x11, 0x22,
    /* def   2 */ 0x63, 0x64, 0x62, 0x66, 0x61, 0x05, 0x00,
    /* raw   9 */ 0x33,
    /* def  10 */ 0x03, 0x00,
    /* raw  12 */
    /* def  12 */ 0x63, 0x04, 0x00,
    /* raw  15 */ 0x44, 0x55
};
const Buffer kPuffsSample1 = {
    /* raw   0 */ 0x11, 0x22,
    /* puff  2 */ 0x00, 0x00, 0xA0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* raw  13 */ 0x00, 0x33,
    /* puff 15 */ 0x00, 0x00, 0xA0, 0xFF, 0x81,
    /* raw  20 */ 0x00,
    /* puff 21 */ 0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
    /* raw  28 */ 0x00, 0x44, 0x55
};
const std::vector<ByteExtent> kDeflateExtentsSample1 = {
  {2, 7}, {10, 2}, {12, 3}};
const std::vector<BitExtent> kSubblockDeflateExtentsSample1 = {
  {16, 50}, {80, 10}, {96, 18}};
const std::vector<ByteExtent> kPuffExtentsSample1 = {{2, 11}, {15, 5}, {21, 7}};

const Buffer kDeflatesSample2 = {
    /* def  0  */ 0x63, 0x64, 0x62, 0x66, 0x61, 0x05, 0x00,
    /* raw  7  */ 0x33, 0x66,
    /* def  9  */ 0x01, 0x05, 0x00, 0xFA, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05,
    /* def  19 */ 0x63, 0x04, 0x00
};
const Buffer kPuffsSample2 = {
    /* puff  0 */ 0x00, 0x00, 0xA0, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* raw  11 */ 0x00, 0x33, 0x66,
    /* puff 14 */ 0x00, 0x00, 0x80, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0xFF,
                  0x81,
    /* puff 25 */ 0x00, 0x00, 0xA0, 0x00, 0x01, 0xFF, 0x81,
    /* raw  32 */ 0x00,
};
const std::vector<ByteExtent> kDeflateExtentsSample2 = {
  {0, 7}, {9, 10}, {19, 3}};
const std::vector<BitExtent> kSubblockDeflateExtentsSample2 = {
  {0, 50}, {72, 80}, {152, 18}};
const std::vector<ByteExtent> kPuffExtentsSample2 = {
  {0, 11}, {14, 11}, {25, 7}};
// clang-format on

}  // namespace puffin
