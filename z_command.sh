# Here are summary of the command for executing the pogram

# == enter docker === 
# mount the repo to docker, reflect local changes instantly
docker run -it --rm \
  -v $(pwd):/rocksdb \
  -w /rocksdb \
  rocksdb-dev \
  bash
# === test === #
make -j2 data_block_perfect_mph_index_test # compile
./data_block_perfect_mph_index_test # run executable
# === compile db_bench === #
make clean
make -j2 db_bench DEBUG_LEVEL=0 # compile not in debug mode
make -j db_bench DEBUG_LEVEL=0 # compile not in debug mode
make -j db_bench DEBUG_LEVEL=0 OPT="-O3 -DNDEBUG -march=native"

# === perf profile ===
make -j db_bench DEBUG_LEVEL=1 OPT_LEVEL=0 # build with debug
perf record -e cycles:u -g <command>
perf report --stdio > report.txt # output perf result to txt

