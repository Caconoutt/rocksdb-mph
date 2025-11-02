#!/bin/bash
set -euo pipefail

RESULTS_FILE=results_hash_index.txt
SEED=99382307420011
DB_BASE=/tmp/rocksdb-bench
KEY_SIZE=8              # bytes per key
VALUE_SIZE=8            # bytes per value
CACHE_SIZE=$((2<<30))   # 2 GB
READS=1000000           # 1 million
NUM_KV=30000000         # 30 million KV < 500 MB
DURATION=120            # early stop

# Header
echo "# RocksDB point query benchmark (hash index on/off)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# key_size=$KEY_SIZE, value_size=$VALUE_SIZE, reads=$READS" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# Function to run tests per hash_index option
run_test() {
  local HASH_INDEX=$1
  if [ "$HASH_INDEX" -eq 1 ]; then
    DB_PATH="${DB_BASE}/hash"
  else
    DB_PATH="${DB_BASE}/binary"
  fi
  mkdir -p "$(dirname "$DB_PATH")"

  echo "=== Running with use_data_block_hash_index=$HASH_INDEX and db=$DB_PATH ==="

  # Write phase
  ./db_bench \
    --seed=$SEED \
    --benchmarks=filluniquerandom \
    --db="${DB_PATH}" \
    --num=$NUM_KV \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --compression_type=none \
    --cache_size=$CACHE_SIZE \
    --cache_index_and_filter_blocks=1 \
    --pin_l0_filter_and_index_blocks_in_cache=1 \
    --use_data_block_hash_index=$HASH_INDEX
    # --statistics=1

  # Read warm-up
  ./db_bench \
    --seed=$SEED \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --use_existing_keys=1 \
    --db="${DB_PATH}" \
    --num=$READS \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --cache_size=$CACHE_SIZE \
    --use_data_block_hash_index=$HASH_INDEX \
    --duration=$DURATION

  # Read
  ./db_bench \
    --seed=$SEED \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --use_existing_keys=1 \
    --db="${DB_PATH}" \
    --num=$READS \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --cache_size=$CACHE_SIZE \
    --use_data_block_hash_index=$HASH_INDEX \
    --duration=$DURATION | tee tmp.out


  # Append results to file
  echo "" >> "$RESULTS_FILE"
  echo "### cache_size=$CACHE_SIZE, use_data_block_hash_index=$HASH_INDEX, num_keys=$NUM_KV" >> "$RESULTS_FILE"
  grep "readrandom" tmp.out >> "$RESULTS_FILE"
}

# Run both cases
run_test 0
run_test 1

echo ""
echo "Done! Results written to $RESULTS_FILE"
