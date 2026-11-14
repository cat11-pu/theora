# Theora 解码器 Fuzz 与一致性测试报告

日期：2026-11-15
被测对象：仓库 `lib/` 下的 libtheora 解码器（提交 `28fd5ec`）
工具链：gcc 15.2.0，AddressSanitizer + UndefinedBehaviorSanitizer
（`detect_leaks=1`、`halt_on_error=1`、`fno-sanitize-recover`）
参考实现：系统 libtheora 1.2.0（`libtheoradec.so.2.1.1`）

## 1. 结论摘要

| 项目 | 目标 | 实测 |
| --- | --- | --- |
| 畸形码流执行次数 | 2000 个用例 | 2150 用例 × 2 容器 = **4300 次执行** |
| 崩溃（ASan/UBSan） | 0 | **0** |
| 越界读写 | 0 | **0** |
| 内存泄漏（LSan） | 0 | **0** |
| 挂起/超时（>10s） | 0 | 修复前 4 例，修复后 **0** |
| 合法流逐像素比对 | 200 个 | **200/200 PIXEL_MATCH** |
| 失败可定位（带偏移） | 是 | 全部拒绝记录含包序号+流偏移+错误码 |

修复前的 4 个挂起用例（`case_0282/0286/1543/1550`）在原始库上 100% 复现
无限循环；修复后全部以 `TH_EBADHEADER` 立即拒绝，单例耗时降到毫秒级。

## 2. 发现的缺陷与修复

### 缺陷 A（实际可触发，DoS 级）：单边超大帧尺寸导致解码无限耗时

- 位置：`lib/decinfo.c:91`，`oc_info_unpack()` 的 `HAVE_MEMORY_CONSTRAINT` 护栏。
- 根因：护栏使用逻辑与 `frame_width>=MAX && frame_height>=MAX`，只有两个维度
  同时超限时才拒绝。Theora 的 FMBW/FMBH 是 16 位字段、解码时左移 4 位，
  最大帧宽可达 1,048,560。畸形流把单个维度改成超大值（另一维度保持 64），
  即可绕过护栏，解码器随后按超大帧分配 fragment 并在
  `oc_dec_frags_recon_mcu_plane` / `oc_idct8x8_c` 中执行天量工作，表现为挂起。
- 复现：id 头第 10 字节（FMBW 高 8 位，MSB 优先）由 `0x00` 改为 `0xFF`，
  帧宽由 96 变为 1,044,576。
- 修复：逻辑与改为逻辑或，任一维度达到 16384 即返回 `TH_EBADHEADER`。

### 缺陷 B（理论可达，纵深防御）：DCT token 零游程零 EOB 死循环

- 位置：`lib/decode.c:1539` 起，`oc_dec_frags_recon_mcu_plane()` 的
  `for(zzi=0;zzi<64;)` 系数重建循环。
- 根因：循环前进依赖 `zzi += rlen` 与 `zzi += !eob`。若畸形 token 同时给出
  `rlen==0` 与 `eob==0`（例如 ZRL token 的额外比特全为 0），`zzi` 完全不
  前进，下一轮在同一系数位置重复解码，永不退出。
- 修复：函数签名由 `void` 改为 `int`，在 `zzi` 一轮结束后若与 `last_zzi`
  相同且无 EOB 挂起，返回 `TH_EBADPACKET`；`th_decode_packetin` 检查返回值
  并向上传播。合法值 token 靠 `zzi+=!eob` 前进、EOB token 靠下一轮顶部的
  `eob_runs` 退出，均不受影响。
- 可达性说明：对全部 4300 个用例插桩，该守卫未被随机变异命中——EOF 填充
  通常先终止 bitpack 读取。此修复保留为防御性护栏，确保即使出现可构造的
  恶意 token 序列也不会挂死。

## 3. 最小反例

目录 `report/minrepro/`：

- `oversize_width_min.raw` / `.ogv`：单字节变异最小反例。
  - 基础：合法 96×64 流（8 个 ogg 包，31494 字节）。
  - 变异：id 头字节偏移 10，`0x00 → 0xFF`（FMBW 高字节）。
  - 效果：帧宽 96 → 1,044,576。
  - 原始库：`timeout 4 fuzz_raw` 返回 124（挂起）。
  - 修复库：立即返回，诊断为
    `stage=header packet=0 stream_offset=0 error=TH_EBADHEADER(-20)`。
- `oversize_header_only.raw` / `.ogv`：仅保留三个头部包（3312 字节）的精简
  形态，用于快速验证头部阶段即在分配前拒绝。

复现命令：

```
timeout 4 ./bin/fuzz_raw_orig report/minrepro/oversize_width_min.raw   # 124 挂起
timeout 4 ./bin/fuzz_raw      report/minrepro/oversize_width_min.raw   # 0 干净
./bin/fuzz_diag raw report/minrepro/oversize_width_min.raw
```

## 4. 畸形码流拒绝分布（4300 次，两个容器合计）

| 错误码 | 次数 | 含义 |
| --- | --- | --- |
| TH_EBADHEADER (-20) | 2190 | 头部/量化表/尺寸字段非法 |
| TH_ENOTFORMAT (-21) | 356 | 非 Theora 标识 |
| TH_EBADPACKET (-24) | 45 | 数据包格式非法 |
| TH_EVERSION (-22) | 18 | 版本号不受支持 |
| TH_EIMPL (-23) | 11 | 算术溢出/不可实现参数 |
| ALLOC_FAIL | 10 | 内部表损坏，分配阶段优雅拒绝（无泄漏） |
| ACCEPTED | 1670 | 解码器安全消费，无任何 sanitizer 告警 |

按阶段：header 2564、frame 56、alloc 10、none（完整消费）1670。
10 个帧尺寸 ≥16384 的用例 100% 在头部阶段被 `TH_EBADHEADER` 拦截。

## 5. 合法流一致性（200 个）

参数矩阵覆盖：
- 尺寸：16×16 至 320×240（10×10 组合，非 16 对齐由编码器补齐）。
- 帧率：30/1、25/1、24000/1001、15/2。
- 像素格式：4:2:0、4:2:2、4:4:4。
- 质量 0–63、帧数 3–14、关键帧间隔 16–112、确定性随机种子。

每个流分别用被测库与系统参考库解码，按 `THDUMP01` 格式转储全部 YCbCr
平面（含 width/height/stride），`cmp` 字节级比较：**200/200 完全一致，
零差异字节**。结果见 `report/legal_results.tsv`。

## 6. 性能与资源

- 4300 次执行总耗时约 113 秒，单例 p50=15ms、p99=23ms、max=39ms
  （修复前挂起用例 >10s 被强杀）。
- LSan 在合法流与畸形流上均无泄漏报告；ALLOC_FAIL 路径经单独验证
  无残留分配。

## 7. 对库源码的改动

仅两个文件，合计 22 行有效改动：
- `lib/decinfo.c`：尺寸护栏 `&&` → `||`。
- `lib/decode.c`：`oc_dec_frags_recon_mcu_plane` 增加前进性检查并改为
  返回错误码，调用点传播 `TH_EBADPACKET`。

测试脚手架（`fuzz/` 目录）与库源码分离，未修改任何编码器或公共 API。
