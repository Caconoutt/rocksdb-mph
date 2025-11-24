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

// === write to file == 
//  {
//    // Open in append mode so each call adds a new line
//    FILE* debug_file = fopen("debugwrite1120.txt", "a");
//    if (debug_file != nullptr) {
//      std::string key_str(key.data(), key.size());
//      fprintf(debug_file, "Write(%s) to hash value %llu, restart index: %zu\n",
//              key_str.c_str(), hash_value,restart_index);
//      fclose(debug_file);
//    }
//  }
}

void DataBlockMPHIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());

  // build mph
  MPH minimal_perfect_hash(key_and_restart_pairs_);

  // level_capacity_.size(), level_capacity_
  uint8_t level_capacity_size = minimal_perfect_hash.level_capacity_.size();
  buffer.push_back(static_cast<char>(level_capacity_size));
  for (uint8_t cap : minimal_perfect_hash.level_capacity_) {
    // fprintf(stderr, "  level_capacity = %u\n", cap);
    // running += sizeof(uint16_t);
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

  // rankprefix.size(), rankprefix
  uint8_t rankPrefix_size = minimal_perfect_hash.bitVector_->rankPrefix.size();
  buffer.push_back(static_cast<char>(rankPrefix_size));
  for (uint8_t r : minimal_perfect_hash.bitVector_->rankPrefix) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&r)),
        sizeof(r));
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
    sizeof(uint8_t) + minimal_perfect_hash.bitVector_->rankPrefix.size() * sizeof(uint8_t) + // rankPrefix
    sizeof(uint8_t) + minimal_perfect_hash.values_.size() * sizeof(uint8_t); // values

  PutFixed32(&buffer, mph_index_size);  
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
  pos += sizeof(uint8_t);
  std::vector<uint8_t> level_capacity(level_capacity_size);
  for (size_t i = 0; i < level_capacity_size; ++i) {
    level_capacity[i] = static_cast<uint8_t>(*pos);
    pos += sizeof(uint8_t);
  }

  // bitVector
  uint8_t bitVector_size = static_cast<uint8_t>(*pos);
  pos += sizeof(uint8_t);
  std::vector<uint64_t> bitVector(bitVector_size);
  for (size_t i = 0; i < bitVector_size; ++i) {
    bitVector[i] = DecodeFixed64(pos);
    pos += sizeof(uint64_t);
  }

  // rankprefix
  uint8_t rankPrefix_size = static_cast<uint8_t>(*pos);
  pos += sizeof(uint8_t);
  std::vector<uint8_t> rankPrefix(rankPrefix_size);
  for (size_t i = 0; i < rankPrefix_size; ++i) {
    rankPrefix[i] = static_cast<uint8_t>(*pos);
    pos += sizeof(uint8_t);
  }

  // values_
  uint8_t values_size = static_cast<uint8_t>(*pos);
  pos += sizeof(uint8_t);
  std::vector<uint8_t> values(values_size);
  for (size_t i = 0; i < values_size; ++i) {
    values[i] = static_cast<uint8_t>(*pos);
    pos += sizeof(uint8_t);
  }
  assert(static_cast<uint32_t>(pos - mph_start) == mph_index_size_);

  // construct BitVector and MPH from deserialized data
  auto bv = std::make_unique<BitVector>(std::move(bitVector), std::move(rankPrefix));
  mph_ = std::make_unique<MPH>(std::move(level_capacity), std::move(values), std::move(bv));
  *map_offset = static_cast<uint16_t>(size - sizeof(uint32_t) - mph_index_size_);
  {
    FILE* debug_file = fopen("debugread1124.txt", "a");
    if (debug_file != nullptr) {

      // Write all caps (level_capacity_)
      fprintf(debug_file, "level_capacity_: ");
      for (uint16_t cap : mph_->level_capacity_) {
        fprintf(debug_file, "%u ", cap);
      }
      fprintf(debug_file, "\n");
      fprintf(debug_file, "rankPrefix: ");
      for (uint16_t r : mph_->bitVector_->rankPrefix) {
        fprintf(debug_file, "%u ", r);
      }
      fprintf(debug_file, "\n");

      fclose(debug_file);
    }
  }
}

uint8_t DataBlockMPHIndex::Lookup(const Slice& key) const {
  return mph_->get(key);
}

// =============== BitVector ===============
inline int popcount(uint64_t x) {
  return __builtin_popcountll(x);  // GCC/Clang builtin
}

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
  // set rankPrefix with an extra element for convenience
  rankPrefix.resize(bitVector.size() + 1);
  size_t sum = 0;
  rankPrefix[0] = 0;
  for (size_t i = 0; i < bitVector.size(); ++i) {
      sum += popcount(bitVector[i]);
      rankPrefix[i + 1] = sum;
  }
}

bool BitVector::get(size_t i) const {
  assert(i / 64 < bitVector.size());
  return bitVector[i >> 6] & (1ULL << (i & 63));
}

size_t BitVector::rank(size_t i) const {
  size_t blockIdx = i >> 6;
  uint64_t mask = (1ULL << (i & 63)) - 1;
  return rankPrefix[blockIdx] + popcount(bitVector[blockIdx] & mask);
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
  // == debug ==
//  {
//    FILE* debug_file = fopen("debugwrite1123.txt", "a");
//    if (debug_file != nullptr) {
//
//      // Write all caps (level_capacity_)
//      fprintf(debug_file, "== in build mph == \n: ");
//      fprintf(debug_file, "level_capacity_: ");
//      for (uint16_t cap : level_capacity_) {
//        fprintf(debug_file, "%u ", cap);
//      }
//      fprintf(debug_file, "\n");
//
//      fclose(debug_file);
//    }
//  }
}

MPH::MPH(std::vector<uint8_t> level_capacity,
    std::vector<uint8_t> values,
    std::unique_ptr<BitVector> bitVector)
    : level_capacity_(std::move(level_capacity)),
      values_(std::move(values)),
      bitVector_(std::move(bitVector)) {}
uint8_t MPH::get(const Slice& key) {
  size_t pos = 0;
  for (size_t level = 0; level < level_capacity_.size(); ++level) {
    uint64_t seed = (level + 1) * kSeedJump;
    auto h = GetSliceHash64(key, seed) % level_capacity_[level];

    if (bitVector_->get(pos + h)) {
        size_t rank = bitVector_->rank(pos + h);
        return values_[rank];
    }
    pos += level_capacity_[level];
  }
  return {};
}
}  // namespace ROCKSDB_NAMESPACE
