# MTK_kernel_device_modules @ bsp-rodin-v-oss

> rodin（Redmi Turbo 4 / POCO X7 Pro，MT6899 / 天玑 8400-Ultra）
> 把小米 6.6 ALPS vendor 驱动**前移**到 Linux 6.18 内核树，并以 `=y` 内建方式挂载——blob 装载态已归零。

**English TL;DR.** Fork of Xiaomi's OSS `MTK_kernel_device_modules`, branch `bsp-rodin-v-oss`.
Vendor modules forward-ported from the stock 6.6 ALPS kernel to a 6.18 (android17-6.18-lts
based) tree, with almost everything linked built-in (`=y`): of 569 unique stock module names,
538 are built-in, 31 closed blobs are dropped at load, and **zero blob modules are loaded**.
Must be paired with the matching 6.18 kernel tree at a pinned rebase point (§2). GPL-2.0,
no warranty. Custom kernel only — **not** GKI/module-ABI compatible.

---

## 1. 这是什么

| 项 | 值 |
|---|---|
| 设备 | rodin = Redmi Turbo 4（国行）/ POCO X7 Pro（海外） |
| SoC | MediaTek MT6899（Dimensity 8400-Ultra），Mali-G720 MC7（r49p1） |
| 上游 | MiCode/MTK_kernel_device_modules，分支 `bsp-rodin-v-oss`（小米 GPL 义务发布） |
| 基线 | `bc2dfc01` "rodin open source commit"（2025-02-17） |
| 本分支 | 基线之上 **+122 提交**（截至 2026-10-16，含本文档提交，持续更新） |
| 许可 | GPL-2.0（派生自小米 GPL OSS 发布） |

上游分支是小米按 GPL 义务公开的 **6.6 / Android V（ALPS）** 设备模块源码。本分支在其上做两件事：

1. **6.6 → 6.18 前移**：API/原型对齐（sched EEVDF 与延迟出队、maple tree、IOMMU core、
   usb xhci、SMI/IOMMU 家族、sysctl 注册收紧等）、显式 include 补齐、Kconfig/Makefile
   重接线、6.18 新机制（SELinux 新权限、io_uring 自权限等）回落 6.6 语义；
2. **内建化（=y）+ 终局收口**：vendor 模块一律 `=y` 编入 vmlinux，并按原厂装载序在内核内
   重放 probe；stock 分区的闭源 `.ko` 由内建跳过（镜像自带 `.modinfo` 名册）与丢载名单
   （`CONFIG_RODIN_BLOB_DROP_LIST`）处置——**不再装载任何 blob**。

## 2. 配对的另一半 ⚠️ 先看这节

**本仓库不能单独构建。** 它只是 vendor 模块半边，必须与 6.18 内核树在同一 rebase 点上一起编。

