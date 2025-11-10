#include "table/block_based/data_block_hash_index.h"
#include "table/block_based/data_block_mph_index.h"

#include <string>
#include <vector>
#include <bit>
#include <unordered_set>

#include "rocksdb/slice.h"
#include "util/coding.h"
#include "util/hash.h"

namespace ROCKSDB_NAMESPACE {

void DataBlockMPHIndexBuilder::Add(const Slice& key,
                                    const size_t restart_index) {
  assert(Valid());
  // if (restart_index > kMaxRestartSupportedByHashIndex) {
  //   valid_ = false;
  //   return;
  // }

  uint32_t hash_value = GetSliceHash(key);
  hash_and_restart_pairs_.emplace_back(hash_value,
                                       static_cast<uint8_t>(restart_index));
  // estimated_num_buckets_ += bucket_per_key_;
}

void DataBlockMPHIndexBuilder::Finish(std::string& buffer) {
  assert(Valid());
  
  // build mph
  MPH minimal_perfect_hash(hash_and_restart_pairs_);

  // level_capacity_.size(), level_capacity_
  uint16_t level_capacity_size = minimal_perfect_hash.level_capacity_.size();
  PutFixed16(&buffer, level_capacity_size);
  for (uint32_t cap : minimal_perfect_hash.level_capacity_) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&cap)),
        sizeof(cap));
  }
  // bitVector.size(), bitVector
  uint32_t bitVector_size = minimal_perfect_hash.bitVector_->bitVector.size();
  PutFixed32(&buffer, bitVector_size);
  for (uint64_t b : minimal_perfect_hash.bitVector_->bitVector) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&b)),
        sizeof(b));
  }
  // rankprefix.size(), rankprefix
  uint32_t rankPrefix_size = minimal_perfect_hash.bitVector_->rankPrefix.size();
  PutFixed32(&buffer, rankPrefix_size);
  for (uint32_t r : minimal_perfect_hash.bitVector_->rankPrefix) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&r)),
        sizeof(r));
  }
  // values_.size, values_
  uint32_t values_size = minimal_perfect_hash.values_.size();
  PutFixed32(&buffer, values_size);
  for (uint8_t v : minimal_perfect_hash.values_) {
    buffer.append(
        const_cast<const char*>(reinterpret_cast<char*>(&v)),
        sizeof(v));
  }
  // MPH_IDX_SIZE
  uint32_t mph_index_size = 
    sizeof(uint16_t) + minimal_perfect_hash.level_capacity_.size() * sizeof(uint32_t) +   // level_capacity
    sizeof(uint32_t) + minimal_perfect_hash.bitVector_->bitVector.size() * sizeof(uint64_t) + // bitVector
    sizeof(uint32_t) + minimal_perfect_hash.bitVector_->rankPrefix.size() * sizeof(uint32_t) + // rankPrefix
    sizeof(uint32_t) + minimal_perfect_hash.values_.size() * sizeof(uint8_t); // values

PutFixed32(&buffer, mph_index_size);  

  // assert(buffer.size() <= kMaxBlockSizeSupportedByHashIndex);
}

void DataBlockMPHIndexBuilder::Reset() {
  // estimated_num_buckets_ = 0;
  valid_ = true;
  hash_and_restart_pairs_.clear();
}

