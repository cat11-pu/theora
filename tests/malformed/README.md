# 畸形 Theora 帧回归测试

本目录验证三类解码器输入：

- 截断帧：包在帧头、编码标志、模式、运动矢量或 DCT token 区域被截断。
- 非法量化矩阵：`NBMS > 384`、量化范围首索引 `BMI >= NBMS`、setup 头截断。
- 损坏的运动矢量区：合法关键帧之后的 INTER 包在模式/MV 区被截断。

所有样本都使用 `TPK1` 包容器，避免 Ogg CRC 和 page lacing 掩盖 Theora
载荷本身的错误：

```
magic[4] = "TPK1"
u32 packet_count
repeated: u32 packet_size, packet bytes
```

`valid.ogv` 是同一合法码流的 Ogg 形态，供人工复现；自动测试使用
`valid.tpk`。

## 构建与运行

在仓库根目录：

```
./configure --disable-encode --disable-examples
make
make check             # 包含 malformed_test
```

单独运行诊断驱动：

```
gcc -std=c99 -Iinclude tests/malformed/decode_tpk.c \
  lib/.libs/libtheoradec.a -logg -o decode_tpk
./decode_tpk tests/malformed/samples/bad_quant_nbms.tpk
```

输出中的 `offset` 是包内首个错误字节的位置。新 API
`th_decode_headerin_ex()` / `th_decode_packetin_ex()` 直接返回该偏移；
`TH_DECCTL_GET_ERROR_OFFSET` 可查询最近一次数据包错误偏移。

## 样本

| 文件 | 变异 | 预期 |
| --- | --- | --- |
| `valid.tpk` | 无 | 全部帧成功解码 |
| `truncated_frame.tpk` | 关键帧截到 100 字节 | `TH_EBADPACKET`, offset=100 |
| `bad_quant_nbms.tpk` | setup 中 NBMS=511 | `TH_EBADHEADER`, offset=185 |
| `bad_quant_bmi.tpk` | 首个量化范围 BMI=15，而 NBMS=12 | `TH_EBADHEADER`, offset=957 |
| `bad_quant_truncated.tpk` | setup 截到 200 字节 | `TH_EBADHEADER`, offset=200 |
| `bad_mv_truncated.tpk` | INTER 包截到 16 字节 | `TH_EBADPACKET`, offset=16 |

`gen_samples.py` 从 `samples/valid.tpk` 重新生成其余样本。

## 变异统计

`run_mutations.py` 对全部数据帧做逐字节截断，并增加 2000 个随机帧变异：

```
python3 tests/malformed/run_mutations.py \
  ./decode_tpk tests/malformed/samples/valid.tpk mutation_results.tsv
```

2026-11-18 在 ASan+UBSan+LSan 下的结果：

- 用例 4217：截断 2217 + 随机 2000。
- 拒绝 3553，接受 664（接受的截断仅为 0 字节丢帧和原长度包）。
- 崩溃 0、ASan/UBSan 报告 0、超时 0、泄漏 0。
- 另做 setup 随机变异 300000 次、数据帧随机变异 200000 次，崩溃 0。

详细说明见 `report/REPORT.md`。
