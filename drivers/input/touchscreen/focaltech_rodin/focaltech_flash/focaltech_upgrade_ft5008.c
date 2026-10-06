// SPDX-License-Identifier: GPL-2.0
/*
 * focaltech_upgrade_ft5008.c — 小米 rodin (FT5008 / ctype 0x90,0x92) 固件升级件重建
 *
 * 来源：自机 blob focaltech_touch_rodin.ko（小米官方 GPL 发布模块）逐指令反汇编重建。
 *       反汇编稿：tools/_b567_touch/blob/focaltech_touch_rodin.disr
 *       证据文档：tools/_b570_a70/upgrade_recon.md
 *       括号内 (blob 0xADDR, SIZE) = blob .text 地址/字节数。
 *
 * 逐函数对应（blob 符号 -> 本文件）：
 *   fts_ft5008_upgrade            (0x2b534,2680) [LOCAL]  ← 主体；内含被内联的静态件：
 *      ├ fts_ft5008_flash_write_buf  (无独立符号，__func__ 串 ".rodata.str1.1+0x120c" 实证)
 *      ├ fts_spidebug_recovery       (无独立符号，与 donor q200 fts_spidebug_recovery 逐常量一致)
 *      └ fts_ft5008_crc16_calc_host  (无独立符号，0x8408=AL2_FCS_COEF 实证)
 *   fts_read_lockdown_info_proc   (0x2b488,168)  [GLOBAL] ← focaltech_core.c fts_get_lockdown_information 调用
 *   upgrade_func_ft5008           (.data 0x2758,128) [GLOBAL] ← blob 仅 1 处重定位：+0x58 -> fts_ft5008_upgrade
 *   upgrade_func_list             (.data 0x2850,8)  = { &upgrade_func_ft5008 }（在 focaltech_flash.c，本文件不含）
 *
 * 骨架基准：MiCode popsicle-w-oss q200/focaltech_3383 的 focaltech_upgrade_ft3383.c
 *   （donor 与活树该文件 md5 相同：b983fc72c404eedc2f0467b77645d5ec）。
 *   同构度判定：fts_ft5008_upgrade 与 donor fts_ft5572_upgrade 逐段同构（同调用序列、
 *   同常量、同错误分支），差异仅 (a) IC 改名/对象值 (b) donor 的 fts_read_lockdown_info
 *   被删除、_proc 版被简化为 8B 直读直拷（blob 实证）(c) 纯代码生成差异。详见 recon §4/§5/§6。
 *
 * 纪律（b570）：
 *   - blob 无 fts_read_lockdown_info 符号（donor 有）：本文件不提供该函数；活树对它的
 *     唯一引用在 focaltech_ex_fun.c:1542（位于整段被注释掉的 fts_lockdown_show 内），
 *     无活动调用点，不破坏链接。
 *   - 日志一律用骨架宏 FTS_INFO/FTS_ERROR/FTS_DEBUG（blob 的宏形态与骨架不同，属模块级
 *     既有差异，见 recon §7.1，本文件不做私改）。
 *   - 不改活树任何文件；接线清单见 recon §8。
 */

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "../focaltech_flash.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_DELAY_ERASE_PAGE            6       /* erase delay = 6 * (len/256) ms，blob 0x2b5c8 实证 */
#define FTS_SIZE_PAGE                   256
#define FTS_FLASH_PACKET_SIZE           1024    /* max 2048；blob memset 0x406=1024+6（含命令头） */

/************************************************************************
* Name: fts_ft5008_crc16_calc_host
* Brief: host 侧 CRC16（FCS 多项式 AL2_FCS_COEF=0x8408）
* Input:  pbuf/length
* Output: ecc（16bit）
* Return: ecc
* 证据：blob 0x2b8d8..0x2b9fc（内联），w10=0x8408，每 2 字节 16 轮位循环，csel 实现条件异或
***********************************************************************/
static u16 fts_ft5008_crc16_calc_host(u8 *pbuf, u32 length)
{
    u16 ecc = 0;
    u32 i = 0;
    u32 j = 0;

    for (i = 0; i < length; i += 2) {
        ecc ^= ((pbuf[i] << 8) | (pbuf[i + 1]));
        for (j = 0; j < 16; j++) {
            if (ecc & 0x01)
                ecc = (u16)((ecc >> 1) ^ AL2_FCS_COEF);
            else
                ecc >>= 1;
        }
    }

    return ecc;
}