| 仓库 | 分支 | 说明 |
|---|---|---|
| `MTK_kernel_device_modules`（本仓库） | `bsp-rodin-v-oss` | vendor 模块 + 6.18 适配 + 内建接线 |
| [`Xiaomi_Rodin_Kernel_Enhance`](https://github.com/omajili-manbu/Xiaomi_Rodin_Kernel_Enhance)（内核仓） | `bsp-rodin-c-rebase` | GKI 6.18 基线 + 内建跳过/丢载名单/装载序重放 + 兼容层 |
| [`MTK_kernel_modules`](https://github.com/omajili-manbu/MTK_kernel_modules)（kmod 仓） | `bsp-rodin-v-oss` | 上游 kmod 半边（connectivity/gpu/mtkcam/_cus）+ mali r49p1 修复 |

本文快照（2026-10-16）：kernel `52a8e4b4498a` · devmod 本文档所在提交 · kmod `991e9cd48ac7`。
两侧错配 = 链接期符号缺口或运行期 `NULL` 解引用。

## 3. 内建化现状（2026-10-16）

- **569 个唯一模块名 = `=y` 内建 538 + 丢载 31 + blob 装载 0**；`modules.builtin` 1206。
- 丢载名单 31 名（`CONFIG_RODIN_BLOB_DROP_LIST`，内核仓 gki_defconfig）：
  MIUI 私有簇（millet 族/binder_gki/metis/mi_schedule/minet/miwill/miicmpfilter/
  mi_mem_epoll/sla/mi_mempool/perf_helper/lb/mpbe/crash_module/debug_ext/hardwareinfo/
  xiaomi_wifi_gpio/migt/scheduler_ext）+ 闭源调度性能件（task_turbo_v/fpsgo/game/
  eas_ext/cpuqos_ext/mtk_em，调度器耦合风险裁决丢弃）。功能影响 = 性能调度档位退化，
  日常功能不受影响（等价能力在上游用户态：uclamp/PSI/cgroup freezer 等，均已开启）。
- 触控三件（xiaomi_touch 框架/goodix brl/focaltech）、`mitee`（TEE/FF-A）、`bootmonitor`
  （启动黑盒记录）等原 blob 已按"实证等同原厂"纪律 `=y` 重建；兼容层代码休眠保留
  （blob 消费面已清空）。
- 最近构建 `Image.lz4` ≈ 44.3 MB（44,331,148 B；自用熔断线 57 MB，boot 分区 64 MB）。

## 4. 工作线选摘

- **装载序重放**：按 stock `modules.load`/`modules.dep` 底账，把 `=y` 单元的 probe
  重放到原厂时序位，消除内建化带来的 probe 相位反转；
- **触控栈 `=y` 重建**：多 donor 拼合 + 对出厂 blob 逐指令重建缺口
  （boost/scp_tp/工厂测试面），修复指纹亮屏、双击手势、palm 逐中断刷屏等；
- **`mitee` TEE/FF-A `=y`**：采用第三方 clean-room 重实现作骨架（见 §6 致谢），
  def/UND/机器码三维对账 + rodin 实况适配后落树；
- **`bootmonitor` `=y`**：blob 逆向等价重建（blackbox 分区布局/pmsg 头/事件表逐项还原）；
- **mali r49p1（kmod 仓）**：`get_unmapped_area` 6.1+ 分支空洞搜索死循环（持 mmap_lock
  楔死整机）根因修复并重写为逐空洞下行扫描；
- **6.18 语义回落**：SELinux `firmware_load`/`io_uring` 自权限、`initcall_blacklist`
  命名敏感项等"上游新机制 × stock 老策略"失配的回落修复。

## 5. 构建

内核树提供 `GNUmakefile`（固化工具链与接线变量），树根 `make` 即全量构建；vendor 两树
由内核树的 `Makefile.ext`/`Kconfig.ext` 接线拉入同一构建。产物取内核树
`arch/arm64/boot/Image.lz4`。环境 = WSL2 + Android clang（r596125 基）+ LLVM 22，
`dwarves/pahole` 随树提供。

## 6. 致谢（Acknowledgments）

- **MiCode（小米 GPL 开源发布）** — 本树与配对的 kmod 树基于
  [MiCode/MTK_kernel_device_modules](https://github.com/MiCode/MTK_kernel_device_modules)@`bsp-rodin-v-oss`
  与 [MiCode/MTK_kernel_modules](https://github.com/MiCode/MTK_kernel_modules)@`bsp-rodin-v-oss`
  （基线 `bc2dfc01` "rodin open source commit"）；`vendor_xiaomi_proprietary_touch-driver`
  等官方 per-domain 仓亦是触控重建的重要参照。没有这份按 GPL 义务公开的源码，本工程无从谈起。
- **YorokobiMaster** — `mitee`（TEE/FF-A 驱动）`=y` 内建采用其在 dash-w-oss 树
  `drivers/tee/mitee` 的 **clean-room 重实现**（修订 `26d0c19` "align dash driver with
  stock behavior"）作骨架，经三维对账与 rodin 实况适配后落树；该工作线现于
  [Content-Creation-Club](https://github.com/Content-Creation-Club) org 延续。

## 7. 许可与免责

- 内核/vendor 模块源码按 **GPL-2.0** 发布（派生自小米/联发科 GPL 发布面）；树内保留的
  MediaTek 专有组件仍按其原始条款。
- 这是**自定义内核**：与 GKI/模块 ABI 不兼容，不可混用官方 `*.ko`；刷写有变砖风险，
  无任何保修。