void DataBlockMPHIndex::Initialize(const char* data, uint16_t size,
                                    uint16_t* map_offset) {
  assert(data != nullptr);
  // assert(size >= sizeof(uint16_t));  //?
  mph_index_size_ = DecodeFixed32(data + size - sizeof(uint32_t));
  const char* mph_start = data + size - sizeof(uint32_t) - mph_index_size_;

  const char* pos = mph_start;
  // level_capacity_
  uint16_t level_capacity_size = DecodeFixed16(pos);
  pos += sizeof(uint16_t);
  std::vector<uint32_t> level_capacity(level_capacity_size);
  for (size_t i = 0; i < level_capacity_size; ++i) {
    level_capacity[i] = DecodeFixed32(pos);
    pos += sizeof(uint32_t);
  }

  // bitVector
  uint32_t bitVector_size = DecodeFixed32(pos);
  pos += sizeof(uint32_t);
  std::vector<uint64_t> bitVector(bitVector_size);
  for (size_t i = 0; i < bitVector_size; ++i) {
    bitVector[i] = DecodeFixed64(pos);
    pos += sizeof(uint64_t);
  }

  // rankprefix
  uint32_t rankPrefix_size = DecodeFixed32(pos);
  pos += sizeof(uint32_t);
  std::vector<uint32_t> rankPrefix(rankPrefix_size);
  for (size_t i = 0; i < rankPrefix_size; ++i) {
    rankPrefix[i] = DecodeFixed32(pos);
    pos += sizeof(uint32_t);
  }

  // values_
  uint32_t values_size = DecodeFixed32(pos);
  pos += sizeof(uint32_t);
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
}

uint8_t DataBlockMPHIndex::Lookup(const Slice& key) const {
  uint32_t hash_value = GetSliceHash(key);
  return mph_->get(hash_value);
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
MPH::MPH(const std::vector<std::pair<uint32_t, uint8_t>>& kvs) {
    values_.reserve(kvs.size());

    std::vector<std::pair<uint32_t, size_t>> keys;
    keys.reserve(kvs.size());
    for(size_t i = 0; i < kvs.size(); ++i){
        keys.emplace_back(kvs[i].first, i);
    }

    std::vector<bool> bitVectorInput;

//    for (size_t level = 0; !keys.empty(); ++level) {
    while (!keys.empty()) {
        size_t cap = keys.size();

        size_t ones = 0;
        std::vector<size_t> used, h, h_idx;
        std::vector<std::pair<uint32_t, size_t>> nxt;

        // Try hashing and increase cap if no unique keys found
        while (true) {
            used.assign(cap, 0);
            h.assign(cap, 0);
            h_idx.assign(cap, 0);

            for (size_t i = 0; i < keys.size(); ++i) {
                h[i] = keys[i].first % cap;
                h_idx[h[i]] = i;
                ++used[h[i]];
            }

            ones = 0;
            for (size_t i = 0; i < cap; ++i) {
                ones += (used[i] == 1);
            }

            if (ones > 0) break; // success, we have unique keys
            cap *= 2;            // double capacity and try again
        }
        level_capacity_.push_back(cap);

        // Assign bits and values for unique keys
        bitVectorInput.reserve(bitVectorInput.size() + cap);
        for (size_t i = 0; i < cap; ++i) {
            if (used[i] == 1) {
                bitVectorInput.push_back(true);
                values_.emplace_back(kvs[keys[h_idx[i]].second].second);
            } else {
                bitVectorInput.push_back(false);
            }
        }

        // Prepare remove_key_set for keys uniquely assigned
        std::unordered_set<size_t> remove_key_set;
        remove_key_set.reserve(ones);
        for (size_t i = 0; i < cap; ++i) {
            if (used[i] == 1) remove_key_set.insert(h_idx[i]);
        }

        // Filter keys for next iteration
        nxt.reserve(keys.size() - ones);
        for (size_t i = 0; i < keys.size(); ++i) {
            if (remove_key_set.find(i) == remove_key_set.end()) {
                nxt.push_back(keys[i]);
            }
        }
        keys.swap(nxt);
    }
    bitVector_ = std::make_unique<BitVector>(bitVectorInput);
}

MPH::MPH(std::vector<uint32_t> level_capacity,
    std::vector<uint8_t> values,
    std::unique_ptr<BitVector> bitVector)
    : level_capacity_(std::move(level_capacity)),
      values_(std::move(values)),
      bitVector_(std::move(bitVector)) {}
uint8_t MPH::get(uint32_t key) {
    size_t pos = 0;
    for (size_t level = 0; level < level_capacity_.size(); ++level) {
        auto h = key % level_capacity_[level];
        if (bitVector_->get(pos + h)) {
            size_t rank = bitVector_->rank(pos + h);
            return values_[rank];
        }
        pos += level_capacity_[level];
    }
    return {};
}
}  // namespace ROCKSDB_NAMESPACE