/************************************************************************
 * Name: fts_ft5008_flash_write_buf
 * Brief: 把 buf 写入 flash 地址 saddr（按 1024B 分包 + 每包状态回读）
 * Input: saddr - flash 起始地址；buf/len - 数据；delay - 每包后延时(ms)
 * Output:
 * Return: 成功返回 host ecc，失败返回错误码
 * 证据（blob 内联体 0x2b5d8..0x2ba00）：
 *   memset(packet_buf,0,0x406) @0x2b5ec；FTS_MAX_LEN_FILE 判 len>0x40000 @0x2b608；
 *   packet_number=len>>10 / remainder=len&0x3ff / +1 @0x2b72c..0x2b73c；
 *   bus_type 读 fts_data+0xad8 比对 BUS_TYPE_SPI_V2(3) @0x2b788：
 *     SPI_V2 支路先发 0xAB+addr(4B)，再发 cmdlen=1 的 0xBF 包；
 *     非 SPI_V2 支路直接发 6 字节头 0xBF+addr(3B)+len(2B)；
 *   mdelay(delay) 展开为 delay 个 udelay(1000) @0x2b828（delay=8，源自调用点 (1024/256)*2）；
 *   wr_ok=(u16)(0x1000 + addr/packet_len) @0x2b888；重试 FTS_RETRIES_WRITE=100 @0x2b88c；
 *   每轮 msleep 1ms（mdelay(FTS_RETRIES_DELAY_WRITE) @0x2b8c0）；
 *   收尾返回 fts_ft5008_crc16_calc_host(buf,len)（内联体 @0x2b8d8）。
 ***********************************************************************/
