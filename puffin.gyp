# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'variables': {
      'deps': [
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
      ],
    },
    'cflags': [
      '-Wextra',
    ],
    'cflags_cc': [
      '-Wnon-virtual-dtor',
    ],
    'include_dirs': [
      'src/include',
    ],
    'defines': [
      'USE_BRILLO=1',
    ],
  },
  'targets': [
    # puffin-proto library
    {
      'target_name': 'puffin-proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': 'src',
        'proto_out_dir': 'include/puffin/src',
        'exported_deps': [
          'protobuf-lite',
        ],
        'deps': ['<@(exported_deps)'],
      },
      'cflags_cc': [
        '-fPIC',
      ],
      'all_dependent_settings': {
        'variables': {
          'deps': [
            '<@(exported_deps)',
          ],
        },
      },
      'sources': [
        '<(proto_in_dir)/puffin.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    # puffin library
    {
      'target_name': 'libpuffin',
      'type': 'static_library',
      'cflags_cc': [
        '-fPIC',
      ],
      'dependencies': [
        'puffin-proto',
      ],
      'sources': [
        'src/bit_reader.cc',
        'src/bit_writer.cc',
        'src/huffer.cc',
        'src/huffman_table.cc',
        'src/puff_reader.cc',
        'src/puff_writer.cc',
        'src/puffer.cc',
        'src/puffin_stream.cc',
        'src/stream.cc',
      ],
    },
    # puffdiff library
    {
      'target_name': 'libpuffdiff',
      'type': 'shared_library',
      'dependencies': [
        'libpuffin',
        'puffin-proto',
      ],
      'sources': [
        'src/puffdiff.cc',
        'src/utils.cc',
      ],
      'link_settings': {
        'libraries': [
          '-lbsdiff',
        ],
      },
    },
    # puffpatch library
    {
      'target_name': 'libpuffpatch',
      'type': 'shared_library',
      'dependencies': [
        'libpuffin',
        'puffin-proto',
      ],
      'sources': [
        'src/puffpatch.cc',
      ],
      'link_settings': {
        'libraries': [
          '-lbspatch',
        ],
      },
    },
    # Puffin binary.
    {
      'target_name': 'puffin',
      'type': 'executable',
      'dependencies': [
        'libpuffin',
        'libpuffpatch',
        'libpuffdiff',
      ],
      'sources': [
        'src/main.cc',
      ],
    },
  ],
  # unit tests.
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        # Samples generator.
        {
          'target_name': 'libsample_generator',
          'type': 'static_library',
          'dependencies': [
            'libpuffin',
          ],
          'sources': [
            'src/sample_generator.cc',
          ],
        },
        # Unit tests.
        {
          'target_name': 'puffin_unittest',
          'type': 'executable',
          'dependencies': [
            'libpuffdiff',
            'libpuffpatch',
            'libsample_generator',
            '../../platform2/common-mk/testrunner.gyp:testrunner',
          ],
          'variables': {
            'deps': [
              'zlib',
            ],
          },
          'includes': ['../../platform2/common-mk/common_test.gypi'],
          'sources': [
            'src/bit_io_unittest.cc',
            'src/puff_io_unittest.cc',
            'src/puffin_unittest.cc',
            'src/stream_unittest.cc',
            'src/utils_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
