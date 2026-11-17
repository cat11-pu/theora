# 基准与逐像素对照工具

## 测试流

- `t_<res>.ogv`：三档主测分辨率 640x360 / 1280x720 / 1920x1080（4:2:0，90 帧）。
- `x_*.ogv`：不同内容、GOP、质量与环路滤波强度的覆盖流。

## 工具

- `dec_bench.c`：端到端逐帧解码计时，可选转储每帧原始 YUV。
- `dec_bench_min.c`：同进程多遍解码，报告最小/中位/最大每帧时间
  （最小值对调度噪声最稳健）。
- `recon_bench.c`：三个运动补偿重建核的隔离微基准。
- `lf_bench.c`：环路滤波的隔离微基准。
- `mc_lf_bench.c`：重建（含 iDCT）+ 环路滤波的合并微基准。
- `diff_dump.sh`：两个解码器对同一流逐帧转储并逐字节比对。
- `e2e_final.sh`：base/opt/lto 三版本端到端对照。

## 典型复现

```sh
# 逐像素对照（任一测试流）
./diff_dump.sh ./dec_bench_base ./dec_bench_lto t_1280x720.ogv

# 端到端三档耗时
for r in 640x360 1280x720 1920x1080; do
  taskset -c 10 ./min_base t_$r.ogv 25
  taskset -c 10 ./min_lto  t_$r.ogv 25
done
```

数据汇总见 `PERFORMANCE.md`。
