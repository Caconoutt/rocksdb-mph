#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cmath>


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
// NUM_LEVEL_CAPCITY: Number of level_capacity, uint8_t
// L: value of capacity at each level, uint8_t
// NUM_BIT_VECTOR: Number of bitVector, uint8_t
// BV: value of bitVector at each index, uint64_t
// NUM_RANKPREFIX: Number of rankPrefix, uint8_t
// R: value of rankPrefix at each index, uint8_t
// NUM_VALUES: Number of values, uint8_t
// V: RI index, uint8_t
// MPH_IDX_SIZE: the size of the MPH_IDX to help locate to the start of MPH_IDX, uint32_t
//

const uint64_t kSeedJump = 989898989; // to get the seed big enough for rehash in levels

class DataBlockMPHIndexBuilder {
 public:
  DataBlockMPHIndexBuilder()
      : 
        valid_(false) {}

  void Initialize() {
    valid_ = true;
  }

  inline bool Valid() const { return valid_; }
  void Add(const Slice& key, const size_t restart_index);
  void Finish(std::string& buffer);
  void Reset();
  inline size_t EstimateSize() const {
    size_t n = key_and_restart_pairs_.size();
    // estimate bitVector
    // size_t estimated_num_bitvector_words = std::ceil((3 * n) / 64); // uint64_t
    size_t estimated_num_bitvector_words = std::ceil((1.23 * n) / 64); // uint64_t serialize of bit, 1.23 is some estimate
    size_t estimated_bit_vector_size = 
      sizeof(uint8_t) + 
      estimated_num_bitvector_words * sizeof(uint64_t);
    // estimate rank
    size_t rank_prefix_size = 
      sizeof(uint8_t) + 
      ceil(estimated_num_bitvector_words / sizeof(uint8_t));
    // estimate value
    size_t value_size = 
      sizeof(uint8_t) + 
      n * sizeof(uint8_t); // value size
    // estimate level
    size_t level_capacities_size = 
      sizeof(uint8_t) + 
      std::ceil(std::log2(n)) * sizeof(uint8_t);
    size_t footer = sizeof(uint32_t);

    return estimated_bit_vector_size + rank_prefix_size + value_size + level_capacities_size + footer;
  }

 private:
  bool valid_;
  std::vector<std::pair<std::string, uint8_t>> key_and_restart_pairs_;
  friend class DataBlockHashIndex_DataBlockHashTestSmall_Test;
};

// ============== Minimal Pefect Hashing ===============
class BitVector {
public:
    explicit BitVector(const std::vector<bool>& v);

    // BitVector(std::vector<uint64_t> bitVector_, std::vector<uint8_t> rankPrefix_)
    //     : bitVector(std::move(bitVector_)), rankPrefix(std::move(rankPrefix_)) {}

    bool get(size_t i) const;
    size_t rank(size_t i) const;

// private:
    std::vector<uint64_t> bitVector;
    // std::vector<uint8_t> rankPrefix;
};

class MPH {
public:
    explicit MPH(const std::vector<std::pair<std::string, uint8_t>>& kvs);

    // MPH(std::vector<uint8_t> level_capacity,
    //     std::vector<uint8_t> values,
    //     std::unique_ptr<BitVector> bitVector);

    // std::uint8_t get(const Slice& key);

// private:
    std::vector<uint8_t> level_capacity_;
    std::vector<uint8_t> values_;
    std::unique_ptr<BitVector> bitVector_;
};
// ============== Minimal Pefect Hashing ===============
class DataBlockMPHIndex {
 public:
  DataBlockMPHIndex() : mph_index_size_(0), level_offset_(0), bitVector_offset_(0), value_offset_(0) {}

  void Initialize(const char* data, uint32_t size, uint16_t* map_offset);

  uint8_t Lookup(const char* data, const Slice& key) const;

  inline bool Valid() { return 
    mph_index_size_ != 0 && 
    level_offset_ != 0 && 
    bitVector_offset_ != 0 && 
    value_offset_ != 0; 
  }

 private:
  // std::unique_ptr<MPH> mph_;
  uint32_t mph_index_size_;
  uint8_t level_capacity_size_;
  uint32_t level_offset_;
  uint8_t bv_size_;
  uint32_t bitVector_offset_;
  uint8_t values_size_;
  uint32_t value_offset_;
};
}  // namespace ROCKSDB_NAMESPACE
