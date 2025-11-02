#!/bin/bash
set -euo pipefail

RESULTS_FILE=results_hash_index.txt
SEED=99382307420011
DB_BASE=/tmp/rocksdb-bench
KEY_SIZE=8              # bytes per key
VALUE_SIZE=8            # bytes per value
CACHE=$((2<<30))        # 2 GB
READS=1000000           # 1 million
NUM_KV=30000000         # 30 million KV < 500 MB
# THREADS=4               # adjust based on CPU cores

# Buffer pool sizes to test (in bytes)
BUFFER_POOLS=(16777216 33554432 67108864 134217728)  # 16,32,64,128 MB

# clear previous results
echo "# RocksDB point query benchmark (hash index on/off)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# value_size=$VALUE_SIZE, reads=$READS, batches=$BATCHES" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# function to run one benchmark
run_case() {
  local HASH_INDEX=$1
  local CACHE_SIZE=$2
  local NUM_KEYS=$3
  local BATCH_ID=$4
  local DB_PATH=${DB_BASE}_batch${BATCH_ID}_cache${CACHE_SIZE}_hash${HASH_INDEX}

  echo "=== [Batch $BATCH_ID] Building DB with use_data_block_hash_index=$HASH_INDEX, cache_size=$CACHE_SIZE, num_keys=$NUM_KEYS ==="
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
  echo "### batch=$BATCH_ID, cache_size=$CACHE_SIZE, use_data_block_hash_index=$HASH_INDEX, num_keys=$NUM_KEYS" >> "$RESULTS_FILE"
  grep "readrandom" tmp.out >> "$RESULTS_FILE"
}

# === Main loop ===
for ((BATCH=1; BATCH<=BATCHES; BATCH++)); do
  echo "========== Starting batch $BATCH / $BATCHES ==========" | tee -a "$RESULTS_FILE"
  for CACHE in "${BUFFER_POOLS[@]}"; do
    NUM_KEYS=$(( CACHE / ((KEY_SIZE + VALUE_SIZE) * OVERHEAD / 10) ))
    run_case false $CACHE $NUM_KEYS $BATCH
    run_case true  $CACHE $NUM_KEYS $BATCH
  done
  echo "" >> "$RESULTS_FILE"
done

echo ""
echo "Done! Results written to $RESULTS_FILE"
