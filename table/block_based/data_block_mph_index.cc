#include "table/block_based/data_block_hash_index.h"
#include "table/block_based/data_block_mph_index.h"

#include <string>
#include <vector>
#include <bit>
#include <unordered_set>

#include "rocksdb/slice.h"
#include "util/coding.h"
#include "util/hash.h"
#include <unistd.h>
#include <limits.h>
#include <cstdio>
namespace ROCKSDB_NAMESPACE {

inline int popcount(uint64_t x) {
  return __builtin_popcountll(x);  // GCC/Clang builtin
}

void DataBlockMPHIndexBuilder::Add(const Slice& key,
                                    const size_t restart_index) {
  assert(Valid());
  if (restart_index > kMaxRestartSupportedByHashIndex) {
    valid_ = false;
    return;
  }

  key_and_restart_pairs_.emplace_back(key.ToString(),
                                      static_cast<uint8_t>(restart_index));
  if (EstimateSize() > kMaxBlockSizeSupportedByHashIndex) {
    valid_ = false;
  }
}

void DataBlockMPHIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());

  // build mph
  MPH minimal_perfect_hash(key_and_restart_pairs_);

  // level_capacity_.size(), level_capacity_
  uint8_t level_capacity_size = minimal_perfect_hash.level_capacity_.size();
  buffer.push_back(static_cast<char>(level_capacity_size));
  for (uint8_t cap : minimal_perfect_hash.level_capacity_) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&cap)),
        sizeof(cap));
  }
  // bitVector.size(), bitVector
  uint8_t bitVector_size = minimal_perfect_hash.bitVector_->bitVector.size();
  buffer.push_back(static_cast<char>(bitVector_size));
  for (uint64_t b : minimal_perfect_hash.bitVector_->bitVector) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&b)),
        sizeof(b));
  }

  // values_.size, values_
  uint8_t values_size = minimal_perfect_hash.values_.size();
  buffer.push_back(static_cast<char>(values_size));
  for (uint8_t v : minimal_perfect_hash.values_) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&v)),
        sizeof(v));
  }

  // mph_index_size
  uint32_t mph_index_size = 
    sizeof(uint8_t) + minimal_perfect_hash.level_capacity_.size() * sizeof(uint8_t) +   // level_capacity
    sizeof(uint8_t) + minimal_perfect_hash.bitVector_->bitVector.size() * sizeof(uint64_t) + // bitVector
    // sizeof(uint8_t) + minimal_perfect_hash.bitVector_->rankPrefix.size() * sizeof(uint8_t) + // rankPrefix
    sizeof(uint8_t) + minimal_perfect_hash.values_.size() * sizeof(uint8_t); // values

  PutFixed32(&buffer, mph_index_size);

  // === mph detail === 
//  {
//   FILE* mph_file = fopen("mph_detail_.txt", "a");
//   if (mph_file != nullptr) {

//   // write entries, size of level_capacity_, bitVector, rankPrefix, values, mph
//   fprintf(mph_file,
//     "Block finished: {\"num_entry\": %zu, \"num_levels\": %zu, "
//     "\"num_bv\": %zu, \"num_rp\": %zu, \"num_value\": %zu, \"mph_size\": %u}\n",
//     key_and_restart_pairs_.size(),
//     minimal_perfect_hash.level_capacity_.size(),
//     minimal_perfect_hash.bitVector_->bitVector.size(),
//     minimal_perfect_hash.bitVector_->rankPrefix.size(),
//     minimal_perfect_hash.values_.size(),
//     mph_index_size
//   );

//     fclose(mph_file);
//   }
//  }
}

void DataBlockMPHIndexBuilder::Reset() {
  valid_ = true;
  key_and_restart_pairs_.clear();
}

void DataBlockMPHIndex::Initialize(const char* data, uint32_t size,
                                    uint16_t* map_offset) {
  assert(data != nullptr);
  assert(size >= sizeof(uint32_t)); 

  mph_index_size_ = DecodeFixed32(data + size - sizeof(uint32_t));
  const char* mph_start = data + size - sizeof(uint32_t) - mph_index_size_;
  const char* pos = mph_start;

  // level_capacity_
  uint8_t level_capacity_size = static_cast<uint8_t>(*pos);
  level_offset_ = pos - data + sizeof(uint8_t);
  pos = pos + sizeof(uint8_t) + level_capacity_size * sizeof(uint8_t);
  // bitVector
  uint8_t bv_size = static_cast<uint8_t>(*pos);
  bitVector_offset_ = pos - data + sizeof(uint8_t);
  pos = pos + sizeof(uint8_t) + bv_size * sizeof(uint64_t);
  // values_
  uint8_t values_size = static_cast<uint8_t>(*pos);
  value_offset_ = pos - data + sizeof(uint8_t);
  pos = pos + sizeof(uint8_t) + values_size * sizeof(uint8_t);

  assert(static_cast<uint32_t>(pos - mph_start) == mph_index_size_);

  *map_offset = static_cast<uint16_t>(size - sizeof(uint32_t) - mph_index_size_);
}

