#!/bin/bash
set -euo pipefail

RESULTS_FILE=result_w_mph_k8v100.txt
SEED=99382307420011
DB_BASE=/tmp/rocksdb-bench
KEY_SIZE=8              # bytes per key
VALUE_SIZE=100           # bytes per value
CACHE_SIZE=$((1<<30))   # 1 GB
READS=1000000           # 1 million
NUM_KV=2000000          # 2 million KV = 32 MB
# NUM_KV=100000
DURATION=60             # early stop

# Header
echo "# RocksDB point query benchmark (hash index on/off/mph)" > "$RESULTS_FILE"
echo "# $(date)" >> "$RESULTS_FILE"
echo "# key_size=$KEY_SIZE, value_size=$VALUE_SIZE, reads=$READS" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# Function to run tests per hash_index option
run_test() {
  local HASH_INDEX=$1
  case "$HASH_INDEX" in
    0)
      DB_PATH="${DB_BASE}/nohash"
      ;;
    1)
      DB_PATH="${DB_BASE}/hash"
      ;;
    2)
      DB_PATH="${DB_BASE}/mph"
      ;;
    *)
      echo "Unknown hash index option: $HASH_INDEX"
      exit 1
      ;;
  esac

  mkdir -p "$(dirname "$DB_PATH")"

  echo "=== Running with hash_index=$HASH_INDEX and db=$DB_PATH ==="

  # Build base command args
  local BASE_ARGS=(
    --seed=$SEED
    --benchmarks=filluniquerandom
    --db="${DB_PATH}"
    --num=$NUM_KV
    --key_size=$KEY_SIZE
    --value_size=$VALUE_SIZE
    --compression_type=none
    --cache_size=$CACHE_SIZE
    --cache_index_and_filter_blocks=1
    --disable_auto_compactions=1
  )
# --pin_l0_filter_and_index_blocks_in_cache=1
  # Append hash index flags accordingly
  if [ "$HASH_INDEX" -eq 2 ]; then
    BASE_ARGS+=(--use_data_block_mph_index=1)
  else
    BASE_ARGS+=(--use_data_block_hash_index=$HASH_INDEX)
  fi

  # Write phase
  echo "./db_bench ${BASE_ARGS[*]}"
  ./db_bench "${BASE_ARGS[@]}"

  # Read warm-up
  echo "./db_bench ${BASE_ARGS[*]} --benchmarks=readrandom --use_existing_db=1 --use_existing_keys=1 --num=$READS --duration=$DURATION"
  ./db_bench "${BASE_ARGS[@]}" \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --use_existing_keys=1 \
    --num=$READS \
    --duration=$DURATION

  # Read
  echo "./db_bench ${BASE_ARGS[*]} --benchmarks=readrandom --use_existing_db=1 --use_existing_keys=1 --num=$READS --duration=$DURATION"
  ./db_bench "${BASE_ARGS[@]}" \
    --benchmarks=readrandom \
    --use_existing_db=1 \
    --use_existing_keys=1 \
    --num=$READS \
    --duration=$DURATION | tee tmp.out

  # Append results to file
  echo "" >> "$RESULTS_FILE"
  if [ "$HASH_INDEX" -eq 2 ]; then
    echo "### cache_size=$CACHE_SIZE, use_data_block_mph_index=1, num_keys=$NUM_KV" >> "$RESULTS_FILE"
  else
    echo "### cache_size=$CACHE_SIZE, use_data_block_hash_index=$HASH_INDEX, num_keys=$NUM_KV" >> "$RESULTS_FILE"
  fi
  grep "readrandom" tmp.out >> "$RESULTS_FILE"
}

# Run all cases: mph hash, no hash, hash
run_test 2
run_test 0
run_test 1

echo ""
echo "Done! Results written to $RESULTS_FILE"




# # test
# # write
# ./db_bench   --seed=99382307420011   --benchmarks=filluniquerandom   --db=/tmp/rocksdb-bench/mph   --num=1000000   --key_size=8   --value_size=8   --compression_type=none   --cache_size=1073741824   --cache_index_and_filter_blocks=1   --disable_auto_compactions=1   --use_data_block_mph_index=1 --threads=1 --write_buffer_size=4194304

# write 2 for hash
./db_bench \
  --seed=99382307420011 \
  --benchmarks=filluniquerandom \
  --db=/tmp/rocksdb-bench/mph \
  --num=2000000 \
  --key_size=8 \
  --value_size=64 \
  --compression_type=none \
  --cache_size=1073741824 \
  --cache_index_and_filter_blocks=1 \
  --disable_auto_compactions=1 \
  --use_data_block_mph_index=1


# # read
# ./db_bench   --seed=99382307420011   --benchmarks=readrandom   --use_existing_db=1   --use_existing_keys=1   --num=1000000   --duration=60   --db=/tmp/rocksdb-bench/mph   --key_size=8   --value_size=8   --compression_type=none   --cache_size=1073741824   --cache_index_and_filter_blocks=1   --disable_auto_compactions=1   --use_data_block_mph_index=1 --threads=1

# read2
./db_bench   \
--seed=99382307420011   \
--benchmarks=readrandom  \
--use_existing_db=1   \
--use_existing_keys=1   \
--num=1000000   \
--duration=60   \
--db=/tmp/rocksdb-bench/mph   \
--key_size=8   \
--value_size=64  \
--compression_type=none   \
--cache_size=1073741824   \
--use_data_block_mph_index=1 \
--threads=1