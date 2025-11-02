#!/bin/bash
set -euo pipefail

RESULTS_FILE="results_hash_index_andrew.txt"

NUM=20000000
SEED=1760593465559160
KEY_SIZE=16
VALUE_SIZE=100
DURATION=60
CACHE=$(( 4 << 30 )) # 4 GB cache

# Clear previous results and write header
echo "# RocksDB point query benchmark (hash index on/off)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# key_size=$KEY_SIZE, value_size=$VALUE_SIZE, reads=warm-up + main (duration=$DURATION)" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

run_test() {
  local HASH_INDEX=$1
  local METHOD_NAME
  if [ "$HASH_INDEX" -eq 1 ]; then
    METHOD_NAME="Hash Index"
    DB_PATH="/tmp/rocksdb-bench/hash"
  else
    METHOD_NAME="Binary Search"
    DB_PATH="/tmp/rocksdb-bench/binary"
  fi

  echo "=== Running test: $METHOD_NAME (use_data_block_hash_index=$HASH_INDEX) ==="

  # Write phase
  ./db_bench \
    --seed=$SEED \
    --benchmarks=filluniquerandom \
    --db="$DB_PATH" \
    --num=$NUM \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --compression_type=none \
    --use_data_block_hash_index=$HASH_INDEX \
    --cache_size=$CACHE \
    --cache_index_and_filter_blocks=1 \
    --pin_l0_filter_and_index_blocks_in_cache=1

  # Read warm-up (1st read, discard output)
  ./db_bench \
    --seed=$SEED \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --db="$DB_PATH" \
    --duration=$DURATION \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --use_existing_keys=1 \
    --compression_type=none > /dev/null

  # Read main (2nd read, capture output)
  OUTPUT=$(./db_bench \
    --seed=$SEED \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --db="$DB_PATH" \
    --duration=$DURATION \
    --key_size=$KEY_SIZE \
    --value_size=$VALUE_SIZE \
    --use_existing_keys=1 \
    --compression_type=none)

  echo "" >> "$RESULTS_FILE"
  echo "### Method: $METHOD_NAME" >> "$RESULTS_FILE"
  echo "### cache_size=$CACHE, use_data_block_hash_index=$HASH_INDEX, num_keys=$NUM" >> "$RESULTS_FILE"
  echo "$OUTPUT" | grep "readrandom" >> "$RESULTS_FILE"
}

run_test 0
run_test 1

echo ""
echo "Done! Results written to $RESULTS_FILE"
