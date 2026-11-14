# Theora 解码器健壮性与一致性测试台

本目录是针对 `libtheora` 解码器的独立 fuzz/回归测试框架，不依赖 autotools，
直接用仓库 `lib/` 下的 C 源码手工组织构建。

## 构建

```
./build.sh
```

- `bin/libtheoradec_san.a`：当前仓库解码器源码，`-fsanitize=address,undefined`
  + `-DHAVE_MEMORY_CONSTRAINT`（启用 16384 尺寸护栏）编译。
- `bin/fuzz_raw`：长度前缀包序列 fuzzer（ASan+UBSan+LSan）。
- `bin/fuzz_ogg`：完整 libogg 解复用 → libtheora 解码管线 fuzzer。
- `bin/fuzz_diag`：诊断器，输出拒绝阶段/包序号/流偏移/错误码。
- `bin/dump_san` / `bin/dump_ref`：被测库与系统参考库（libtheora 1.2.0）的
  确定性 YCbCr 帧转储，用于逐像素比对。
- `bin/gen_legal`：基于系统 libtheoraenc 的合法流生成器。

## 测试流程

```
python3 gen_malformed.py   # 2000 个畸形用例（raw + ogg 两种容器）
python3 gen_edge.py        # 132 个极端边界用例
./run_fuzz.sh              # 4300 次 sanitizer 执行，产出 report/fuzz_results.tsv
./run_diag.sh              # 4300 个用例的拒绝偏移映射，产出 report/rejection_map.tsv
./run_legal.sh             # 200 个合法流，与参考实现逐像素比对
```

## 语料分类

- `trunc`：按字节偏移逐点截断（含整包/半包）。
- `shift`：插入、删除、覆写、比特错位，破坏包与字节对齐。
- `quant`：setup 头量化/Huffman 表字段污染与渐进截断。
- `size`：id 头 FMBW/FMBH/PIC 字段注入 0、1、最大 24/16 位值（MSB 优先）。
- `soup`：多轮随机变异 + 丢包/重复包。
- `edge_*`：零/最大尺寸、纯随机巨包、仅头部等边界。

## 关键文件

- `report/fuzz_results.tsv`：每次执行的状态、耗时、崩溃类别、地址。
- `report/rejection_map.tsv`：每个用例被拒绝的数据包序号与绝对流偏移。
- `report/legal_results.tsv`：200 个合法流的逐像素比对结果。
- `report/minrepro/`：最小反例（单字节变异）。
