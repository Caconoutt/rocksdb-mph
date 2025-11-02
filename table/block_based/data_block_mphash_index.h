// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rocksdb/slice.h"

namespace ROCKSDB_NAMESPACE {
// This is an experimental feature aiming to reduce the CPU utilization of
// point-lookup within a data-block. It is only used in data blocks, and not
// in meta-data blocks or per-table index blocks. This aims to improve the search
// using the perfect hash to locate the restart ptr for each seen key
//
// It only used to support BlockBasedTable::Get().
//
// A serialized hash index is appended to the data-block. The new block data
// format is as follows:
//
// DATA_BLOCK: [RI RI RI ... RI RI_IDX MPH_BUCKET MPH_VALUE FOOTER]
//
// RI:       Restart Interval (the same as the default data-block format)
// RI_IDX:   Restart Interval index (the same as the default data-block format)
// MPH_BUCKET: The minimal perfect hashing bucket that maps each key to specific bucket
// MPH_VALUE: The value list to for each bucket, stores restart ptr
// FOOTER:   A 32bit block footer, which is the NUM_RESTARTS with the MSB as
//           the flag indicating if this hash index is in use. Note that
//           given a data block < 32KB, the MSB is never used. So we can
//           borrow the MSB as the hash index flag. Therefore, this format is
//           compatible with the legacy data-blocks with num_restarts < 32768,
//           as the MSB is 0.
//
// The format of the data-block hash index is as follows:
//
// MPH_BUCKET: [NUM_CAPACITY_LEVEL, OFFSET_TO_values_, OFFSET_TO_bitVector, level_capacity_, bitVector]
//
// NUM_CAPACITY_LEVEL: number of levels
// OFFSET_TO_values_:  offset to values_ that stores the RIptr
// OFFSET_TO_bitVector:  offset to bitVector that is the bucket that each key falls into
// level_capacity_: list of capacity for each level
// bitVector: hash mapping for each key
//
// MPH_VALUE: [RIptr RIptr ... RIptr]
//
// RIptr: RI ptr for the key
//
// We reserve two special flag: <- NO NEED
//    kNoEntry=255,
//    kCollision=254.
//
// When storing a key when MPH is used, the key is first hashed and stored in 
// a list, with a copy that considers more bit number to avoid collision later
// when building the MPH. In constructing MPH, we use the hashed value to map to
// each bucket, if hash value is unique to a bucket, that is marked as 1; if 
// there's a collision, those bucket marked as 0 in the bitVector and moved to next
// level to use the more bit version of hashed value to map.
// 
//
// During query process, a key is first hashed with all required bit size(default,
// and more to avoid collision). Then we examine if the maapped buckets store 
// the key, if not, move to next level to search recursively.
// When found, we count the rank before the idx in the bitVector and read the 
// restart ptr from values_.
// ??? bottom this key???
// Note that we only support blocks with #restart_interval < 254. If a block
// has more restart interval than that, hash index will not be create for it.

const uint8_t kNoEntry = 255;
const uint8_t kCollision = 254;
const uint8_t kMaxRestartSupportedByHashIndex = 253;

// Because we use uint16_t address, we only support block no more than 64KB
const size_t kMaxBlockSizeSupportedByHashIndex = 1u << 16;
const double kDefaultUtilRatio = 0.75;

class DataBlockMPHashIndexBuilder {
 public:
  DataBlockMPHashIndexBuilder()
      : bucket_per_key_(-1 /*uninitialized marker*/),
        estimated_num_buckets_(0),
        valid_(false) {}

  void Initialize(double util_ratio) {
    if (util_ratio <= 0) {
      util_ratio = kDefaultUtilRatio;  // sanity check
    }
    bucket_per_key_ = 1 / util_ratio;
    valid_ = true;
  }

  inline bool Valid() const { return valid_ && bucket_per_key_ > 0; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    uint16_t estimated_num_buckets =
        static_cast<uint16_t>(estimated_num_buckets_);

    // Maching the num_buckets number in DataBlockMPHashIndexBuilder::Finish.
    estimated_num_buckets |= 1;

    return sizeof(uint16_t) +
           static_cast<size_t>(estimated_num_buckets * sizeof(uint8_t));
  }

 private:
  double bucket_per_key_;  // is the multiplicative inverse of util_ratio_
  double estimated_num_buckets_;

  // Now the only usage for `valid_` is to mark false when the inserted
  // restart_index is larger than supported. In this case HashIndex is not
  // appended to the block content.
  bool valid_;

  std::vector<std::pair<uint32_t, uint8_t>> hash_and_restart_pairs_;
  friend class DataBlockHashIndex_DataBlockHashTestSmall_Test;
};

class DataBlockHashIndex {
 public:
  DataBlockHashIndex() : num_buckets_(0) {}

  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, uint32_t map_offset, const Slice& key) const;

  inline bool Valid() { return num_buckets_ != 0; }

 private:
  // To make the serialized hash index compact and to save the space overhead,
  // here all the data fields persisted in the block are in uint16 format.
  // We find that a uint16 is large enough to index every offset of a 64KiB
  // block.
  // So in other words, DataBlockHashIndex does not support block size equal
  // or greater then 64KiB.
  uint16_t num_buckets_;
};

}  // namespace ROCKSDB_NAMESPACE