static int fts_ft5008_flash_write_buf(u32 saddr, u8 *buf, u32 len, u32 delay)
{
    int ret = 0;
    u32 i = 0;
    u32 j = 0;
    u32 packet_number = 0;
    u32 packet_len = 0;
    u32 addr = 0;
    u32 offset = 0;
    u32 remainder = 0;
    u32 cmdlen = 0;
    u8 packet_buf[FTS_FLASH_PACKET_SIZE + FTS_CMD_WRITE_LEN] = { 0 };
    int ecc_in_host = 0;
    u8 cmd = 0;
    u8 val[FTS_CMD_FLASH_STATUS_LEN] = { 0 };
    u16 read_status = 0;
    u16 wr_ok = 0;
    u32 flash_packet_size = FTS_FLASH_PACKET_SIZE;

    FTS_INFO("**********write data to flash**********");
    if (!buf || !len || (len > FTS_MAX_LEN_FILE)) {
        FTS_ERROR("buf/len(%d) is invalid", len);
        return -EINVAL;
    }

    FTS_INFO("data buf start addr=0x%x, len=0x%x", saddr, len);
    packet_number = len / flash_packet_size;
    remainder = len % flash_packet_size;
    if (remainder > 0)
        packet_number++;
    packet_len = flash_packet_size;
    FTS_INFO("write data, num:%d remainder:%d", packet_number, remainder);

    for (i = 0; i < packet_number; i++) {
        offset = i * flash_packet_size;
        addr = saddr + offset;

        /* last packet */
        if ((i == (packet_number - 1)) && remainder)
            packet_len = remainder;

        if (fts_data->bus_type == BUS_TYPE_SPI_V2) {
            packet_buf[0] = FTS_CMD_SET_WFLASH_ADDR;
            packet_buf[1] = BYTE_OFF_16(addr);
            packet_buf[2] = BYTE_OFF_8(addr);
            packet_buf[3] = BYTE_OFF_0(addr);
            ret = fts_write(packet_buf, FTS_LEN_SET_ADDR);
            if (ret < 0) {
                FTS_ERROR("set flash address fail");
                return ret;
            }

            packet_buf[0] = FTS_CMD_WRITE;
            cmdlen = 1;
        } else {
            packet_buf[0] = FTS_CMD_WRITE;
            packet_buf[1] = BYTE_OFF_16(addr);
            packet_buf[2] = BYTE_OFF_8(addr);
            packet_buf[3] = BYTE_OFF_0(addr);
            packet_buf[4] = BYTE_OFF_8(packet_len);
            packet_buf[5] = BYTE_OFF_0(packet_len);
            cmdlen = 6;
        }

        for (j = 0; j < packet_len; j++) {
            packet_buf[cmdlen + j] = buf[offset + j];
        }

        ret = fts_write(packet_buf, packet_len + cmdlen);
        if (ret < 0) {
            FTS_ERROR("app write fail");
            return ret;
        }
        mdelay(delay);

        /* read status */
        wr_ok = FTS_CMD_FLASH_STATUS_WRITE_OK + addr / packet_len;
        for (j = 0; j < FTS_RETRIES_WRITE; j++) {
            cmd = FTS_CMD_FLASH_STATUS;
            ret = fts_read(&cmd, 1, val, FTS_CMD_FLASH_STATUS_LEN);
            read_status = (((u16)val[0]) << 8) + val[1];
            /*  FTS_INFO("%x %x", wr_ok, read_status); */
            if (wr_ok == read_status) {
                break;
            }
            mdelay(FTS_RETRIES_DELAY_WRITE);
        }
    }

    ecc_in_host = (int)fts_ft5008_crc16_calc_host(buf, len);
    return ecc_in_host;
}

