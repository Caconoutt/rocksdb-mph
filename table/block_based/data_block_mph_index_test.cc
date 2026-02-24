// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "table/block_based/data_block_mph_index.h"

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "db/table_properties_collector.h"
#include "rocksdb/slice.h"
#include "table/block_based/block.h"
#include "table/block_based/block_based_table_reader.h"
#include "table/block_based/block_builder.h"
#include "table/get_context.h"
#include "table/table_builder.h"
#include "test_util/testharness.h"
#include "test_util/testutil.h"
#include "util/random.h"

namespace ROCKSDB_NAMESPACE {
TEST(DataBlockMPHIndex, Simple) {
  DataBlockMPHIndexBuilder builder;
  builder.Initialize();

  constexpr int num_keys = 10;

  for (int i=0; i < num_keys; i++) {
    std::string key("key" + std::to_string(i));
    uint8_t restart_idx = i;
    builder.Add(key, restart_idx);
  }

  std::string buffer("dummy"), buffer2;
  builder.Finish(buffer);

  buffer2 = buffer;

  Slice s(buffer2);
  DataBlockMPHIndex index;
  uint16_t map_offset;

  index.Initialize(s.data(), static_cast<uint16_t>(s.size()), &map_offset);

  for (int i = 0; i < num_keys; ++i) {
    std::string key("key" + std::to_string(i));
    uint8_t restart_idx = i;
    ASSERT_EQ(index.Lookup(key), restart_idx);
  }
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char** argv) {
  ROCKSDB_NAMESPACE::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
