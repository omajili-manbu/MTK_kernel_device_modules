# MTK_kernel_device_modules @ bsp-rodin-v-oss

> rodin（Redmi Turbo 4 / POCO X7 Pro，MT6899 / 天玑 8400-Ultra）
> 把小米 6.6 ALPS vendor 驱动**前移**到 6.18 内核树，并以 `=y` 内建方式挂载。

**English TL;DR.** Fork of Xiaomi's OSS `MTK_kernel_device_modules`, branch
`bsp-rodin-v-oss`. Vendor modules forward-ported from the stock 6.6 ALPS kernel to a
6.18 tree and linked built-in (`=y`), plus ABI shims for the stock `vendorboot`
blobs. Must be paired with the matching 6.18 kernel tree at the pinned rebase point
(§2). GPL-2.0, no warranty. Custom kernel only — **not** GKI/module-ABI compatible.

---

## 1. 这是什么

| 项 | 值 |
|---|---|
| 设备 | rodin = Redmi Turbo 4（国行）/ POCO X7 Pro（海外） |
| SoC | MediaTek MT6899（Dimensity 8400-Ultra），Mali-G720 MC7 |
| 上游 | MiCode/MTK_kernel_device_modules，分支 `bsp-rodin-v-oss` |
| 基线 | `bc2dfc01` "rodin open source commit"（2025-02-17，Yue Kang） |
| 本分支 | 基线之上 **+81 提交**（截至 2026-10-08，持续更新） |
| 许可 | GPL-2.0（派生自小米 GPL OSS 发布） |

上游 `bsp-rodin-v-oss` 是小米按 GPL 义务公开的 **6.6 / Android V（ALPS）** 设备模块源码。
本分支在其上做两件事：

1. **6.6 → 6.18 前移**：API/原型对齐、头文件瘦身后的显式 include 补齐、`__weak`
   空实现、Kconfig/Makefile 重接线；
2. **内建化 + 互操作**：把原 `*.ko` 改为 `=y` 编入 vmlinux；并补齐 stock `vendorboot`
   闭源模块（596 个 `.ko`）所消费的导出符号面，使原厂 blobs 仍可装载。

## 2. 配对的另一半 ⚠️ 先看这节

**本仓库不能单独构建。** 它只是 vendor 模块半边，必须与 6.18 内核树在**同一 rebase 点**上一起编。

| 仓库 | 分支 @ commit | 说明 |
|---|---|---|
| `MTK_kernel_device_modules`（本仓库） | `bsp-rodin-v-oss` @ `8d4c7a3` | vendor 模块 + ABI shim |
| `Xiaomi_Rodin_Kernel_Enhance`（内核仓） | `bsp-rodin-c-rebase` @ `<SHA>` | GKI 基线 + 内建化兼容适配 |

两侧错配 = 链接期符号缺口或运行期 `NULL` deref。**改任一侧都必须同步更新上表。**

## 3. 现状

迭代编号 `#NNN`（提交主题尾部），当前 **#230**，提交信息自述已进行到**第 82 轮**。
覆盖子系统：

- 平台/电源：`clk`、`mtk-pd-chk`、`pinctrl`、`mminfra`、`vcp`
- 存储：`ufs-mediatek` + Xiaomi UFS ABI shim（`drivers/ufs/vendor/ufs-xiaomi.c`）
- 显示/GPU：`gpu/drm/mediatek`
- IOMMU/USB：`iommu`（含 `rodin_io_pgtable_arm_66.h` 值面还原）、`usb/mtu3`
- 输入：`focaltech`、`goodix`、`xiaomi_touch` 框架
- TEE/FFA：`mitee`、`tmem` 绑定面

### 已验证的关键修复

- **b52** `pd-chk` 探测顺序：6.18 下 clk provider 以 `vseq_device_initcall` 注册、晚于
  `pd-chk` 的 `vseq_subsys_initcall`，导致 in-probe 遍历空 provider 表 —— **启动省 ~3.3s**。
- **#149** `ffa_device_match()` 的 `tmem` 通配匹配抢绑 mitee 分区，修 `[NULL+8]` Oops
  （恢复纯 UUID 匹配）。
- **#150** 补齐 UFS ABI（`get_ufs_xiaomi` / `ufshcd_query_descriptor_retry_xm` /
  `ufshcd_read_desc_param_sel`），修 `mi_memory.ko` 装载失败的 `Attempted to kill init` panic。

## 4. 构建

依赖内核树以 make 方式构建