/************************************************************************
* Name: fts_spidebug_recovery
* Brief: SPI 调试口恢复序列（升级失败后的兜底），最多 3 轮
* 证据（blob 内联体 0x2bad0..0x2be58）：
*   cmd_0/1/.../cmd_12 全部 11B/3B/7B 常量与 donor q200 fts_spidebug_recovery 逐字节一致，
*   其中 cmd_3={70,07,f8,81,c6,00,00,00,80,00,00}（0x80 在下标 8，blob `mov w8,#0x8000;
*   stur w8,[x9,#0x87]` 实证；活树/donor 同值），cmd_1 的 0x20 下标 7（blob `stur w8,[sp,#0x2f]`）。
*   13 次 fts_spi_transfer_direct + mdelay(8) + cmd_12(7B) + {0x71} 读 5B；
*   判 val[1]==0x50 && val[2..4]==0 则 break，否则 mdelay(10) 重试；循环 3 次（blob w19=3）；
*   收尾 fts_reset_for_upgrade(); mdelay(100)（blob 0x2be3c 用 100 次 udelay(1000) 循环）。
***********************************************************************/
static void fts_spidebug_recovery(void)
{
    uint8_t cmd_0[] = {0x70, 0x55, 0xaa};
    uint8_t cmd_1[] = {0x70, 0x07, 0xf8, 0x81, 0xca, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00};
    uint8_t cmd_2[] = {0x70, 0x07, 0xf8, 0x81, 0xc7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t cmd_3[] = {0x70, 0x07, 0xf8, 0x81, 0xc6, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00};
    uint8_t cmd_4[] = {0x70, 0x07, 0xf8, 0x81, 0xc2, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};
    uint8_t cmd_5[] = {0x70, 0x07, 0xf8, 0x81, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t cmd_6[] = {0x70, 0x07, 0xf8, 0x81, 0xc0, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00};
    uint8_t cmd_7[] = {0x70, 0x07, 0xf8, 0x81, 0xc1, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00};
    uint8_t cmd_8[] = {0x70, 0x07, 0xf8, 0x81, 0xc8, 0x00, 0x00, 0xa5, 0x00, 0x00, 0x00};
    uint8_t cmd_9[] = {0x70, 0x07, 0xf8, 0x81, 0xc8, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00};
    uint8_t cmd_10[] = {0x70, 0x07, 0xf8, 0x81, 0xc8, 0x00, 0x00, 0x6a, 0x00, 0x00, 0x00};
    uint8_t cmd_11[] = {0x70, 0x07, 0xf8, 0x81, 0xc4, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00};
    uint8_t cmd_12[] = {0x70, 0x06, 0xf9, 0x81, 0xc4, 0x00, 0x00};
    uint8_t cmd = 0x71;
    uint8_t val[5] = {0};
    uint32_t i = 0;

    for (i = 0; i < 3; i++) {
        fts_reset_for_upgrade();
        mdelay(2);

        fts_spi_transfer_direct(cmd_0, sizeof(cmd_0), NULL, 0);
        fts_spi_transfer_direct(cmd_1, sizeof(cmd_1), NULL, 0);
        fts_spi_transfer_direct(cmd_2, sizeof(cmd_2), NULL, 0);
        fts_spi_transfer_direct(cmd_3, sizeof(cmd_3), NULL, 0);
        fts_spi_transfer_direct(cmd_4, sizeof(cmd_4), NULL, 0);
        fts_spi_transfer_direct(cmd_5, sizeof(cmd_5), NULL, 0);
        fts_spi_transfer_direct(cmd_6, sizeof(cmd_6), NULL, 0);
        fts_spi_transfer_direct(cmd_7, sizeof(cmd_7), NULL, 0);
        fts_spi_transfer_direct(cmd_8, sizeof(cmd_8), NULL, 0);
        fts_spi_transfer_direct(cmd_9, sizeof(cmd_9), NULL, 0);
        fts_spi_transfer_direct(cmd_10, sizeof(cmd_10), NULL, 0);
        fts_spi_transfer_direct(cmd_11, sizeof(cmd_11), NULL, 0);
        mdelay(8);
        fts_spi_transfer_direct(cmd_12, sizeof(cmd_12), NULL, 0);
        fts_spi_transfer_direct(&cmd, sizeof(cmd), val, sizeof(val));

        if (val[1] == 0x50 && val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) {
            break;
        }
        mdelay(10);
    }
    fts_reset_for_upgrade();
    mdelay(100);
}

/************************************************************************
* Name: fts_ft5008_upgrade
* Brief: FT5008 固件升级主流程（状态机：参数校验 -> 进 boot -> 长度/模式命令 -> 擦除 ->
*        分包写入 -> ECC 比对 -> 复位；任一步失败走 fw_reset = spidebug 恢复 + -EIO）
* Input:  buf - 固件镜像; len - 长度
* Output:
* Return: 成功 0（含 msleep(200)）；参数非法 -EINVAL；流程失败 -EIO
* 证据（blob 0x2b534..0x2be90）：
*   校验 !buf || len<FTS_MIN_LEN(0x120) @0x2b56c/0x2b570，错误串 "buffer/len(%x) is invalid"；
*   fts_fwupg_enter_into_boot() @0x2b57c，失败 "enter into pramboot/bootloader fail,ret=%d"；
*   cmd=0x7A(FTS_CMD_APP_DATA_LEN_INCELL)+len(3B) @0x2b58c..0x2b5a4，失败 "data len cmd write fail"；
*   cmd=0x09,0x0B(FLASH_MODE_UPGRADE_VALUE) @0x2b5b0(`strh #0xb09`)，失败 "upgrade mode(09) cmd write fail"；
*   delay=6*(len/256) @0x2b5c8，fts_fwupg_erase(delay) @0x2b5d0，失败 "erase cmd write fail"；
*   start_addr = upgrade_func_ft5008.appoff @0x2b5d8（blob 重定位 upgrade_func_ft5008+0x10 实证）；
*   delay=(1024/256)*2=8，fts_ft5008_flash_write_buf(start_addr,buf,len,delay) 内联，失败 "flash write fail"；
*   ecc_in_tp=fts_fwupg_ecc_cal(start_addr,len) @0x2ba08，失败 "ecc read fail"；
*   FTS_INFO("ecc in tp:%x, host:%x") @0x2bf28；不等 -> "ecc check fail"；
*   FTS_INFO("upgrade success, reset to normal boot")；fts_fwupg_reset_in_boot() @0x2ba38，
*   失败 "reset to normal boot fail"；msleep(200) @0x2ba48（mov w0,#0xc8）；return 0；
*   fw_reset @0x2bac0: FTS_INFO("upgrade fail, reset to normal boot") + fts_spidebug_recovery() + return -EIO(0x2be58)。
***********************************************************************/
static int fts_ft5008_upgrade(u8 *buf, u32 len)
{
    int ret = 0;
    u32 start_addr = 0;
    u8 cmd[4] = { 0 };
    u32 delay = 0;
    int ecc_in_host = 0;
    int ecc_in_tp = 0;

    if ((NULL == buf) || (len < FTS_MIN_LEN)) {
        FTS_ERROR("buffer/len(%x) is invalid", len);
        return -EINVAL;
    }

    /* enter into upgrade environment */
    ret = fts_fwupg_enter_into_boot();
    if (ret < 0) {
        FTS_ERROR("enter into pramboot/bootloader fail,ret=%d", ret);
        goto fw_reset;
    }

    cmd[0] = FTS_CMD_APP_DATA_LEN_INCELL;
    cmd[1] = BYTE_OFF_16(len);
    cmd[2] = BYTE_OFF_8(len);
    cmd[3] = BYTE_OFF_0(len);
    ret = fts_write(cmd, FTS_CMD_DATA_LEN_LEN);
    if (ret < 0) {
        FTS_ERROR("data len cmd write fail");
        goto fw_reset;
    }

    cmd[0] = FTS_CMD_FLASH_MODE;
    cmd[1] = FLASH_MODE_UPGRADE_VALUE;
    ret = fts_write(cmd, 2);
    if (ret < 0) {
        FTS_ERROR("upgrade mode(09) cmd write fail");
        goto fw_reset;
    }

    delay = FTS_DELAY_ERASE_PAGE * (len / FTS_SIZE_PAGE);
    ret = fts_fwupg_erase(delay);
    if (ret < 0) {
        FTS_ERROR("erase cmd write fail");
        goto fw_reset;
    }

    /* write app */
    start_addr = upgrade_func_ft5008.appoff;
    delay = (FTS_FLASH_PACKET_SIZE / FTS_SIZE_PAGE) * 2;
    ecc_in_host = fts_ft5008_flash_write_buf(start_addr, buf, len, delay);
    if (ecc_in_host < 0) {
        FTS_ERROR("flash write fail");
        goto fw_reset;
    }

    /* ecc */
    ecc_in_tp = fts_fwupg_ecc_cal(start_addr, len);
    if (ecc_in_tp < 0) {
        FTS_ERROR("ecc read fail");
        goto fw_reset;
    }

    FTS_INFO("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
    if (ecc_in_tp != ecc_in_host) {
        FTS_ERROR("ecc check fail");
        goto fw_reset;
    }

    FTS_INFO("upgrade success, reset to normal boot");
    ret = fts_fwupg_reset_in_boot();
    if (ret < 0) {
        FTS_ERROR("reset to normal boot fail");
    }

    msleep(200);
    return 0;

fw_reset:
    FTS_INFO("upgrade fail, reset to normal boot");
    fts_spidebug_recovery();
    return -EIO;
}

/************************************************************************
* Name: fts_read_lockdown_info_proc
* Brief: 读 0x1F800 处 8B lockdown 信息到 buf（proc 接口）
 * 证据（blob 0x2b488..0x2b530）：
 *   fts_flash_read(0x1F800, local8, 8)；立即数实证：mov w0,#0x1f800 / mov w2,#0x8
 *   成功 -> 8 字节直拷到 buf（str x8,[x20]，源码形态 memcpy / u64 存等价）；
 *   失败 -> FTS_ERROR("fail to get vendor id from tp")（行号 325），返回 ret。
*   ★ 与 donor/活树 fts_read_lockdown_info_proc（读 0x20B + sprintf %c + 时间戳调试打印）不同，
*     blob 是简化版；且 blob 无 fts_read_lockdown_info 符号。参见 recon §5.2。
*   调用面：focaltech_core.c fts_get_lockdown_information() 传入 ts_data->lockdown_info
*   （blob 偏移 0xaf0，8B），blob 该函数直接按 8 个 u8 打印，契合同为"8 原始字节"。
***********************************************************************/
int fts_read_lockdown_info_proc(u8 *buf)
{
    u32 lockdown_addr = 0x1F800;
    int ret = 0;
    u8 lockdown_info[8] = {0};

    ret = fts_flash_read(lockdown_addr, lockdown_info, sizeof(lockdown_info));
    if (ret < 0) {
        FTS_ERROR("fail to get vendor id from tp");
        return ret;
    }

    memcpy(buf, lockdown_info, sizeof(lockdown_info));
    return 0;
}

/*
 * upgrade_func_ft5008 —— blob .data+0x2758 / 128B 逐字段抠出：
 *   +0x00 ctype[4]      = {0x90, 0x92, 0, 0}
 *   +0x08 fwveroff      = 0x010E
 *   +0x0c fwcfgoff      = 0x1F80
 *   +0x10 appoff        = 0x0000   （fts_ft5008_upgrade 内 `ldr w21,[upgrade_func_ft5008+0x10]` 实证）
 *   +0x14 licoff        = 0
 *   +0x18 paramcfgoff   = 0
 *   +0x1c paramcfgveroff= 0
 *   +0x20 paramcfg2off  = 0
 *   +0x24 pram_ecc_check_mode = 0 (ECC_CHECK_MODE_XOR)
 *   +0x28 fw_ecc_check_mode   = 0 (ECC_CHECK_MODE_XOR)
 *   +0x2c upgspec_version     = 0x0100 (UPGRADE_SPEC_V_1_0；注意非 ft5572 的 V_1_1)
 *   +0x30 new_return_value_from_ic = false
 *   +0x31 appoff_handle_in_ic      = false
 *   +0x32 is_reset_register_BC     = false
 *   +0x33 read_boot_id_need_reset  = false
 *   +0x34 hid_supported            = true   ← blob 字节 +0x34=0x01；由 fts_fwupg_enter_into_boot 消费
 *   +0x35 pramboot_supported       = false  ← 无 pramboot 数据（pramboot/pb_length 均 0）
 *   +0x38 pramboot      = NULL
 *   +0x40 pb_length     = 0
 *   +0x48 init                 = NULL
 *   +0x50 write_pramboot_private = NULL
 *   +0x58 upgrade      = fts_ft5008_upgrade   ← 全对象唯一重定位（.rela.data @0x27b0）
 *   +0x60 get_hlic_ver = NULL
 *   +0x68 lic_upgrade  = NULL
 *   +0x70 param_upgrade= NULL
 *   +0x78 force_upgrade= NULL
 * 布局裁定：活树 struct upgrade_func = 128B，upgrade 成员在 +0x58（DWARF 实证），与 blob 完全一致。
 */
struct upgrade_func upgrade_func_ft5008 = {
    .ctype = {0x90, 0x92},
    .fwveroff = 0x010E,
    .fwcfgoff = 0x1F80,
    .appoff = 0x0000,
    .upgspec_version = UPGRADE_SPEC_V_1_0,
    .pramboot_supported = false,
    .hid_supported = true,
    .upgrade = fts_ft5008_upgrade,
};
