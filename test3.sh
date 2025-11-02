#!/bin/bash
set -euo pipefail
# script for different restart intervals (RI)

RESULTS_FILE=results_RI_hash_index.txt
SEED=99382307420011
DB_BASE=/tmp/rocksdb-bench
KEY_SIZE=8              # bytes per key
VALUE_SIZE=8            # bytes per value
CACHE_SIZE=$((2<<30))   # 2 GB
READS=1000000           # 1 million
NUM_KV=30000000         # 30 million KV < 500 MB
DURATION=120
RESTART_INTERVAL_SIZES=(1 2 4 8 16)  # test values

# clear previous results
echo "# RocksDB point query benchmark (hash index on/off)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# key_size=$KEY_SIZE, value_size=$VALUE_SIZE, reads=$READS" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

run_case() {
    local HASH_INDEX=$1   # 0 or 1
    local RI_SIZE=$2
    local DB_PATH="${DB_BASE}_RI${RI_SIZE}_${HASH_INDEX}"

    echo "=== Building DB with use_data_block_hash_index=$HASH_INDEX, restart_interval_size=$RI_SIZE ==="
    rm -rf "$DB_PATH"
    mkdir -p "$DB_PATH"

    # fill the DB
    ./db_bench \
        --seed=$SEED \
        --benchmarks=filluniquerandom \
        --db="$DB_PATH" \
        --num=$NUM_KV \
        --key_size=$KEY_SIZE \
        --value_size=$VALUE_SIZE \
        --compression_type=none \
        --cache_size=$CACHE_SIZE \
        --cache_index_and_filter_blocks=1 \
        --pin_l0_filter_and_index_blocks_in_cache=1 \
        --use_data_block_hash_index=$HASH_INDEX \
        --block_restart_interval=$RI_SIZE

    # warm-up read (populate cache)
    ./db_bench \
        --seed=$SEED \
        --benchmarks=readrandom \
        --use_existing_db=1 \
        --use_existing_keys=1 \
        --db="$DB_PATH" \
        --num=$READS \
        --key_size=$KEY_SIZE \
        --value_size=$VALUE_SIZE \
        --cache_size=$CACHE_SIZE \
        --use_data_block_hash_index=$HASH_INDEX \
        --duration=$DURATION > /dev/null

    # actual read (recorded)
    ./db_bench \
        --seed=$SEED \
        --benchmarks=readrandom \
        --use_existing_db=1 \
        --use_existing_keys=1 \
        --db="$DB_PATH" \
        --num=$READS \
        --key_size=$KEY_SIZE \
        --value_size=$VALUE_SIZE \
        --cache_size=$CACHE_SIZE \
        --use_data_block_hash_index=$HASH_INDEX \
        --duration=$DURATION | tee tmp.out

    echo "" >> "$RESULTS_FILE"
    echo "### use_data_block_hash_index=$HASH_INDEX, restart_interval=$RI_SIZE, cache_size=$CACHE_SIZE, num_keys=$NUM_KV" >> "$RESULTS_FILE"
    grep "readrandom" tmp.out >> "$RESULTS_FILE"
}

# === Main loop ===
for RI_SIZE in "${RESTART_INTERVAL_SIZES[@]}"; do
    run_case 0 "$RI_SIZE"
    run_case 1 "$RI_SIZE"
done

echo ""
echo "Done! Results written to $RESULTS_FILE"
