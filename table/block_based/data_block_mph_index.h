// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>


#include "rocksdb/slice.h"

namespace ROCKSDB_NAMESPACE {
// This is an experimental feature aiming to reduce the CPU utilization of
// point-lookup within a data-block. It is only used in data blocks, and not
// in meta-data blocks or per-table index blocks.
//
// It only used to support BlockBasedTable::Get().
//
// A serialized minimal perfect hash index is appended to the data-block. The new block data
// format is as follows:
//
// DATA_BLOCK: [RI RI RI ... RI RI_IDX MPH_IDX FOOTER]
//
// RI:       Restart Interval (the same as the default data-block format)
// RI_IDX:   Restart Interval index (the same as the default data-block format)
// MPH_IDX: The new data-block minimal perfect hash index feature.
// FOOTER:   A 32bit block footer, which is the NUM_RESTARTS with the MSB as
//           the flag indicating if this hash index is in use. Note that
//           given a data block < 32KB, the MSB is never used. So we can
//           borrow the MSB as the hash index flag. Therefore, this format is
//           compatible with the legacy data-blocks with num_restarts < 32768,
//           as the MSB is 0.
//
// The format of the data-block hash index is as follows:
//
// MPH_IDX: [
//  NUM_LEVEL_CAPCITY L ... L 
//  NUM_BIT_VECTOR BV ... BV 
//  NUM_RANKPREFIX R ... R 
//  NUM_VALUES V ... V 
//  MPH_IDX_SIZE
// ]
//
// NUM_LEVEL_CAPCITY: Number of level_capacity, uint16_t
// L: value of capacity at each level, uint32_t
// NUM_BIT_VECTOR: Number of bitVector, uint32_t
// BV: value of bitVector at each index, uint64_t
// NUM_RANKPREFIX: Number of rankPrefix, uint32_t
// R: value of rankPrefix at each index, uint32_t
// NUM_VALUES: Number of values, uint32_t
// V: RI index, uint8_t
// MPH_IDX_SIZE: the size of the MPH_IDX to help locate to the start of MPH_IDX
//
// ============ TODO: change following ============
// We reserve two special flag:
//    kNoEntry=255,
//    kCollision=254.

// Therefore, the max number of restarts this hash index can supoport is 253.

// Buckets are initialized to be kNoEntry.
//
// When storing a key in the hash index, the key is first hashed to a bucket.
// If there the bucket is empty (kNoEntry), the restart index is stored in
// the bucket. If there is already a restart index there, we will update the
// existing restart index to a collision marker (kCollision). If the
// the bucket is already marked as collision, we do not store the restart
// index either.
//
// During query process, a key is first hashed to a bucket. Then we examine if
// the buckets store nothing (kNoEntry) or the bucket had a collision
// (kCollision). If either of those happens, we get the restart index of
// the key and will directly go to the restart interval to search the key.
//
// Note that we only support blocks with #restart_interval < 254. If a block
// has more restart interval than that, hash index will not be create for it.

// const uint8_t kNoEntry = 255;
// const uint8_t kCollision = 254;
// const uint8_t kMaxRestartSupportedByHashIndex = 253;

// TODO: look at here if need to also add size constraint becuase the uint16_t?
// // Because we use uint16_t address, we only support block no more than 64KB
// const size_t kMaxBlockSizeSupportedByHashIndex = 1u << 16;
// const double kDefaultUtilRatio = 0.75;

class DataBlockMPHIndexBuilder {
 public:
  DataBlockMPHIndexBuilder()
      : 
        // estimated_num_buckets_(0),
        valid_(false) {}

  void Initialize() {
    valid_ = true;
  }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  // inline size_t EstimateSize() const {
  //   uint16_t estimated_num_buckets =
  //       static_cast<uint16_t>(estimated_num_buckets_);

  //   // Maching the num_buckets number in DataBlockMPHIndexBuilder::Finish.
  //   estimated_num_buckets |= 1;

  //   return sizeof(uint16_t) +
  //          static_cast<size_t>(estimated_num_buckets * sizeof(uint8_t));
  // }

 private:
  // double bucket_per_key_;  // is the multiplicative inverse of util_ratio_
  // double estimated_num_buckets_;

  // Now the only usage for `valid_` is to mark false when the inserted
  // restart_index is larger than supported. In this case HashIndex is not
  // appended to the block content.
  bool valid_;

  std::vector<std::pair<uint32_t, uint8_t>> hash_and_restart_pairs_;
  friend class DataBlockHashIndex_DataBlockHashTestSmall_Test;
};

// ============== Minimal Pefect Hashing ===============
class BitVector {
public:
    explicit BitVector(const std::vector<bool>& v);

    BitVector(std::vector<uint64_t> bitVector_, std::vector<uint32_t> rankPrefix_)
        : bitVector(std::move(bitVector_)), rankPrefix(std::move(rankPrefix_)) {}

    bool get(size_t i) const;
    size_t rank(size_t i) const;

// private:
    std::vector<uint64_t> bitVector;
    std::vector<uint32_t> rankPrefix;
};

class MPH {
public:
    explicit MPH(const std::vector<std::pair<uint32_t, uint8_t>>& kvs);

    MPH(std::vector<uint32_t> level_capacity,
        std::vector<uint8_t> values,
        std::unique_ptr<BitVector> bitVector);

    std::uint8_t get(uint32_t key);

// private:
    std::vector<uint32_t> level_capacity_;
    std::vector<uint8_t> values_;
    std::unique_ptr<BitVector> bitVector_;
};
// ============== Minimal Pefect Hashing ===============
class DataBlockMPHIndex {
 public:
  DataBlockMPHIndex() : mph_(nullptr), mph_index_size_(0) {}

  void Initialize(const char* data, uint16_t size, uint16_t* map_offset);

  uint8_t Lookup(const Slice& key) const;

  inline bool Valid() { return mph_ != nullptr && mph_index_size_ != 0; }

 private:
  // save these to reconstruct mph for look up(?)
  // const char* data_; // serialized data
  // uint16_t size_; // how many should read from data_(?)
  // uint16_t map_offset_; // offset to read from data_(?)

  std::unique_ptr<MPH> mph_;
  uint32_t mph_index_size_;
};
}  // namespace ROCKSDB_NAMESPACE
