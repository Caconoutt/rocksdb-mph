#!/bin/bash
set -euo pipefail

DB_BASE=/tmp/rocksdb-bench
KEY_SIZE=16             # bytes per key
VALUE_SIZE=100          # bytes per value
READS=1000000           # number of reads per test
THREADS=4               # adjust based on CPU cores
RESULTS_FILE=results_hash_index.txt
OVERHEAD=15            # 1.5 factor, scaled as integer percent

# Buffer pool sizes to test (in bytes)
BUFFER_POOLS=(16777216 33554432 67108864 134217728)  # 16,32,64,128 MB

# clear previous results
echo "# RocksDB point query benchmark (hash index on/off)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# value_size=$VALUE_SIZE, reads=$READS" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# function to run one benchmark
run_case() {
  local HASH_INDEX=$1
  local CACHE_SIZE=$2
  local NUM_KEYS=$3
  local DB_PATH=${DB_BASE}_cache${CACHE_SIZE}_hash${HASH_INDEX}

  echo "=== Building DB with use_data_block_hash_index=$HASH_INDEX, cache_size=$CACHE_SIZE, num_keys=$NUM_KEYS ==="
  rm -rf "$DB_PATH"

  ./db_bench \
    --seed=1760593465559160 \
    --benchmarks=filluniquerandom \
    --num=$NUM_KEYS \
    --value_size=$VALUE_SIZE \
    --cache_size=$CACHE_SIZE \
    --db=$DB_PATH \
    --disable_wal=1 \
    --use_data_block_hash_index=$HASH_INDEX \
    --use_direct_io_for_flush_and_compaction \
    --use_direct_reads=false \
    --threads=$THREADS

  ./db_bench \
    --seed=1760593465559160 \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --num=$READS \
    --cache_size=$CACHE_SIZE \
    --db=$DB_PATH \
    --use_data_block_hash_index=$HASH_INDEX \
    --use_direct_reads=false \
    --duration=60 \
    --threads=$THREADS \
    | tee tmp.out

  echo "" >> "$RESULTS_FILE"
  echo "### cache_size=$CACHE_SIZE, use_data_block_hash_index=$HASH_INDEX, num_keys=$NUM_KEYS" >> "$RESULTS_FILE"
  grep "readrandom" tmp.out >> "$RESULTS_FILE"
}

# Loop over buffer pool sizes
for CACHE in "${BUFFER_POOLS[@]}"; do
  # Compute number of keys that roughly fit in cache
  NUM_KEYS=$(( CACHE / ((KEY_SIZE + VALUE_SIZE) * OVERHEAD / 10) ))
  run_case false $CACHE $NUM_KEYS
  run_case true  $CACHE $NUM_KEYS
done

echo ""
echo "Done! Results written to $RESULTS_FILE"
cat "$RESULTS_FILE"