uint8_t DataBlockMPHIndex::Lookup(const char* data, const Slice& key) const {
  size_t pos = 0;
  uint8_t level_capacity_size =
    *(reinterpret_cast<const uint8_t*>(data + level_offset_ - 1));

  const uint8_t* level_capacity = reinterpret_cast<const uint8_t*>(data + level_offset_);
  const uint64_t* bit_vector = reinterpret_cast<const uint64_t*>(data + bitVector_offset_);
  const uint8_t* values = reinterpret_cast<const uint8_t*>(data + value_offset_);

  for (size_t level = 0; level < level_capacity_size; ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    uint8_t cap = level_capacity[level];
    auto h = GetSliceHash64(key, seed) % cap;

    // Check bit at position (pos + h) in bit_vector
    size_t bit_pos = pos + h;
    size_t block_idx = bit_pos >> 6; // divide by 64
    uint64_t mask = (1ULL << (bit_pos & 63)) - 1;

    if (bit_vector[block_idx] & (1ULL << (bit_pos & 63))) {
      size_t rank = 0;
      for (size_t i = 0; i < block_idx; i++) {
        rank += popcount(bit_vector[i]);
      }
      rank += popcount(bit_vector[block_idx] & mask);

      return values[rank];
    }
    pos += cap;
  }
  return {};
}

// =============== BitVector ===============
BitVector::BitVector(const std::vector<bool>& v) {
  // set bitVector
  bitVector.assign((v.size() + 63) / 64, 0);
  for (size_t i = 0; i < v.size(); ++i) {
      if (v[i]) {
          // i >> 6 is same as i / 64 to calculate which block of bitVector in
          // i & 63 same as i % 64 to give the position of the bit inside that 64-bit block
          bitVector[i >> 6] |= (1ULL << (i & 63));
      }
  }
}

bool BitVector::get(size_t i) const {
  assert(i / 64 < bitVector.size());
  return bitVector[i >> 6] & (1ULL << (i & 63));
}

size_t BitVector::rank(size_t i) const {
  size_t blockIdx = i >> 6;
  uint64_t mask = (1ULL << (i & 63)) - 1;
  size_t rank = 0;

  for (size_t b = 0; b < blockIdx ; b++) {
    rank += popcount(bitVector[b]);
  }
  rank += popcount(bitVector[blockIdx] & mask);
  return rank;
}
// =============== MPH ===============
MPH::MPH(const std::vector<std::pair<std::string, uint8_t>>& kvs) {
  values_.reserve(kvs.size());

  std::vector<std::pair<std::string, uint8_t>> keys;
  keys.reserve(kvs.size());
  for(size_t i = 0; i < kvs.size(); ++i){
      keys.emplace_back(kvs[i].first, i);
  }

  std::vector<bool> bitVectorInput;

  for (size_t level = 0; !keys.empty(); ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    uint8_t cap = static_cast<uint16_t>(keys.size());
    level_capacity_.push_back(cap);

    uint8_t ones = 0;
    std::vector<uint8_t> used(cap, 0), h(cap, 0), h_idx(cap, 0);

    // Try hashing in the level
    for (uint8_t i = 0; i < cap; ++i) {
      h[i] = GetSliceHash64(keys[i].first, seed) % cap;
      h_idx[h[i]] = i;
      ++used[h[i]];

      ones += used[h[i]] == 1;
      ones -= used[h[i]] == 2;
    }

    // Assign bits and values for unique keys
    bitVectorInput.reserve(bitVectorInput.size() + cap);
    for (uint8_t i = 0; i < cap; ++i) {
        if (used[i] == 1) {
            bitVectorInput.push_back(true);
            values_.emplace_back(kvs[keys[h_idx[i]].second].second);
        } else {
            bitVectorInput.push_back(false);
        }
    }

    std::vector<std::pair<std::string, uint8_t>> nxt;
    nxt.reserve(cap - ones);
    for (uint16_t i = 0; i < cap; ++i) {
      if (used[h[i]] != 1) {
        nxt.push_back(keys[i]);
      }
    }
    keys.swap(nxt);
  }
  bitVector_ = std::make_unique<BitVector>(bitVectorInput);
}

}  // namespace ROCKSDB_NAMESPACE
