#!/bin/bash

# 删除旧的可执行文件和临时文件
echo "清理旧文件..."
rm -f sr *.o

# 检查是否启用调试模式
echo "正常编译..."
gcc -Wall -ansi -pedantic -o sr emulator.c sr.c


# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo "编译失败，请检查代码中的错误！"
    exit 1
fi

# 如果编译成功，运行生成的可执行文件
echo "编译成功，正在运行程序..."
./sr