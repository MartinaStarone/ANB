#!/bin/bash
/usr/lib/llvm-21/bin/llvm-link /home/martina/CLionProjects/ASPIS2/autonomous_bench_pico/build/multiple_functions.ll -o out.ll
/usr/lib/llvm-21/bin/opt --passes=lower-switch out.ll -o out.ll
/usr/lib/llvm-21/bin/opt --passes=simplifycfg out.ll -o out.ll
gdb -batch -ex "run" -ex "bt" --args /usr/lib/llvm-21/bin/opt -load-pass-plugin=/home/martina/CLionProjects/ASPIS2/build/passes/Utils/libUtils.so -load-pass-plugin=/home/martina/CLionProjects/ASPIS2/build/passes/libANB.so --passes=anb-encode out.ll -o out2.ll
