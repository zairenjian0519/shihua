# shihua


### 编译
1， shihua/104_slave# rm -rf build/
2， shihua/104_slave# cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
3， shihua/104_slave# cmake --build build -j$(nproc)

4， 执行 shihua/104_slave# ./build/iec104_slave
