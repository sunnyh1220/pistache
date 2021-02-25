
# Lustre Quota restapi

- Lustre c api
    https://github.com/whamcloud/lustre/blob/master/lustre/utils/liblustreapi.c

```bash
# clone
git clone https://github.com/sunnyh1220/pistache.git
cd pistache
git submodule update --init

# build
mkdir build
cd build    # ${workspaceFolder}/build

# Note
# check gcc version

gcc -v && g++ -v
# change gcc version refer ： https://www.cnblogs.com/jixiaohua/p/11732225.html
scl enable devtoolset-9 bash  # change gcc version to 9.x

cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DPISTACHE_BUILD_EXAMPLES=true ../
make -j
make install

# run
cd examples     # ${workspaceFolder}/build/examples
nohup ./run_rest_server > logs.file 2>&1 &  # ${workspaceFolder}/build/examples/run_rest_server

```