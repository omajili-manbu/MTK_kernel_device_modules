// SPDX-License-Identifier: GPL-2.0
/*
 * focaltech_test_ft5672.c — rodin FT5672 工厂测试套件（A-70 ③，blob 机器码重建）
 * 拼装自 tools/_b570_a70/src/{seg1,seg2,seg3,compare_new}，证据链见各 recon 文档。
 * 分段顺序 = blob .text 地址序；对象初始化置尾（blob .data 0x26d8 128B，12 槽）。
 */

#include "../focaltech_test.h"



/*****************************************************************************
* private constant and macro definitions using #define
*****************************************************************************/
/* FT5672 专用寄存器（blob 实测字面量；活树 focaltech_test.h 中无同名宏者以 _FT5672 后缀标注） */
#define FT5672_REG_RAWDIFF_SEL          0x06    /* FACTORY_REG_DATA_SELECT      */
#define FT5672_REG_LINE_ADDR            0x01    /* FACTORY_REG_LINE_ADDR        */
#define FT5672_REG_RAWDATA_ADDR         0x36    /* FACTORY_REG_RAWDATA_ADDR_MC_SC */
#define FT5672_REG_FRE_LIST             0x0A    /* FACTORY_REG_FRE_LIST         */
#define FT5672_REG_DATA_TYPE            0x5B    /* FACTORY_REG_DATA_TYPE        */
#define FT5672_REG_MODE_BD              0xBD    /* NEW (mc_sc panel-differ 用)  */
#define FT5672_REG_NORMALIZE            0x16    /* FACTORY_REG_NORMALIZE        */
#define FT5672_REG_NOMAPPING            0x54    /* FACTORY_REG_NOMAPPING        */
#define FT5672_REG_WP_HOLD              0x59    /* noise 用: 0x59 <- 1          */
#define FT5672_REG_SCAN_MODE            0x1B    /* noise 用: 0x1B <- scan_mode  */
#define FT5672_REG_SHORT2_STATE         0xC3    /* FACTROY_REG_SHORT2_TEST_STATE*/
#define FT5672_REG_CB_H_OFF             0x49    /* FACTORY_REG_MC_SC_CB_H_ADDR_OFF */
#define FT5672_REG_CB_OFF               0x45    /* FACTORY_REG_MC_SC_CB_ADDR_OFF */
#define FT5672_REG_CB_ADDR              0x4E    /* FACTORY_REG_MC_SC_CB_ADDR    */
#define FT5672_REG_NOISE_DATA           0xCE    /* noise mass-data 起始地址     */
#define FT5672_REG_NOISE_STATE          0x00    /* noise scan 状态寄存器        */

/* 裸 printf 前缀（blob 中直接以字面量出现，非 FTS_TEST_* 宏） */
/* _b582-INTB：blob 串 = 0x01+'6'+"[FTS_TS]ab_ch:"；原 "\x016" 被 C 贪婪 hex 转义吞成 0x16，
 * 改为显式两段拼接（0x16 非 0x01+'6'）。 */
#define FT5672_DBG_AB_CH       "\001" "6[FTS_TS]ab_ch:"
#define FT5672_DBG_AB_CH_VAL   "\001" "6%2d "
#define FT5672_DBG_NL          "\x016\n"

/* per-test fail buffer 数组槽位（blob 实测：tdata+0x12c0 + k*0x7534，元素=30004B）
 * 本段用到 3 个槽；全量实测槽位 = 3,4,5,6,7,8,10,17,32（其余见 recon §3.6） */
#define FT5672_FAILBUF_PANEL_DIFFER     3
#define FT5672_FAILBUF_RAWSHIFT_PIC     10
#define FT5672_FAILBUF_SHORT            32

/* xiaomi 事件 id（blob 实测立即数） */
#define FT5672_XIAOMI_EVT_PANEL_DIFFER  0x365c0bd1
#define FT5672_XIAOMI_EVT_SHORT         0x365c0bd2
#define FT5672_XIAOMI_MODULE            "focal"

/* SC_NUM_MAX 来自 focaltech_test.h (=160) */

/*****************************************************************************
* static function prototypes（本文件内前向声明，顺序 = blob 地址顺序）
*****************************************************************************/
static int ft3658_get_rawdata(bool is_raw, int *data_buffer, int byte_num);
static int ft3658_data_dump(int *rawdata, int *differ_data);
static void free_item_data(struct fts_test *tdata);
static int malloc_item_data(struct fts_test *tdata);
static int fts_open_test(void);
static int fts_short_test(void);
static int fts_spi_test(void);
static int get_cb_ft5672(int *cb_buf, int byte_num);
static int get_noise_ft5672(struct fts_test *tdata, int *noise_data, u8 fre, u8 scan_mode, bool wp);
static int get_null_noise(struct fts_test *tdata);
static int ft5672_short_test(struct fts_test *tdata, bool *test_result);
static int ft5672_panel_differ_test(struct fts_test *tdata, bool *test_result);
static int ft5672_rawshift_pic_test(struct fts_test *tdata, bool *test_result, u8 pic_type);
static int param_init_ft5672(void);

/* ==== 跨段前向声明（三段合并后补齐；blob 内联关系见各 recon）==== */
static int get_rawshift_fre_data(struct fts_test *tdata, int *data, u8 fre, u8 frames, bool *valid);
static int fts_rawshift_fre_test(void);
static int fts_rawshift_pic_black_test(void);
static int fts_rawshift_pic_white_test(void);

/*****************************************************************************
* static functions
*****************************************************************************/

/*
 * ft3658_get_rawdata - 读一组 rawdata 或 differ data（FT5672 版）
 * blob: 0x2a734 / 1100B / LOCAL；唯一调用方 ft3658_data_dump
 * donor: ft3383.c ft3383_get_rawdata —— 同构，差异：
 *   + malloc 的 buffer 在读完后 fts_free_proc()（donor 泄漏）
 *   + 循环内 (int)(short) 语义（sxth 指令为证）
 *   + 读失败后仍继续 restore_reg 流程（donor 同）
 */
static int ft3658_get_rawdata(bool is_raw, int *data_buffer, int byte_num)
{
    int ret = 0;
    int i = 0;
    u8 data_type = 0;
    u8 data_sel = 0;
    u8 fre = 0;
    u8 *data = NULL;

    data = (u8 *)fts_malloc(byte_num * sizeof(u8));
    if (data == NULL) {
        FTS_TEST_SAVE_ERR("raw/diff data buffer malloc fail\n");
        return -ENOMEM;
    }
    /* select rawdata */
    ret = fts_test_write_reg(FT5672_REG_RAWDIFF_SEL, is_raw ? 0x00 : 0x01);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set fir fail,ret=%d\n", ret);
        goto restore_reg;
    }
    ret = start_scan();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("scan fail\n");
        goto restore_reg;
    }
    ret = fts_test_write_reg(FT5672_REG_LINE_ADDR, TEST_RETVAL_AA);
    if (ret < 0)
        FTS_TEST_SAVE_ERR("write line addr fail\n");   /* 仅记录：blob 0x2ab18 回到 read，不跳 restore */
    ret = fts_test_read(FT5672_REG_RAWDATA_ADDR, data, byte_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read mass data fail\n");
        goto restore_reg;
    }
    for (i = 0; i < byte_num; i = i + 2) {
        data_buffer[i >> 1] = (int)(s16)(((short)(data[i] << 8)) | data[i + 1]);
    }
restore_reg:
    ret = fts_test_write_reg(FT5672_REG_FRE_LIST, fre);
    if (ret < 0)
        FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);
    ret = fts_test_write_reg(FT5672_REG_DATA_TYPE, data_type);
    if (ret < 0)
        FTS_TEST_SAVE_ERR("restore 0x5B fail,ret=%d\n", ret);
    ret = fts_test_write_reg(FT5672_REG_RAWDIFF_SEL, data_sel);
    if (ret < 0)
        FTS_TEST_SAVE_ERR("restore 0x06 fail,ret=%d\n", ret);
    fts_free_proc(data);              /* blob 0x2a818: 无 NULL 判定的直接调用 */
    return ret;
}

/*
 * ft3658_data_dump - 供 .data 函数表 (+0x50) 调用
 * blob: 0x246bc / 192B / LOCAL；FTS_TEST_FUNC_EXIT __LINE__ = 4289
 * donor ft3383_data_dump 同构，仅被调函数改名。
 */
static int ft3658_data_dump(int *rawdata, int *differ_data)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;

    FTS_TEST_FUNC_ENTER();
    ret = ft3658_get_rawdata(true, rawdata, tdata->node.node_num * 2);
    if (ret) {
        FTS_TEST_ERROR("get rawdata error!");
        goto out;
    }
    ret = ft3658_get_rawdata(false, differ_data, tdata->node.node_num * 2);
    if (ret) {
        FTS_TEST_ERROR("get differ_data error!");
        goto out;
    }
out:
    FTS_TEST_FUNC_EXIT();             /* __LINE__ 需 = 4289（字节级） */
    return ret;
}

/*
 * free_item_data - 函数表 (+0x78)
 * blob: 0x28878 / 164B / LOCAL
 * donor 只有 item1..6 且用 fts_free 宏；blob = item1..item7 + item8_data(0x88)，
 * 逐项展开成 fts_free() 宏形态（判空 + fts_free_proc + 置 NULL）。
 * 偏移实测：0x40/0x48/0x50/0x58/0x60/0x68/0x70(item1..7) + 0x88(item8_data)
 */
static void free_item_data(struct fts_test *tdata)
{
    fts_free(tdata->item1_data);
    fts_free(tdata->item2_data);
    fts_free(tdata->item3_data);
    fts_free(tdata->item4_data);
    fts_free(tdata->item5_data);
    fts_free(tdata->item6_data);
    fts_free(tdata->item7_data);
    fts_free(tdata->item8_data);      /* +0x88：第 8 个 malloc 数据（blob 0x288fc/0x28908 实证；
                                         node_valid 在 0xa0，见 recon §5.3/[TODO-VERIFY-9] 已归零） */
}

/*
 * malloc_item_data - [GAP-FILL] 见 recon §6：fts_open_test/fts_short_test/fts_spi_test/
 *   fts_rawshift_pic_{black,white}_test 均调用；blob: 0x2891c / 392B / LOCAL
 * 与 donor 差异：每项都先判空再分配（可重入），大小倍数实测
 *   item1 = node_num*4B, item2 = node_num*8B, item3 = sc_node*24B,
 *   item4 = sc_node*12B, item5 = node_num*4B, item6 = node_num*4B,
 *   item7 = node_num*24B, item8_data = node_num*60B
 * [TODO-VERIFY] item7/node_valid 的倍数（24B/60B）来自 shift-add 序列
 *   0x289e8(x8*3<<3=*24)、0x28a08(x8<<6 - x8<<2 = *60)；语义解释见 recon。
 */
static int malloc_item_data(struct fts_test *tdata)
{
    if (!tdata->item1_data) {
        tdata->item1_data = (int *)fts_malloc(tdata->node.node_num * sizeof(int));
        if (!tdata->item1_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item2_data) {
        tdata->item2_data = (int *)fts_malloc(tdata->node.node_num * sizeof(int) * 2);
        if (!tdata->item2_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item3_data) {
        tdata->item3_data = (int *)fts_malloc(tdata->sc_node.node_num * sizeof(int) * 6);
        if (!tdata->item3_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item4_data) {
        tdata->item4_data = (int *)fts_malloc(tdata->sc_node.node_num * sizeof(int) * 3);
        if (!tdata->item4_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item5_data) {
        tdata->item5_data = (int *)fts_malloc(tdata->node.node_num * sizeof(int));
        if (!tdata->item5_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item6_data) {
        tdata->item6_data = (int *)fts_malloc(tdata->node.node_num * sizeof(int));
        if (!tdata->item6_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item7_data) {
        tdata->item7_data = (int *)fts_malloc(tdata->node.node_num * sizeof(int) * 6);
        if (!tdata->item7_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    if (!tdata->item8_data) {
        tdata->item8_data = (int *)fts_malloc(tdata->node.node_num * 15 * sizeof(int));
        if (!tdata->item8_data) {
            FTS_TEST_SAVE_ERR("memory malloc fails");
            return -ENOMEM;
        }
    }
    return 0;
}

/*
 * fts_open_test - 函数表 (+0x38)，open/panel-differ 自检入口
 * blob: 0x240fc / 468B / LOCAL；FTS_TEST_FUNC_EXIT __LINE__ = 2932
 * 与 donor 差异（实测）：
 *   - 删除了 memset(testdata)/fts_test_init_basicinfo/fts_test_malloc_free_thr/
 *     fts_test_get_testparam_from_ini 四个块
 *   - 删除尾部 fts_test_main_exit() + enter_work_mode()
 *   - panel_differ 检查位 = mc_sc.u.item 的 bit5（tree 同名位，未变）
 */
static int fts_open_test(void)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;
    struct mc_sc_testitem *test_item = &tdata->ic.mc_sc.u.item;
    bool temp_result = false;
    bool test_result = true;

    FTS_TEST_FUNC_ENTER();
    ret = fts_test_read_reg(0x14, &tdata->fre_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read fre_num fails");
        return ret;
    }
    FTS_TEST_INFO("fre_num:%d", tdata->fre_num);
    ret = malloc_item_data(tdata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("memory malloc fails");
        return -ENOMEM;
    }
    /* panel differ test */
    if (true == test_item->panel_differ_test) {
        ret = ft5672_panel_differ_test(tdata, &temp_result);
        if ((ret < 0) || (false == temp_result))
            test_result = false;
    }
    if (test_result == true)
        ret = SELFTEST_PASS;
    else
        ret = SELFTEST_FAIL;
    fts_test_write_reg(FACTORY_REG_NOMAPPING, tdata->mapping);
    FTS_TEST_FUNC_EXIT();             /* __LINE__ 需 = 2932 */
    return ret;
}

/*
 * fts_short_test - 函数表 (+0x40)
 * blob: 0x242d4 / 468B / LOCAL；FTS_TEST_FUNC_EXIT __LINE__ = 2973
 * 与 donor 差异：删除尾部 enter_work_mode()。其余逐指令同构
 * （唯一差异：blob 多一条 strb wzr,[sp,#4] = temp_result=false 的显式初始化）。
 */
static int fts_short_test(void)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;
    struct mc_sc_testitem *test_item = &tdata->ic.mc_sc.u.item;
    bool temp_result = false;
    bool test_result = true;

    FTS_TEST_FUNC_ENTER();
    ret = fts_test_read_reg(0x14, &tdata->fre_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read fre_num fails");
        return ret;
    }
    FTS_TEST_INFO("fre_num:%d", tdata->fre_num);
    ret = malloc_item_data(tdata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("memory malloc fails");
        return -ENOMEM;
    }
    /* short test */
    if (true == test_item->short_test) {
        ret = ft5672_short_test(tdata, &temp_result);
        if ((ret < 0) || (false == temp_result))
            test_result = false;
    }
    if (test_result == true)
        ret = SELFTEST_PASS;
    else
        ret = SELFTEST_FAIL;
    fts_test_write_reg(FACTORY_REG_NOMAPPING, tdata->mapping);
    FTS_TEST_FUNC_EXIT();             /* __LINE__ 需 = 2973 */
    return ret;
}

/*
 * fts_spi_test - 函数表 (+0x48)
 * blob: 0x244ac / 524B / LOCAL；FTS_TEST_FUNC_EXIT __LINE__ = 3017
 * donor .o 逐指令等价（仅寄存器分配差异：blob 提前 orr x20 = &chip_id[1]）。
 */
static int fts_spi_test(void)
{
    int ret = 0;
    struct fts_test *tdata = fts_ftest;
    /*struct mc_sc_testitem *test_item = &tdata->ic.mc_sc.u.item;*/
    u8 chip_id[2] = { 0 };
    int cnt = 0;

    FTS_TEST_FUNC_ENTER();
    ret = fts_test_read_reg(0x14, &tdata->fre_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read fre_num fails");
        return ret;
    }
    FTS_TEST_INFO("fre_num:%d", tdata->fre_num);
    ret = malloc_item_data(tdata);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("memory malloc fails");
        return -ENOMEM;
    }
    do {
        ret = fts_read_reg(FTS_REG_CHIP_ID, &chip_id[0]);
        ret = fts_read_reg(FTS_REG_CHIP_ID2, &chip_id[1]);
        if ((ret < 0) || (chip_id[0] == 0x0) || (chip_id[1] == 0x0)) {
            cnt++;
            ret = SELFTEST_FAIL;
            msleep(100);
        } else {
            ret = SELFTEST_PASS;
            FTS_TEST_INFO("spi test success, Device id: 0x%02x%02x",
                          chip_id[0], chip_id[1]);
            break;
        }
    } while (cnt < 10);
    fts_test_write_reg(FACTORY_REG_NOMAPPING, tdata->mapping);
    FTS_TEST_FUNC_EXIT();             /* __LINE__ 需 = 3017 */
    return ret;
}

/*
 * get_cb_ft5672 - 读 CB（SCap CB）数据
 * blob: 0x29c8c / 812B / LOCAL；调用方 = start_test_ft5672 x3
 * 与 donor get_cb_ft5572 差异（实测）：
 *   - 无 is_cf 形参（blob 调用点仅 2 实参）
 *   - 字节->int 转换 = 符号-幅值格式：v = (cb[i] & 0x7f)，若 (s8)cb[i] < 0 取负
 *     （sxtb/and #0x7f/cneg 序列为证）→ 输出 1 int / 1 byte（donor 为 2 byte/1 int）
 *   - 初始 3 条写/读寄存器同 donor（0x49/0x45/0x4E）
 *   - 边界 = byte_num >= SC_NUM_MAX(=160) → SAVE_ERR("CB byte(%d)>max(%d)") + (-EINVAL)
 */
static int get_cb_ft5672(int *cb_buf, int byte_num)
{
    int ret = 0;
    int i = 0;
    s8 cb[SC_NUM_MAX] = { 0 };

    if (byte_num >= SC_NUM_MAX) {
        FTS_TEST_SAVE_ERR("CB byte(%d)>max(%d)", byte_num, SC_NUM_MAX);
        return -EINVAL;
    }

    ret = fts_test_write_reg(FT5672_REG_CB_H_OFF, 0);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write cb_h addr offset fail\n");
        return ret;
    }
    ret = fts_test_write_reg(FT5672_REG_CB_OFF, 0);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write cb addr offset fail\n");
        return ret;
    }
    ret = fts_test_read(FT5672_REG_CB_ADDR, (u8 *)cb, byte_num);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read cb fail\n");
        return ret;
    }

    for (i = 0; i < byte_num; i++) {
        /* 符号-幅值：bit7 = 符号，bit6..0 = 幅值 */
        cb_buf[i] = ((s8)cb[i] < 0) ? -(cb[i] & 0x7F) : (cb[i] & 0x7F);
    }

    return 0;
}

/*
 * get_noise_ft5672 - 噪声数据采集（内联了 start_scan_ft5672 的等待循环）
 * blob: 0x29fb8 / 1392B / LOCAL；调用方 start_test_ft5672 x3
 * 形参：x0=tdata, x1=noise_data, w2=fre, w3=scan_mode/reg0x1B 值, w4=wp/enable 位
 * 实测要素：
 *   - R0x0A<-fre; wait_state_update(0xAA); if (wp) { R0x59<-1; sys_delay(18); }
 *   - R0x1B<-scan_mode
 *   - tdata->func 空检查（+0x3c8）→ "test/func is null"
 *   - 等待循环：sys_delay(18); fts_test_read_reg(0x00,&v); v==0x40 才跳出（retry 计数打 reg%x=%x,retry:%d）
 *   - 超时 → "scan timeout"
 *   - read_mass_data(0xCE, node_num*2, noise_data)
 * [TODO-VERIFY] 等待循环的重试上限/精确计数（blob 用 w26/w19 双计数，疑似 retry 上限来自 func->startscan_mode）。
 */
static int get_noise_ft5672(struct fts_test *tdata, int *noise_data, u8 fre, u8 scan_mode, bool wp)
{
    int ret = 0;
    int retry = 0;
    int frames = tdata->ic.mc_sc.thr.basic.noise_framenum;
    u8 state = 0;

    FTS_TEST_FUNC_ENTER();
    ret = fts_test_write_reg(FT5672_REG_FRE_LIST, fre);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
        goto out;
    }
    ret = wait_state_update(TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
        goto out;
    }
    if (wp) {
        ret = fts_test_write_reg(FT5672_REG_WP_HOLD, 0x01);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("write 0x01 fail,ret=%d\n", ret);
            goto out;
        }
        sys_delay(18);
    }
    ret = fts_test_write_reg(FT5672_REG_SCAN_MODE, scan_mode);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write start scan mode fail\n");
        goto out;
    }
    if (!fts_ftest || !tdata->func) {
        FTS_TEST_SAVE_ERR("test/func is null\n");
        goto out;
    }
    /* start_scan_ft5672 等待循环（被内联） */
    for (retry = 0; retry < frames; retry++) {
        sys_delay(18);
        ret = fts_test_read_reg(FT5672_REG_NOISE_STATE, &state);
        FTS_TEST_DBG("reg%x=%x,retry:%d", FT5672_REG_NOISE_STATE, state, retry);   /* _b582-INTB：blob DBG 族无尾 \n */
        if ((ret >= 0) && (state == 0x40))
            break;
    }
    if (retry >= frames) {
        FTS_TEST_SAVE_ERR("scan timeout\n");
        ret = -ETIMEDOUT;             /* [TODO-VERIFY] 错误码：blob 该路径 w19 = -5 */
        goto out;
    }
    ret = fts_test_write_reg(FT5672_REG_LINE_ADDR, TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write 0x01 fail,ret=%d\n", ret);
        goto out;
    }
    ret = read_mass_data(FT5672_REG_NOISE_DATA, tdata->node.node_num * 2, noise_data);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read noise fail\n");
        goto out;
    }
out:
    return ret;
}

/*
 * get_null_noise - [GAP-FILL] 读一路“空噪声”基准
 * blob: 0x2a528 / 524B / LOCAL；调用方 = start_test_ft5672 @0x23218
 * 实测串：null noise num:%d / null_noise malloc fail / write 0x01 fail,
 *         read null noise fail / null noise:%d
 * [TODO-VERIFY] 形参个数与 buffer 处理（blob 内部先 fts_malloc 再读，读后释放未在本函数证实）。
 */
static int get_null_noise(struct fts_test *tdata)
{
	int ret = 0;
	int total = tdata->node.rx_num * tdata->fre_num + 1;   /* w21*fre + 1，blob 0x2a558/0x2a56c */
	int *buf = NULL;

	tdata->csv_item_cnt++;                                 /* 0x2a55c/0x2a560 */
	FTS_TEST_INFO("null noise num:%d", total);             /* 0x2a564 串 +0x660e */

	buf = (int *)kmalloc(total * sizeof(int), GFP_KERNEL); /* 0x2a578 sbfiz*4 + __kmalloc(0xdc0) */
	if (!buf) {
		FTS_TEST_ERROR("null_noise malloc fail");    /* 0x2a588；_b582-INTB：blob E 族无 append（无 standalone 字面量） */
		return 0;
	}

	ret = fts_test_write_reg(FT5672_REG_LINE_ADDR, 0xB0);  /* 0x2a5a4: w0=1 w1=0xb0 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write 0x01 fail,ret=%d\n", ret); /* 0x2a604 串 +0x4904 */
		goto out;
	}
	ret = read_mass_data(FT5672_REG_NOISE_DATA, total * 2, buf);  /* 0x2a620: w1=total*2 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read null noise fail\n");      /* 0x2a680 串 +0xe0ed */
		goto out;
	}
	tdata->null_noise_value = buf[0];                      /* 0x2a6a8 str w3,[x20,#0xe8] */
	FTS_TEST_SAVE_INFO("null noise:%d\n", buf[0]);        /* 0x2a6c0 串 +0xf352 */
	/* _b582-INTB：blob 该站点无 DBG 族串（0 命中）⇒ 按 blob 删除该 DBG 行 */
	print_buffer(buf + 1, tdata->node.rx_num * tdata->fre_num,
		     tdata->node.rx_num);                      /* 0x2a700: x0=buf+4 w1=rx*fre w2=rx_num */
out:
	kfree(buf);                                            /* 0x2a710/0x2a714 */
	return ret;    /* blob 尾部无显式 return（源码漏写），调用方 0x23218 忽略返回值 */
}

/*
 * ft5672_short_test - Short(channel-to-all) 测试（内联 donor 的 short_test_ch_to_all）
 * blob: 0x28aa4 / 1764B / LOCAL；调用方 = start_test_ft5672 + fts_short_test
 * FTS_TEST_FUNC_EXIT __LINE__ = 1190
 * 与 donor ft5572_short_test 差异（实测）：
 *   - short_test_ch_to_all 被内联；其内部 save-err 的 __func__ 仍是 "short_test_ch_to_all"
 *     （字符串常量 0x906a）→ 是 #[inline] 展开，非改写
 *   - 失败项写 tdata 的 per-test fail buffer（本件基址 = tdata+0xeb940，
 *     节点数组 = base+4 + n*20，字段 tx/rx/val/min，1..1500 上限 brk 保护）
 *   - NG 分支追加 xiaomi 事件 + FTS_TEST_DBG；PASS 分支追加 FTS_TEST_INFO
 *   - 尾部无 enter_work_mode()
 */
static int ft5672_short_test(struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    int ch_num = 0;
    int i = 0;
    int min_cc = tdata->ic.mc_sc.thr.basic.short_cc;
    int short_res[SC_NUM_MAX + 1] = { 0 };
    u8 ab_ch[SC_NUM_MAX + 1] = { 0 };
    u8 ab_ch_num = 0;
    int temp = 0;
    bool ca_result = false;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Short Test\n");
    ch_num = tdata->sc_node.tx_num + tdata->sc_node.rx_num;

    if (ch_num >= SC_NUM_MAX) {
        FTS_TEST_SAVE_ERR("sc_node ch_num(%d)>max(%d)", ch_num, SC_NUM_MAX);
        ca_result = false;
        goto test_err;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
        ca_result = false;
        goto test_err;
    }

    /* short is in no-mapping mode */
    ret = mapping_switch(NO_MAPPING);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("switch no-mapping fail,ret=%d\n", ret);
        ca_result = false;
        goto test_err;
    }

    /* ---- short_test_ch_to_all() 内联体 ---- */
    {
        int byte_num = ch_num * 2;

        FTS_TEST_DBG("short test:channel to all other\n");   /* 内部函数 __func__ 字符串 */
        tdata->fail_buf[FT5672_FAILBUF_SHORT].fail_num = 0;  /* blob 0x28eec: stp/str wzr */

        /*get resistance data*/
        ret = short_get_adc_data_mc(TEST_RETVAL_AA, byte_num, &short_res[0],
                                    FACTROY_REG_SHORT2_CA);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("get weak short data fail,ret:%d\n", ret);
            goto test_err;                                    /* 内部函数 return ret */
        }
        ca_result = true;
        for (i = 0; i < ch_num; i++) {
            temp = short_res[i];
            if ((27 - ((temp * 32 / 2047) + 5)) == 0) {
                short_res[i] = 50000;
                continue;
            }
            short_res[i] = fts_abs(((temp * 32 / 2047 + 5) * 200) /
                                   (27 - ((temp * 32 / 2047) + 5)));
            if (short_res[i] < min_cc) {
                if (tdata->fail_buf[FT5672_FAILBUF_SHORT].fail_num < 1500) {
                    int n = tdata->fail_buf[FT5672_FAILBUF_SHORT].fail_num;
                    tdata->fail_buf[FT5672_FAILBUF_SHORT].node[n].tx = i;
                    tdata->fail_buf[FT5672_FAILBUF_SHORT].node[n].rx = i;
                    tdata->fail_buf[FT5672_FAILBUF_SHORT].node[n].val = short_res[i];
                    tdata->fail_buf[FT5672_FAILBUF_SHORT].node[n].min = min_cc;
                    tdata->fail_buf[FT5672_FAILBUF_SHORT].fail_num++;
                }
                ab_ch_num++;
                ab_ch[ab_ch_num] = i;
                ca_result = false;
            }
        }
        if (ab_ch_num) {
            print_buffer(short_res, ch_num, ch_num);
            ab_ch[0] = ab_ch_num;
            FTS_TEST_DBG("%s", FT5672_DBG_AB_CH);             /* blob: printk("\x016[FTS_TS]ab_ch:") */
            for (i = 1; i < ab_ch_num + 1; i++) {
                FTS_TEST_DBG("%s", FT5672_DBG_AB_CH_VAL);     /* blob: printk("\x016%2d ", ab_ch[i]) */
            }
            FTS_TEST_DBG("%s", FT5672_DBG_NL);
        }
    }
    /* ---- 内联体结束 ---- */

test_err:
    ret = fts_test_write_reg(FT5672_REG_SHORT2_STATE, 0x03);
    if (ret < 0) {
        FTS_TEST_SAVE_INFO("write short state c3 03 fail");
    }
    sys_delay(50);

    if (ca_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ short test PASS\n");
        FTS_TEST_INFO("------ short test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_ERR("------ short test NG\n");
        xiaomi_touch_mievent_report_int_common(FT5672_XIAOMI_EVT_SHORT, 0,
                                              "TpShortTestFail", FT5672_XIAOMI_MODULE, ret);
        FTS_TEST_DBG("------ short test NG");   /* _b582-INTB：blob DBG 族无尾 \n */
    }
    FTS_TEST_FUNC_EXIT();             /* __LINE__ 需 = 1190 */
    return ret;
}

/*
 * ft5672_panel_differ_test - panel differ 测试
 * blob: 0x29188 / 2820B / LOCAL；调用方 = start_test_ft5672 + fts_open_test
 * FTS_TEST_FUNC_EXIT __LINE__ = 1368 (null-guard 早退) / 最后一个 = 3384 见 recon
 * 与 donor ft5572_panel_differ_test 差异（实测）：
 *   - 新增 read/write 寄存器 0xBD（donor 用 0x5B 当 fir/data_type）
 *   - null 分支后追加 xiaomi 事件(0x365c0bd1) + DBG + 早退
 *   - /10 循环同时维护 tdata 的 panel_differ max/min 统计（0x1198/0x119c）
 *   - compare_array → compare_array_new（out = tdata+0x1725c）
 */
static int ft5672_panel_differ_test(struct fts_test *tdata, bool *test_result)
{
    int ret = 0;
    bool tmp_result = false;
    int i = 0;
    u8 fre = 0;
    u8 normalize = 0;
    u8 data_type = 0;
    u8 reg_bd = 0;
    int *panel_differ = NULL;
    struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: Panel Differ Test\n");
    panel_differ = tdata->item5_data;
    tdata->csv_item_cnt++;

    if (!panel_differ || !thr->panel_differ_min || !thr->panel_differ_max) {
        FTS_TEST_SAVE_ERR("panel_differ_h_min/max is null\n");
        ret = -EINVAL;
        *test_result = false;
        xiaomi_touch_mievent_report_int_common(FT5672_XIAOMI_EVT_PANEL_DIFFER, 0,
                                              "TpOpenTestFail", FT5672_XIAOMI_MODULE, ret);
        FTS_TEST_DBG("------ panel differ test NG");   /* _b582-INTB：blob DBG 族无尾 \n */
        FTS_TEST_FUNC_EXIT();         /* __LINE__ 需 = 1368 */
        return ret;
    }

    ret = enter_factory_mode();
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
        goto out;
    }

    /* panel differ test in mapping mode */
    ret = mapping_switch(MAPPING);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("switch mapping fail,ret=%d\n", ret);
        goto out;
    }

    /* save origin value */
    ret = fts_test_read_reg(FT5672_REG_FRE_LIST, &fre);
    if (ret) {
        FTS_TEST_SAVE_ERR("read 0x0A fail,ret=%d\n", ret);
        goto test_err;
    }
    ret = fts_test_read_reg(FT5672_REG_RAWDIFF_SEL, &data_type);
    if (ret) {
        FTS_TEST_SAVE_ERR("read 0x5B fail,ret=%d\n", ret);
        goto test_err;
    }
    ret = fts_test_read_reg(FT5672_REG_MODE_BD, &reg_bd);
    if (ret) {
        FTS_TEST_SAVE_ERR("read regBD fail,ret=%d\n", ret);
        goto test_err;
    }
    ret = fts_test_read_reg(FT5672_REG_NORMALIZE, &normalize);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("read normalize fail,ret=%d\n", ret);
        goto test_err;
    }

    /* set frequecy high */
    ret = fts_test_write_reg(FT5672_REG_FRE_LIST, 0x81);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
        goto restore_reg;
    }
    ret = wait_state_update(TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
        goto restore_reg;
    }

    ret = fts_test_write_reg(FT5672_REG_RAWDIFF_SEL, 0x0);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
        goto restore_reg;
    }
    ret = fts_test_write_reg(FT5672_REG_MODE_BD, 0x0);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set fir fail,ret=%d\n", ret);
        goto restore_reg;
    }
    ret = wait_state_update(TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
        goto restore_reg;
    }

    /* set to overall normalize */
    ret = fts_test_write_reg(FT5672_REG_NORMALIZE, 0x00);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("write normalize fail,ret=%d\n", ret);
        goto restore_reg;
    }

    /* get rawdata */
    for (i = 0; i < 3; i++) {
        ret = get_rawdata(panel_differ);
        if (ret < 0) {
            FTS_TEST_SAVE_ERR("get rawdata fail\n");
            goto restore_reg;
        }
    }

    tdata->panel_differ_max = 0;
    tdata->panel_differ_min = 0xFFFF;
    for (i = 0; i < tdata->node.node_num; i++) {
        panel_differ[i] = panel_differ[i] / 10;
        if (panel_differ[i] > tdata->panel_differ_max)
            tdata->panel_differ_max = panel_differ[i];
        if (panel_differ[i] < tdata->panel_differ_min)
            tdata->panel_differ_min = panel_differ[i];
    }

    /* show test data */
    show_data(panel_differ, false);

    /* compare */
    tmp_result = compare_array_new(&tdata->fail_buf[FT5672_FAILBUF_PANEL_DIFFER], panel_differ,
                                   thr->panel_differ_min,
                                   thr->panel_differ_max,
                                   false);

restore_reg:
    /* set the origin value */
    ret = fts_test_write_reg(FT5672_REG_FRE_LIST, fre);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);
    }
    ret = wait_state_update(TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
    }
    ret = fts_test_write_reg(FT5672_REG_RAWDIFF_SEL, data_type);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
    }
    ret = fts_test_write_reg(FT5672_REG_MODE_BD, reg_bd);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore 0xBD fail,ret=%d\n", ret);
    }
    ret = wait_state_update(TEST_RETVAL_AA);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("wait state update fail\n");
    }
    ret = fts_test_write_reg(FT5672_REG_NORMALIZE, normalize);
    if (ret < 0) {
        FTS_TEST_SAVE_ERR("restore normalize fail,ret=%d\n", ret);
    }

test_err:
out:
    /* result */
    if (tmp_result) {
        *test_result = true;
        FTS_TEST_SAVE_INFO("------ panel differ test PASS\n");
    } else {
        *test_result = false;
        FTS_TEST_SAVE_ERR("------ panel differ test NG\n");
    }

    FTS_TEST_FUNC_EXIT();             /* __LINE__ = 3384? 见 recon 备注 */
    return ret;
}

/*
 * ft5672_rawshift_pic_test - RawShift PIC（黑/白图）测试；两次调用状态机
 * blob: 0x2af04 / 1408B / LOCAL；调用方 = fts_rawshift_pic_black_test(w2=0) /
 *       fts_rawshift_pic_white_test(w2=1)（F2c）
 * 实测状态机（pic_state 位于 tdata+0xd0，int 位域语义）：
 *   bit0 = black 数据就绪；bit1 = white 数据就绪；bit2 = 已完成比对
 *   black 调用: state=0 → 采图 → |=1 → state!=3 → 报 black OK/NG（按 *result 输出参数）
 *   white 调用: state|=2 → 若 ==3 则算 |black-white| → show_data → compare_data_new
 *               (out=tdata+0x4a6c8, data=tdata+0x1335b4, min=0, max=thr->rawshift_pic_threshold)
 *               → state=7 → 决定 PASS/NG（w0 = compare 结果）
 *   min/max 统计写入 tdata+0x12a4..0x12b8（black/white/differ 各 max,min）
 * [TODO-VERIFY] thr->basic 的 3 个新字段命名（rawshift_pic / _frames / _threshold
 *   对应 ini 键 RawShift_PIC / RawShift_PIC_Frames / RawShift_PIC_Threshold）。
 */
static int ft5672_rawshift_pic_test(struct fts_test *tdata, bool *test_result, u8 pic_type)
{
    int ret = 0;
    int i = 0;
    bool black_ok = false;
    bool white_ok = false;
    bool pic_ok = false;               /* w0：cross-call 判定值 */
    int *black_data = tdata->rawshift_pic_black_data;
    int *white_data = tdata->rawshift_pic_white_data;
    int *pic_differ = tdata->rawshift_pic_differ_data;
    struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;
    u8 frames = thr->basic.rawshift_pic_frames;

    FTS_TEST_FUNC_ENTER();
    FTS_TEST_SAVE_INFO("\n============ Test Item: RawShift PIC Test\n");
    tdata->csv_item_cnt++;

    if (!thr->basic.rawshift_pic || !thr->basic.rawshift_pic_threshold) {
        FTS_TEST_SAVE_ERR("rawshift_pic_data/rawshift_pic/rawshift_pic_threshold is null\n");
        pic_ok = false;
        goto verdict;
    }

    ret = enter_factory_mode();        /* blob: 返回值未检查（0x2b0bc 直接测 pic_type） */

    if (0 == pic_type) {               /* black */
        tdata->rawshift_result_mask = 0;
        black_ok = true;
        ret = get_rawshift_fre_data(tdata, black_data, 0x81, frames, &black_ok);
        if (ret < 0) {
            /* _b582-INTB：blob ft5672_rawshift_pic_test 串面 0 命中（无此日志）⇒ 按 blob 删除 */
            pic_ok = false;
            goto verdict;
        }
        tdata->rawshift_result_mask |= 0x01;
        show_data(black_data, false);
        pic_ok = false;
        if (tdata->rawshift_result_mask != 0x03)
            goto verdict;
    } else {                           /* white */
        ret = get_rawshift_fre_data(tdata, white_data, 0x81, frames, &white_ok);
        if (ret < 0) {
            /* _b582-INTB：blob ft5672_rawshift_pic_test 串面 0 命中（无此日志）⇒ 按 blob 删除 */
            pic_ok = false;
            goto verdict;
        }
        tdata->rawshift_result_mask |= 0x02;
        show_data(white_data, false);
        pic_ok = false;
        if (tdata->rawshift_result_mask != 0x03)
            goto verdict;
    }

    /* 两路数据齐备 → 求差 + 比对 */
    tdata->rawshift_result_mask = 0x07;
    tdata->rawshift_black_max = 0;
    tdata->rawshift_black_min = 0xFFFF;
    tdata->rawshift_white_max = 0;
    tdata->rawshift_white_min = 0xFFFF;
    tdata->rawshift_differ_max = 0;
    tdata->rawshift_differ_min = 0xFFFF;

    for (i = 0; i < tdata->node.node_num; i++)
        pic_differ[i] = fts_abs(black_data[i] - white_data[i]);
    show_data(pic_differ, false);
    pic_ok = compare_data_new(&tdata->fail_buf[FT5672_FAILBUF_RAWSHIFT_PIC], pic_differ, 0,
                              thr->basic.rawshift_pic_threshold, 0, 0, false);
    for (i = 0; i < tdata->node.node_num; i++) {
        if (pic_differ[i] > tdata->rawshift_differ_max)
            tdata->rawshift_differ_max = pic_differ[i];
        if (black_data[i] > tdata->rawshift_black_max)
            tdata->rawshift_black_max = black_data[i];
        if (white_data[i] > tdata->rawshift_white_max)
            tdata->rawshift_white_max = white_data[i];
        if (pic_differ[i] < tdata->rawshift_differ_min)
            tdata->rawshift_differ_min = pic_differ[i];
        if (black_data[i] < tdata->rawshift_black_min)
            tdata->rawshift_black_min = black_data[i];
        if (white_data[i] < tdata->rawshift_white_min)
            tdata->rawshift_white_min = white_data[i];
    }

verdict:
    if (tdata->rawshift_result_mask & 0x04) {
        if (pic_ok) {
            *test_result = true;
            FTS_TEST_SAVE_INFO("rawshift pic is OK\n");
			FTS_TEST_INFO("rawshift pic is OK\n");
        } else {
            *test_result = false;
            FTS_TEST_SAVE_INFO("rawshift pic is NG\n");
			FTS_TEST_INFO("rawshift pic is NG\n");
        }
    } else if (pic_type) {             /* white 单跑：按 white_ok 报 */
        if (white_ok) {
            *test_result = true;
            FTS_TEST_SAVE_INFO("rawshift pic white is OK\n");
			FTS_TEST_INFO("rawshift pic white is OK\n");
        } else {
            *test_result = false;
            FTS_TEST_SAVE_INFO("rawshift pic white is NG\n");
			FTS_TEST_INFO("rawshift pic white is NG\n");
        }
    } else {                           /* black 单跑：按 black_ok 报 */
        if (black_ok) {
            *test_result = true;
            FTS_TEST_SAVE_INFO("rawshift pic black is OK\n");
			FTS_TEST_INFO("rawshift pic black is OK\n");
        } else {
            *test_result = false;
            FTS_TEST_SAVE_INFO("rawshift pic black is NG\n");
			FTS_TEST_INFO("rawshift pic black is NG\n");
        }
    }
    FTS_TEST_FUNC_EXIT();              /* [TODO-VERIFY] __LINE__ */
    return ret;
}

/*
 * param_init_ft5672 - ini 阈值初始化（58 x get_keyword_value + 4 x INFO）
 * blob: 0x1d74c / 1260B / LOCAL；节名固定 "Basic_Threshold"
 * 逐字段证据 = 每条 get_keyword_value(section, key, &thr->basic.*)，键名/偏移表见 recon §4。
 * FTS_TEST_FUNC_EXIT 不存在（本函数无 Enter/Exit 打印）。
 */
static int param_init_ft5672(void)
{
    struct mc_sc_threshold *thr = &fts_ftest->ic.mc_sc.thr;

    get_keyword_value("Basic_Threshold", "SCapCbTest_ON_Global_Cb_Min", &thr->basic.scap_cb_on_gcb_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_ON_Global_Cb_Max", &thr->basic.scap_cb_on_gcb_max);
    get_keyword_value("Basic_Threshold", "SCapCbTest_OFF_Global_Cb_Min", &thr->basic.scap_cb_off_gcb_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_OFF_Global_Cb_Max", &thr->basic.scap_cb_off_gcb_max);
    get_keyword_value("Basic_Threshold", "SCapCbTest_High_Global_Cb_Min", &thr->basic.scap_cb_hi_gcb_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_High_Global_Cb_Max", &thr->basic.scap_cb_hi_gcb_max);
    FTS_TEST_INFO("GCB,On(%d,%d),Off(%d,%d),High(%d,%d)",
                  thr->basic.scap_cb_on_gcb_min, thr->basic.scap_cb_on_gcb_max,
                  thr->basic.scap_cb_off_gcb_min, thr->basic.scap_cb_off_gcb_max,
                  thr->basic.scap_cb_hi_gcb_min, thr->basic.scap_cb_hi_gcb_max);

    get_keyword_value("Basic_Threshold", "SCapCbTest_ON_Cf_Cb_Min", &thr->basic.scap_cb_on_cf_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_ON_Cf_Cb_Max", &thr->basic.scap_cb_on_cf_max);
    get_keyword_value("Basic_Threshold", "SCapCbTest_OFF_Cf_Cb_Min", &thr->basic.scap_cb_off_cf_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_OFF_Cf_Cb_Max", &thr->basic.scap_cb_off_cf_max);
    get_keyword_value("Basic_Threshold", "SCapCbTest_High_Cf_Cb_Min", &thr->basic.scap_cb_hi_cf_min);
    get_keyword_value("Basic_Threshold", "SCapCbTest_High_Cf_Cb_Max", &thr->basic.scap_cb_hi_cf_max);

    get_keyword_value("Basic_Threshold", "NoiseTest_Max", &thr->basic.noise_max);
    get_keyword_value("Basic_Threshold", "NoiseTest_Frames", &thr->basic.noise_framenum);
    get_keyword_value("Basic_Threshold", "NoiseTest_FwNoiseMode", &thr->basic.noise_mode);
    get_keyword_value("Basic_Threshold", "Polling_Frequency", &thr->basic.noise_polling);
    FTS_TEST_INFO("noise_max:%d,frame_num:%d,noise_mode:%d,polling:%d",
                  thr->basic.noise_max, thr->basic.noise_framenum,
                  thr->basic.noise_mode, thr->basic.noise_polling);

    get_keyword_value("Basic_Threshold", "RawShift_PIC", &thr->basic.rawshift_pic);
    get_keyword_value("Basic_Threshold", "RawShift_PIC_Frames", &thr->basic.rawshift_pic_frames);
    get_keyword_value("Basic_Threshold", "RawShift_PIC_Threshold", &thr->basic.rawshift_pic_threshold);

    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Scan_Mode", &thr->basic.auxiliary_fre_noise_scan_mode);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Frames", &thr->basic.auxiliary_fre_noise_framenum);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre0", &thr->basic.auxiliary_fre_noise_test_fre0);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre0_Threshold", &thr->basic.auxiliary_fre_noise_test_fre0_threshold);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre1", &thr->basic.auxiliary_fre_noise_test_fre1);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre1_Threshold", &thr->basic.auxiliary_fre_noise_test_fre1_threshold);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre2", &thr->basic.auxiliary_fre_noise_test_fre2);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre2_Threshold", &thr->basic.auxiliary_fre_noise_test_fre2_threshold);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre3", &thr->basic.auxiliary_fre_noise_test_fre3);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre3_Threshold", &thr->basic.auxiliary_fre_noise_test_fre3_threshold);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre4", &thr->basic.auxiliary_fre_noise_test_fre4);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre4_Threshold", &thr->basic.auxiliary_fre_noise_test_fre4_threshold);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre5", &thr->basic.auxiliary_fre_noise_test_fre5);
    get_keyword_value("Basic_Threshold", "Auxiliary_Fre_Noise_Test_Fre5_Threshold", &thr->basic.auxiliary_fre_noise_test_fre5_threshold);

    /* --- FT5672 新增：Jump_Fre_Noise（字段名 [TODO-VERIFY]，键名/偏移见 recon §4） --- */
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Scan_Mode", &thr->basic.jump_fre_noise_scan_mode);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Frames", &thr->basic.jump_fre_noise_framenum);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre0", &thr->basic.jump_fre_noise_test_fre0);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre1", &thr->basic.jump_fre_noise_test_fre1);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre2", &thr->basic.jump_fre_noise_test_fre2);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre3", &thr->basic.jump_fre_noise_test_fre3);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre4", &thr->basic.jump_fre_noise_test_fre4);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre5", &thr->basic.jump_fre_noise_test_fre5);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre01_Threshold", &thr->basic.jump_fre_noise_test_fre01_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre02_Threshold", &thr->basic.jump_fre_noise_test_fre02_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre03_Threshold", &thr->basic.jump_fre_noise_test_fre03_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre04_Threshold", &thr->basic.jump_fre_noise_test_fre04_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre05_Threshold", &thr->basic.jump_fre_noise_test_fre05_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre12_Threshold", &thr->basic.jump_fre_noise_test_fre12_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre13_Threshold", &thr->basic.jump_fre_noise_test_fre13_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre14_Threshold", &thr->basic.jump_fre_noise_test_fre14_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre15_Threshold", &thr->basic.jump_fre_noise_test_fre15_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre23_Threshold", &thr->basic.jump_fre_noise_test_fre23_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre24_Threshold", &thr->basic.jump_fre_noise_test_fre24_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre25_Threshold", &thr->basic.jump_fre_noise_test_fre25_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre34_Threshold", &thr->basic.jump_fre_noise_test_fre34_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre35_Threshold", &thr->basic.jump_fre_noise_test_fre35_threshold);
    get_keyword_value("Basic_Threshold", "Jump_Fre_Noise_Test_Fre45_Threshold", &thr->basic.jump_fre_noise_test_fre45_threshold);

    return 0;
}

/*****************************************************************************
* 段尾注记（整合者必读）
*****************************************************************************
* 1) 对象初始化不放本段（三个分段拼接后统一放文件尾）：
*    struct test_funcs test_func_ft5672 = {
*        .ctype = {0x90, 0x92},          // blob .data+0x00 实测 u16[0]=144, u16[1]=146
*        .hwtype = IC_HW_MC_SC,          // .data+0x08 = 2
*        .startscan_mode = SCAN_NORMAL,  // .data+0x0c = 0
*        .key_num_total = 0,             // .data+0x10 = 0
*        .rawdata2_support = false,      // .data+0x14 = 0
*        .force_touch = false,           // .data+0x15 = 0
*        .mc_sc_short_v2 = true,         // .data+0x16 = 1
*        .raw_u16 = false,               // .data+0x17 = 0
*        .cb_high_support = true,        // .data+0x18 = 1
*        .param_update_support = true,   // .data+0x19 = 1
*        .param_init = param_init_ft5672,          // +0x20 -> 0x1d74c
*        .init = NULL,                             // +0x28 无重定位
*        .start_test = start_test_ft5672,          // +0x30 -> 0x1dc3c   (F2b)
*        .open_test = fts_open_test,               // +0x38 -> 0x240fc
*        .short_test = fts_short_test,             // +0x40 -> 0x242d4
*        .spi_test = fts_spi_test,                 // +0x48 -> 0x244ac
*        .data_dump = ft3658_data_dump,            // +0x50 -> 0x246bc
*        .save_data_private = save_data_ft5672,    // +0x58 -> 0x24780   (F2b)
*        .rawshift_fre_test = fts_rawshift_fre_test,           // +0x60 -> 0x27f8c (F2c)
*        .rawshift_pic_black_test = fts_rawshift_pic_black_test, // +0x68 -> 0x28680 (F2c)
*        .rawshift_pic_white_test = fts_rawshift_pic_white_test, // +0x70 -> 0x2877c (F2c)
*        .free_item_data = free_item_data,         // +0x78 -> 0x28878
*    };
* 2) 段内 #define FT5672_REG_* 仅为可读性；与活树 focaltech_test.h 同名宏等值时可直接替换。
* 3) 本段用到的“新字段”必须先在 focaltech_test.h 落补丁（recon §3 给出精确补丁）。
************************************************************************/

/* ==================================================================== *
 * compare_data_new —— blob 0x13578, 776B（.text GLOBAL FUNC）
 * 旧版 compare_data(0x132c0) 的 rodin 升级：多一个「失败记录缓冲」首参，
 * 主节点区每条失败记录还会往 out 里追加一条 {tx, rx, val, min, max}；
 * 阈值语义/遍历边界/打印串与旧版逐字一致（仅 __func__ 变为 compare_data_new）。
 *
 * 逐块地址：
 *   1359c/135a0 tdata = fts_ftest           135a4  !data -> err
 *   135a8       !tdata->node_valid -> err   135b0  node_num/key_num (+0x14/+0x18)
 *   135b4/135b8 w26=arg5(max_vk) w20=arg4(min_vk)
 *   135bc/135c0 x21=arg1(data) x25=arg0(out)
 *   135c4       node_va = node_num - key_num
 *   135c8/135d4 rx = tdata->node.rx_num (+0x10)，溢出到 [x29-4]
 *   135cc       out->fail_num = 0  ← 只在通过上面两条校验后执行
 *   135d0       node_va < 1 -> 0x136fc（跳过主循环）
 *   135dc/135e0 w23=arg3(max) w24=arg2(min)
 *   13600..1361c 主循环：node_valid[i]==0 continue；data[i]<min||>max 失败
 *   13620..1367c FTS_TEST_SAVE_ERR 的 snprintf 半（串 +0x6b78，上限 0x64000）
 *   13684..136bc FTS_TEST_SAVE_ERR 的 pr_err 半（串 +0x11a95，func +0xcade）
 *   136c0..136c8 n = out->fail_num；n >= 1500 -> brk #0x5512（UBSan，非源码逻辑）
 *   136cc..136f8 追加记录：rx=i/rx+1 -> +0x04、tx=i%rx+1 -> +0x08、
 *               val=data[i] -> +0x0c、min -> +0x10、max -> +0x14、fail_num++
 *   136fc..1370c key==0 或 node_va>=node_num -> 直接返回
 *   13710..137f4 键区循环（阈值 min_vk/max_vk）：**只打印，不追加记录**
 *   137f8..13850 错误路径：SAVE 24B "data/node_valid is null\n"（+0x8de5）
 *               + pr_err(+0x4e7a, func +0xcade)，返回 false
 * ==================================================================== */
bool compare_data_new(struct fts_test_fail_buf *out, int *data,
		      int min, int max, int min_vk, int max_vk, bool key)
{
	int i = 0;
	bool result = true;
	struct fts_test *tdata = fts_ftest;
	int rx = tdata->node.rx_num;
	int node_va = tdata->node.node_num - tdata->node.key_num;

	if (!data || !tdata->node_valid) {	/* 135a4 / 135a8 */
		FTS_TEST_SAVE_ERR("data/node_valid is null\n");
		return false;
	}

	out->fail_num = 0;			/* 135cc：str wzr,[x0] */

	for (i = 0; i < node_va; i++) {
		if (0 == tdata->node_valid[i])
			continue;

		if ((data[i] < min) || (data[i] > max)) {
			/* 串 = 旧版原生串，仅 __func__ 变 compare_data_new */
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
					  i / rx + 1, i % rx + 1,
					  data[i], min, max);
			/* 136c0..136f8：主节点区失败追加到 out
			 * （记录顺序 = 打印实参顺序：行= i/rx+1，列= i%rx+1） */
			out->node[out->fail_num].tx = i / rx + 1;
			out->node[out->fail_num].rx = i % rx + 1;
			out->node[out->fail_num].val = data[i];
			out->node[out->fail_num].min = min;
			out->node[out->fail_num].max = max;
			out->fail_num++;
			result = false;
		}
	}

	if (key) {
		for (i = node_va; i < tdata->node.node_num; i++) {
			if (0 == tdata->node_valid[i])
				continue;

			if ((data[i] < min_vk) || (data[i] > max_vk)) {
				/* 键区失败只打印：blob 该路径无 umaddl/0x5dc 记录块 */
				FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
						  i / rx + 1, i % rx + 1,
						  data[i], min_vk, max_vk);
				result = false;
			}
		}
	}

	return result;
}

/* ==================================================================== *
 * compare_array_new —— blob 0x13884, 572B（.text GLOBAL FUNC）
 * 旧版 compare_array(0x13ac4) 的 rodin 升级：多一个「失败记录缓冲」首参，
 * 每条失败记录追加 {tx, rx, val, min[i], max[i]}；其余（key 决定是否
 * 扣除键区、遍历边界、打印串）与旧版逐字一致。
 *
 * 逐块地址：
 *   138a8/138ac tdata = fts_ftest
 *   138b0       !data(arg1) -> err        138b4 !min(arg2) -> err
 *   138bc       !max(arg3) -> err         138c4 !tdata->node_valid -> err
 *               （arg0 = out 不校验：blob 无 cbz x0）
 *   138cc       node_num = tdata->node.node_num (+0x14)
 *   138d0/138d4 x21=arg1(data) x22=arg0(out)
 *   138d8/138e0 if (!key) node_num -= node.key_num (+0x18)
 *   138e4       rx = node.rx_num (+0x10) -> [x29-4]
 *   138ec       out->fail_num = 0
 *   13914..1393c 循环：node_valid[i]==0 continue；data[i]<min[i]||>max[i] 失败
 *   13940..139a4 SAVE snprintf 半（串 +0x6b78，args i/rx+1, i%rx+1, data[i], min[i], max[i]）
 *   139b0..139e8 pr_err 半（串 +0x11a95，func +0xeec = "compare_array_new"）
 *   139ec..13a28 追加记录（n>=1500 -> brk #0x5512）：rx=i/rx+1 -> +0x04、
 *               tx=i%rx+1 -> +0x08、val=data[i] -> +0x0c、min[i] -> +0x10、
 *               max[i] -> +0x14、fail_num++，result=false
 *   13a34..13a8c 错误路径：SAVE 32B "data/min/max/node_valid is null\n"（+0xc2a1）
 *               + pr_err(+0x70ba, func +0xeec)，返回 false
 * ==================================================================== */
bool compare_array_new(struct fts_test_fail_buf *out, int *data,
		       int *min, int *max, bool key)
{
	int i = 0;
	bool result = true;
	struct fts_test *tdata = fts_ftest;
	int rx = tdata->node.rx_num;
	int node_num = tdata->node.node_num;

	if (!data || !min || !max || !tdata->node_valid) {
		FTS_TEST_SAVE_ERR("data/min/max/node_valid is null\n");
		return false;
	}

	if (!key)
		node_num -= tdata->node.key_num;

	out->fail_num = 0;			/* 138ec：str wzr,[x22] */

	for (i = 0; i < node_num; i++) {
		if (0 == tdata->node_valid[i])
			continue;

		if ((data[i] < min[i]) || (data[i] > max[i])) {
			FTS_TEST_SAVE_ERR("test fail,node(%4d,%4d)=%5d,range=(%5d,%5d)\n",
					  i / rx + 1, i % rx + 1,
					  data[i], min[i], max[i]);
			/* 139ec..13a28：失败追加到 out */
			out->node[out->fail_num].tx = i / rx + 1;
			out->node[out->fail_num].rx = i % rx + 1;
			out->node[out->fail_num].val = data[i];
			out->node[out->fail_num].min = min[i];
			out->node[out->fail_num].max = max[i];
			out->fail_num++;
			result = false;
		}
	}

	return result;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * focaltech_test_ft5672_seg2.c — rodin FT5672 工厂测试套件【分段 2/骨架巨头】重建
 *
 * 覆盖 blob 符号：
 *   start_test_ft5672  LOCAL FUNC .text 0x1dc3c size 25788 (0x64bc)
 *   save_data_ft5672   LOCAL FUNC .text 0x24780 size 14344 (0x3808)
 *
 * 证据来源（唯一 ground truth = 小米官方 GPL blob 机器码）：
 *   ko/vendor_dlkm/lib/modules/focaltech_touch_rodin.ko
 *   反汇编稿 tools/_b567_touch/blob/focaltech_touch_rodin.disr（llvm-objdump -d -r）
 *   字符串   tools/_b567_touch/blob/focaltech_touch_rodin.rostr.bin
 *   符号表   tools/_b567_touch/blob/focaltech_touch_rodin.symtab
 *   donor   focaltech_test_ft3383.c（内含 ft5572 套件）作骨架对照
 *   分析文档 tools/_b570_a70/ft5672_main_recon.md（逐块地址证据）
 *
 * ★ 重要结构事实（反汇编推断，见 recon §1）：
 *   GCC 把「只有一个调用点的 static 函数」内联并删除了 out-of-line 副本，
 *   因此 start_test_ft5672 这 25KB 里 *内联* 了下列测试项函数体（blob 中无独立符号）：
 *     ft5672_rawdata_test / ft5672_uniformity_test / ft5672_scap_cb_test /
 *     ft5672_scap_rawdata_test / ft5672_noise_test /
 *     ft5672_auxiliary_freq_noise_test / ft5672_jump_freq_noise_test /
 *     ft5672_ex_rst_test / ft5672_rst_test / ft5672_read_lockdown / scap_cb_ccbypass
 *   仅 ft5672_short_test(0x28aa4) 与 ft5672_panel_differ_test(0x29188) 因有 2 个调用点
 *   保留了 out-of-line 副本，由本函数以 bl 调用。
 *   为保持与原始源码同构，本文件把被内联的测试项**还原为独立 static 函数**
 *   （原源码即如此书写；GCC 的内联是可重放的编译行为）。
 *
 * ★ 本文件只写 start_test_ft5672 及其内联单元 + save_data_ft5672。
 *   其它分段（param_init_ft5672 / ft5672_short_test / get_cb_ft5672 / get_noise_ft5672 /
 *   ft5672_panel_differ_test / rawshift 三件套 / fts_open_test / fts_short_test /
 *   fts_spi_test / ft3658_data_dump / ft3658_get_rawdata / free_item_data /
 *   malloc_item_data / get_null_noise / compare_*_new）由其它代理产出，同文件 static 可见。
 */


/* ------------------------------------------------------------------ *
 * 本段用到的 blob 结构体偏移（全部由机器码反推；见 recon §6 偏移表）
 * 这里用注释钉住，字段名沿用 donor（focaltech_test.h），结构体定义由框架代理维护。
 * ------------------------------------------------------------------ *
 *  struct fts_test（blob 实测偏移，donor 名）：
 *    +0x08 node            struct fts_test_node {channel=+0, tx=+4, rx=+8, node=+0xc, key=+0x10}
 *                          （同域内偏移：rx_num=+0x10, node_num=+0x14, key_num=+0x18）
 *    +0x1c sc_node         同布局（sc tx_num=+0x20, rx_num=+0x24, node_num=+0x28）
 *    +0x30 fw_ver  +0x31 va_touch_thr +0x32 vk_touch_thr +0x33 key_support
 *    +0x34 v3_pattern +0x35 mapping +0x36 normalize +0x37 fre_num
 *    +0x40 item1_data +0x48 item2_data +0x50 item3_data +0x58 item4_data
 *    +0x60 item5_data +0x68 item6_data +0x70 item7_data
 *    +0x90 buffer  +0x98 buffer_length  +0xa0 node_valid  (+0xa8 node_valid_sc[TODO-VERIFY])
 *    +0xc0 csv_item_cnt  +0xc4 csv_item_sraw  +0xc8 csv_item_scb  +0xd4 csv_item_af_noise
 *    +0x35* mapping；+0xbe0 testresult  +0xbe8 testresult_len
 *    +0x12bc item_fail_flag（int 位掩码，0=全过；本文件 start_test 写、save_data 读）
 *    +0x12c0 struct fts_test_fail_buf fail[..] 起始（元素 30004B，见 compare 契约）
 *    +0x1130 ini（ic_name 在 ini+0）；+0x1158 lockdown_info[8]
 *    thr = &tdata->ic.mc_sc.thr（ic 起 +0xbc），本段用到的 thr 字段偏移见 recon §6
 */

/* ================================================================== *
 * 1. ft5672_read_lockdown —— blob 内联于 start_test_ft5672（无独立符号）
 *    证据：blob 0x21530-0x21960（3 次重试的 0x90 bootid 读 + 0xAC 取 8B lockdown）
 *          "ft5672_read_lockdown" 串 @ .rodata.str1.1+0x7f6d (0x21630 起引用)
 *    [TODO-VERIFY] 本函数是否原为 static 或在别的 .c（名字串存在于 start_test 内）
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_read_lockdown(void)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;
	u8 regval[32] = { 0 };
	u8 i = 0;

	FTS_TEST_FUNC_ENTER();

	for (i = 0; i < 3; i++) {
		ret = fts_test_write_reg(0xfc, 0xaa);		/* 0x21534/0x2158c/0x215dc */
		msleep(5);					/* 0x21544 */
		ret = fts_test_write_reg(0xfc, 0x55);		/* 0x21550 */
		msleep(80);					/* 0x21558 */
		ret = fts_test_read(0x90, regval, 2);		/* 0x21568 / 0x215b8 / 0x21608 */
		if (regval[0] == 0x00 || regval[0] == 0xef || regval[0] == 0xff)
			break;			/* 0x21570-0x21580 → 继续重试或落 0x217b8 */
	}
	FTS_TEST_INFO("read boot id: val[0x%x%x]", regval[0], regval[1]);	/* 0x21624-0x21638 */

	/* 设置读地址 0xAC，写 {0x01,0xf8,0x00}（sturh 0xf801 + sturb 0） */
	regval[0] = 0x01;					/* sturh w8=0xf801 */
	regval[1] = 0xf8;
	regval[2] = 0x00;
	ret = fts_test_write(0xac, regval, 3);			/* 0x21654 */
	if (ret < 0) {
		FTS_TEST_ERROR("set read addr fail!!!");	/* 0x2165c；_b582-INTB：blob E 族无 append */
		goto read_0x90;					/* → 0x21714/0x21760/0x217ac */
	}
	msleep(5);						/* 0x216f4/0x218a8/0x218d0 */
	ret = fts_test_read(0x03, regval, 8);			/* 0x21704 */
	if (ret < 0) {
		FTS_TEST_ERROR("read lockdown info fail!!!");	/* 0x2170c；_b582-INTB：blob E 族无 append */
		goto read_0x90;
	}
	memcpy(&tdata->lockdown_info[0], regval, 8);		/* 0x218fc: str x8,[x20,#0x1158] */
	FTS_TEST_SAVE_INFO("------ read lockdown PASS\n\n");	/* 0x21928 */
	return 0;

read_0x90:
	/* 0x21720/0x2176c/0x2176c 三处同构：reset(0xfc) 后重读 0x90 */
	fts_test_write_reg(0xfc, 0xaa);
	msleep(5);
	fts_test_write_reg(0xfc, 0x55);
	msleep(80);
	fts_test_read(0x90, regval, 2);
	FTS_TEST_ERROR("read 0x90 fail!!!");		/* 0x21758/0x217a4；_b582-INTB：blob E 族无 append */
	return -EIO;
}

/* ================================================================== *
 * 2. ft5672_rst_test / ft5672_ex_rst_test
 *    blob 内联，热块 0x211e0-0x21334，冷块 0x21964-0x21a6c（value 比较）
 *    证据：0x21244 "ft5672_ex_rst_test"、0x214cc "ft5672_rst_test"、0x21510 尾块
 * ================================================================== */
static __attribute__((always_inline)) inline bool ft5672_ex_rst_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	u8 value_1 = 0;
	u8 value_2 = 0;
	u8 value_3 = 0;
	int retry = 25;			/* 0x21350: mov w21,#0x19 */
	bool tmp_result = false;

	FTS_TEST_FUNC_ENTER();
	FTS_TEST_SAVE_INFO("\n============ Test Item: RST Test\n");	/* 0x21200 */
	enter_work_mode();						/* 0x21250 */

	ret = fts_test_read_reg(0xad, &value_1);			/* 0x2125c */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("value_1 read reg 0xad fail,ret=%d\n", ret);/* 0x2128c */
		tmp_result = false;
		goto out;
	}
	ret = fts_test_write_reg(0xad, (u8)(value_1 + 1));		/* 0x212c8 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("write reg 0xad fail,ret=%d\n", ret);	/* 0x212f8 */
		tmp_result = false;
		goto out;
	}

	fts_reset_proc(50);						/* 0x21340/0x21344 */
	msleep(50);							/* 0x21348 */

	/* 0x21358-0x21394：最多 25 次，读 0x9f 判 0x83/0x72 决定是否进 factory */
	do {
		ret = enter_factory_mode();				/* 0x21358 */
		if (ret == 0)
			break;
		ret = fts_test_read_reg(0x9f, &value_2);		/* 0x21370 */
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("value_2 read reg 0x9f fail,ret=%d\n", ret);/*0x213c0*/
			tmp_result = false;
			goto out;
		}
		msleep(20);						/* 0x2137c */
	} while ((value_2 == 0x83 || value_2 == 0x72) && --retry);	/* 0x21384-0x21394 */

	enter_work_mode();						/* 0x213f0 */
	ret = fts_test_read_reg(0xad, &value_3);			/* 0x213fc */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("after read reg 0xad fail,ret=%d\n", ret);/* 0x2142c */
		tmp_result = false;
		goto out;
	}
	FTS_TEST_INFO("value_1 = %x, value_3 = %x\n", value_1, value_3);	/* 0x21964-0x2197c */
	if (value_1 == value_3) {					/* 0x21988 cmp */
		tmp_result = true;
		fts_test_write_reg(0xad, value_1);			/* 0x21990/0x21998 */
	} else {
		tmp_result = false;
		FTS_TEST_SAVE_ERR("value_befor = %x, value_after = %x\n",
				  value_1, value_3);			/* 0x21a28 */
		/* 0x21a6c: b 0x21468 → 走 test_err 尾 */
		*test_result = false;
		FTS_TEST_SAVE_ERR("------ RST test NG\n");		/* 0x21494 + 0x214d4 */
		return tmp_result;
	}
	return tmp_result;

out:
	if (tmp_result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------ RST test PASS\n");		/* 0x219e4 */
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("------ RST test NG\n");		/* 0x21494 */
	}
	FTS_TEST_FUNC_EXIT();
	return tmp_result;
}

static int ft5672_rst_test(struct fts_test *tdata, bool *test_result)
{
	bool tmp_result = false;
	int ret = 0;

	ret = ft5672_ex_rst_test(tdata, &tmp_result);
	/* 0x214d4 之后即 0x214e4 下一位图判定；此处的收尾 printk 已在 ex 内 */
	*test_result = tmp_result;
	fts_test_write_reg(0x54, tdata->mapping);			/* 见 recon §5 尾块 */
	return ret;
}

/* ================================================================== *
 * 3. scap_cb_ccbypass —— donor 同名函数（focaltech_test_ft3383.c:115）
 *    blob 内联：ENTER 串 @0x1ed2c；Exit 串 @0x240d4（region B 冷块）
 * ================================================================== */
static __attribute__((always_inline)) inline int scap_cb_ccbypass(struct fts_test *tdata, int *scap_cb, bool *result)
{
	int ret = 0;
	int i = 0;
	u8 sc_cb[4] = { 0 };
	u8 sc_mode = 0;
	int gcb_tx = 0;
	int gcb_rx = 0;
	int *cb_on = NULL;
	int *cb_off = NULL;
	int *cb_hi = NULL;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	*result = false;
	FTS_TEST_FUNC_ENTER();
	if (!scap_cb) {
		FTS_TEST_SAVE_ERR("scap_cb fails");			/* 0x1e2c0 */
		return -EINVAL;
	}
	if (!thr->scap_cb_on_min || !thr->scap_cb_on_max ||
	    !thr->scap_cb_off_min || !thr->scap_cb_off_max) {
		FTS_TEST_SAVE_ERR("scap_cb_on/off/hi_min/max is null\n");/* 0x1ed9c */
		return -EINVAL;
	}
	ret = fts_test_read_reg(0x59, &sc_cb[0]);			/* [TODO-VERIFY] 0x59 位置 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x59 fail,ret=%d\n", ret);
		return ret;
	}
	ret = enter_factory_mode();					/* 0x1e308 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);/* 0x1e338 */
		return ret;
	}
	ret = mapping_switch(NO_MAPPING);				/* 0x1e36c */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("switch no-mapping fail,ret=%d\n", ret);/* 0x1e39c */
		return ret;
	}
	ret = fts_test_read_reg(0x44, &sc_mode);			/* 0x1e450 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read sc_mode fail,ret=%d\n", ret);	/* 0x1e480 */
		return ret;
	}
	/* 三次空扫丢帧 */
	for (i = 0; i < 3; i++) {
		ret = start_scan();
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan fail\n");		/* 0x202d0 */
			goto restore;
		}
	}
	/* water proof on / off / high 三段，逐段 get_cb_mc_sc + compare
	 * 证据串：0x206dc "scap_cb in waterproof on mode:" 0x2372c "...off mode:"
	 *         0x23d04 "...high mode:" 0x23230/0x1f2f0(GCB) 等
	 *         get_cb_ft5672 调用点 0x20678 / 0x236c8 / 0x23c54
	 *         compare_array_new 调用点 0x29c7c（在 ft5672_panel_differ_test 内，不属本段）
	 * [TODO-VERIFY] 三段内部逐项比较细节（gcb_tx / gcb_rx 串已落实）
	 */
	ret = get_cb_ft5672(scap_cb, tdata->sc_node.node_num);/* 0x20678 */
	/* 实证：x0=scap_cb(x20), w1=w21=sc_node.node_num(0x1ed28 `ldr w21,[x19,#0x28]`)；
	 * w2(is_cf) 未在调用点附近设置 → 形参待 get_cb_ft5672 分段定案 [TODO-VERIFY] */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read sc_cb fail,ret=%d\n", ret);	/* [TODO-VERIFY] 串 */
		goto restore;
	}
	show_data(scap_cb, false);
	FTS_TEST_INFO("GCB RX:%d,TX:%d\n", gcb_rx, gcb_tx);		/* 串 0x??；_b582-INTB：blob 含尾 \n */
	*result = true;

restore:
	ret = fts_test_write_reg(0x44, sc_mode);			/* 恢复 sc mode */
	if (ret < 0)
		FTS_TEST_SAVE_ERR("restore sc mode fail,ret=%d\n", ret);
	fts_test_write_reg(0x59, 0x00);					/* set 0x59 to 0x00 */
	wait_state_update(TEST_RETVAL_AA);
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 4. ft5672_rawdata_test —— blob 内联，热 0x1ddbc-0x1df58 / 续 0x1e3cc-0x1e444
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_rawdata_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0, j = 0;
	int *temp_rawdata = NULL;
	int *rawdata = NULL;
	u8 fre = 0;
	u8 data_type = 0;
	bool result = false;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	FTS_TEST_FUNC_ENTER();						/* 0x1ddbc */
	FTS_TEST_SAVE_INFO("\n============ Test Item: rawdata test\n");/* 0x1ddec */
	rawdata = tdata->item1_data;					/* 0x1de3c +0x40 */
	tdata->csv_item_cnt++;						/* 0x1de38-0x1de44 +0xc0 */

	if (!rawdata || !thr->rawdata_h_min || !thr->rawdata_h_max) {	/* 0x1de48-58 */
		FTS_TEST_SAVE_ERR("rawdata_h_min/max is null\n");	/* 0x1de78 */
		ret = -EINVAL;						/* w21=-0x16 */
		goto test_err;
	}
	temp_rawdata = fts_malloc(tdata->node.node_num * sizeof(int));	/* 0x1e3cc */
	if (!temp_rawdata) {
		FTS_TEST_SAVE_ERR("memory temp_rawdata malloc fails");	/* 0x1e3f8 */
		ret = -ENOMEM;
		goto test_err;
	}
	ret = enter_factory_mode();					/* [TODO-VERIFY] 落点 0x1ecc4 段 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("failed to enter factory mode,ret=%d\n", ret);
		goto test_err;
	}
	ret = mapping_switch(MAPPING);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("switch mapping fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_FRE_LIST, &fre);		/* 0x0A */
	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x0A fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &data_type);	/* 0x06 */
	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x06 fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_write_reg(FACTORY_REG_FRE_LIST, 0x81);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set frequecy fail,ret=%d\n", ret);
		goto restore_reg;
	}
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n", ret);
		goto restore_reg;
	}
	ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, 0);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set data_sel fail,ret=%d\n", ret);
		goto restore_reg;
	}
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto restore_reg;
	}
	ret = fts_test_write_reg(FACTORY_REG_DATA_TYPE, 0x01);
	if (ret) {
		FTS_TEST_SAVE_ERR("set data type fail,ret=%d\n");
		goto restore_reg;
	}
	for (i = 0; i < 3; i++) {
		ret = start_scan();
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan fail\n");
			goto restore_reg;
		}
	}
	for (i = 0; i < 3; i++) {
		ret = get_rawdata(temp_rawdata);
		for (j = 0; j < tdata->node.node_num; j++)
			rawdata[j] += temp_rawdata[j];
	}
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get rawdata fail,ret=%d\n", ret);
		goto restore_reg;
	}
	for (j = 0; j < tdata->node.node_num; j++)
		rawdata[j] = (rawdata[j] / 3);
	show_data(rawdata, false);					/* bl show_data 0x1f314? */
	/* ★ blob: 走 compare_array_new（失败缓冲 = fail[1]） */
	result = compare_array_new(&tdata->fail_buf[0], rawdata,
				   thr->rawdata_h_min, thr->rawdata_h_max, false);/* 0x1f5ac */

restore_reg:
	ret = fts_test_write_reg(FACTORY_REG_DATA_TYPE, 0);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("restore 0x5B fail,ret=%d\n", ret);
	ret = fts_test_write_reg(FACTORY_REG_FRE_LIST, fre);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("restore reg0A fail,ret=%d\n", ret);
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("wait state update fail\n");
	ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, data_type);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("restore data_sel fail,ret=%d\n", ret);
	wait_state_update(TEST_RETVAL_AA);

test_err:
	fts_free(temp_rawdata);
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------ rawdata test PASS\n");	/* 0x205e0 */
	} else {
		*test_result = false;
		FTS_TEST_SAVE_INFO("------ rawdata test NG\n");	/* 0x1df08 */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 5. ft5672_uniformity_test —— blob 内联，热 0x1df78-0x1e204
 *    计算体 0x1ef90-0x1fa94（region A 尾部），compare_array_new @0x1f5ac/0x1f7d8
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_uniformity_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0, row = 0, col = 1, i = 0;
	int deviation = 0, max = 0, min = 0, uniform = 0;
	int *rawdata = NULL;
	int *rawdata_linearity = NULL;
	int *rl_tmp = NULL;
	int rl_cnt = 0;
	int offset = 0, offset2 = 0;
	int tx_num = 0, rx_num = 0;
	struct mc_sc_threshold *thr = &fts_ftest->ic.mc_sc.thr;
	bool result = false, result2 = false, result3 = false;

	FTS_TEST_FUNC_ENTER();						/* 0x1df78 */
	FTS_TEST_SAVE_INFO("\n============ Test Item: rawdata unfiormity test\n");/*0x1dfa0*/
	memset(tdata->buffer, 0, tdata->buffer_length);			/* 0x1e078 (+0x90,+0x98) */
	rawdata = tdata->item1_data;					/* 0x1e07c */
	tx_num = tdata->node.tx_num;
	rx_num = tdata->node.rx_num;

	rawdata_linearity = tdata->item2_data;				/* 0x1e07c +0x48 */
	if (!rawdata_linearity) {
		FTS_TEST_SAVE_ERR("rawdata_linearity buffer fail");	/* 0x1e0a0 */
		ret = -ENOMEM;						/* w20=-0xc */
		goto test_err;
	}
	if (!thr->tx_linearity_max || !thr->rx_linearity_max ||
	    !tdata->node_valid) {					/* 0x1e0f0/0x1e0f8/0x1e100 */
		FTS_TEST_SAVE_ERR("tx/rx_lmax/node_valid is null\n");	/* 0x1e124 */
		ret = -EINVAL;						/* w20=-0x16 */
		goto test_err;
	}
	print_buffer(rawdata, tdata->node.node_num, tdata->node.rx_num);

	result = true;
	if (thr->basic.uniformity_check_tx) {				/* +0x108 */
		FTS_TEST_SAVE_INFO("Check Tx Linearity\n");		/* 0x1f2ec */
		tdata->csv_item_cnt++;
		rl_tmp = rawdata_linearity + rl_cnt;
		for (row = 0; row < tx_num; row++) {
			for (col = 1; col < rx_num; col++) {
				offset = row * rx_num + col;
				offset2 = row * rx_num + col - 1;
				deviation = abs(rawdata[offset] - rawdata[offset2]);
				max = (rawdata[offset] > rawdata[offset2]) ?
				      rawdata[offset] : rawdata[offset2];
				max = max ? max : 1;
				rl_tmp[offset] = 100 * deviation / max;
			}
		}
		FTS_TEST_SAVE_INFO(" Tx Linearity:\n");		/* 0x1f510 */
		show_data(rl_tmp, false);
		FTS_TEST_SAVE_INFO("\n");
		/* 失败缓冲 = fail[2] */
		result = compare_array_new(&tdata->fail_buf[1], rl_tmp,
				thr->tx_linearity_min, thr->tx_linearity_max, false);/*0x1f5ac*/
		rl_cnt += tdata->node.node_num;
	}

	result2 = true;
	if (thr->basic.uniformity_check_rx) {				/* +0x10c */
		FTS_TEST_SAVE_INFO("Check Rx Linearity\n");		/* 0x1f64c */
		tdata->csv_item_cnt++;
		rl_tmp = rawdata_linearity + rl_cnt;
		for (row = 1; row < tx_num; row++) {
			for (col = 0; col < rx_num; col++) {
				offset = row * rx_num + col;
				offset2 = (row - 1) * rx_num + col;
				deviation = abs(rawdata[offset] - rawdata[offset2]);
				max = (rawdata[offset] > rawdata[offset2]) ?
				      rawdata[offset] : rawdata[offset2];
				max = max ? max : 1;
				rl_tmp[offset] = 100 * deviation / max;
			}
		}
		FTS_TEST_SAVE_INFO("Rx Linearity:\n");			/* 0x1f738 */
		show_data(rl_tmp, false);
		FTS_TEST_SAVE_INFO("\n");
		result2 = compare_array_new(&tdata->fail_buf[2], rl_tmp,
				thr->rx_linearity_min, thr->rx_linearity_max, false);/*0x1f7d8*/
		rl_cnt += tdata->node.node_num;
	}

	result3 = true;
	if (thr->basic.uniformity_check_min_max) {
		FTS_TEST_SAVE_INFO("Check Min/Max\n");			/* 0x1f870 */
		min = 100000;
		max = -100000;
		for (i = 0; i < tdata->node.node_num; i++) {
			if (0 == tdata->node_valid[i])
				continue;
			min = (min < rawdata[i]) ? min : rawdata[i];
			max = (max > rawdata[i]) ? max : rawdata[i];
		}
		max = !max ? 1 : max;
		uniform = 100 * abs(min) / abs(max);
		FTS_TEST_SAVE_INFO("min:%d, max:%d, get value of min/max:%d\n",
				   min, max, uniform);			/* 串 @0x1f8c? */
		if (uniform < thr->basic.uniformity_min_max_hole) {
			result3 = false;
			FTS_TEST_SAVE_ERR("min_max out of range, set value: %d\n",
					  thr->basic.uniformity_min_max_hole);
		}
	}

test_err:
	if (result && result2 && result3) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("uniformity test is OK\n");		/* 0x1fdb0 */
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("uniformity test is NG\n");		/* 0x1e190 */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 6. ft5672_scap_cb_test —— blob 内联，热 0x1e224-0x1e558
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_scap_cb_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	u8 sc_mode = 0;
	u8 sc_cb[4] = { 0 };
	bool tmp_result = false;
	int *scap_cb = NULL;
	int i = 0;

	FTS_TEST_FUNC_ENTER();						/* 0x1e224 */
	FTS_TEST_SAVE_INFO("\n============ Test Item: Scap CB Test\n");/* 0x1e250 */
	scap_cb = tdata->item3_data;					/* 0x1e29c +0x50 */
	if (!scap_cb) {
		FTS_TEST_SAVE_ERR("scap_cb fails");			/* 0x1e2c0 */
		ret = -EINVAL;
		goto test_err;
	}
	ret = enter_factory_mode();					/* 0x1e308 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);/* 0x1e338 */
		tmp_result = false;
		goto test_err;
	}
	ret = mapping_switch(NO_MAPPING);				/* 0x1e36c */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("switch no-mapping fail,ret=%d\n", ret);/* 0x1e39c */
		tmp_result = false;
		goto test_err;
	}
	ret = fts_test_read_reg(0x44, &sc_mode);			/* 0x1e450 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read sc_mode fail,ret=%d\n", ret);	/* 0x1e480 */
		tmp_result = false;
		goto test_err;
	}
	for (i = 0; i < 3; i++) {
		ret = start_scan();
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan fail\n");
			tmp_result = false;
			goto test_err;
		}
	}
	ret = scap_cb_ccbypass(tdata, scap_cb, &tmp_result);		/* 0x1ed2c 内联 */
	if (ret < 0)
		FTS_TEST_SAVE_ERR("scap_cb fail,ret:%d", ret);
	ret = fts_test_write_reg(0x44, sc_mode);
	if (ret) {
		FTS_TEST_SAVE_ERR("restore sc mode fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("wait state update fail\n");

test_err:
	if (tmp_result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("\n------ scap cb test PASS\n");	/* 0x1fa90 */
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("\n------ scap cb test NG\n");	/* 0x1e4dc */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 7. ft5672_scap_rawdata_test —— blob 内联，热 0x1e578-0x1e790
 *    主体 0x1efc0-0x206xx，get_cb_ft5672 @0x20678 属 ccbypass 交叉段
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_scap_rawdata_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0;
	bool tmp_result = false;
	bool tmp2_result = false;
	bool fw_wp_check = false;
	bool tx_check = false;
	bool rx_check = false;
	int *scap_rawdata = NULL;
	int *srawdata_tmp = NULL;
	int srawdata_cnt = 0;
	u8 wc_sel = 0, hc_sel = 0, data_type = 0;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	FTS_TEST_FUNC_ENTER();						/* 0x1e578 */
	FTS_TEST_SAVE_INFO("\n============ Test Item: Scap Rawdata Test\n");/*0x1e5a8*/
	scap_rawdata = tdata->item4_data;				/* 0x1e600 +0x58 */
	if (!scap_rawdata) {
		FTS_TEST_SAVE_ERR("scap rawdata fails");		/* 0x1e624 */
		ret = -EINVAL;
		goto test_err;
	}
	/* ★ blob 只校验 4 个指针（on_min/on_max/off_min/off_max），
	 *   donor 校验 6 个（含 hi_min/hi_max）——此差异为实证。 */
	if (!thr->scap_rawdata_on_min || !thr->scap_rawdata_on_max ||	/* 0x330/0x338 */
	    !thr->scap_rawdata_off_min || !thr->scap_rawdata_off_max) {	/* 0x320/0x328 */
		FTS_TEST_SAVE_ERR("scap_rawdata_on/off/hi_min/max is null\n");/*0x1e69c*/
		ret = -EINVAL;
		goto test_err;
	}
	ret = enter_factory_mode();					/* [落点 0x1efc0] */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("enter factory mode fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = mapping_switch(NO_MAPPING);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("switch no-mapping fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_WC_SEL, &wc_sel);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read water_channel_sel fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_HC_SEL, &hc_sel);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read high_channel_sel fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_DATA_TYPE, &data_type);
	if (ret) {
		FTS_TEST_SAVE_ERR("read 0x59 fail,ret=%d\n", ret);	/* [TODO-VERIFY] */
		goto test_err;
	}
	ret = fts_test_write_reg(FACTORY_REG_DATA_TYPE, 0x01);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set raw type fail,ret=%d\n", ret);
		goto restore_reg;
	}
	for (i = 0; i < 3; i++) {
		ret = start_scan();
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("scan scap rawdata fail\n");	/* 0x1ff3c */
			goto restore_reg;
		}
	}
	ret = start_scan();
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("scan scap rawdata(2) fail\n");	/* 0x20170 */
		goto restore_reg;
	}
	/* --- water proof on --- */
	tmp_result = true;
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_ON);
	if (thr->basic.scap_rawdata_wp_on_check && fw_wp_check) {
		tdata->csv_item_cnt++;
		tdata->csv_item_sraw |= 0x01;				/* +0xc4 */
		srawdata_tmp = scap_rawdata + srawdata_cnt;
		ret = get_rawdata_mc_sc(WATER_PROOF_ON, srawdata_tmp);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("get scap(WP_ON) rawdata fail\n");/*0x20590*/
			goto restore_reg;
		}
		FTS_TEST_SAVE_INFO("scap_rawdata in waterproof on mode:\n");
		show_data_mc_sc(srawdata_tmp);
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_ON_RX);
		for (i = 0; i < tdata->sc_node.node_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;
			if ((rx_check && (i < tdata->sc_node.rx_num)) ||
			    (tx_check && (i >= tdata->sc_node.rx_num))) {
				if ((srawdata_tmp[i] < thr->scap_rawdata_on_min[i]) ||
				    (srawdata_tmp[i] > thr->scap_rawdata_on_max[i])) {
					FTS_TEST_SAVE_ERR("test fail,CH%d=%5d,range=(%5d,%5d)\n",
						i + 1, srawdata_tmp[i],
						thr->scap_rawdata_on_min[i],
						thr->scap_rawdata_on_max[i]);/*0x21fe4*/
					tmp_result = false;
				}
			}
		}
		srawdata_cnt += tdata->sc_node.node_num;
	}
	/* --- water proof off --- */
	tmp2_result = true;
	fw_wp_check = get_fw_wp(wc_sel, WATER_PROOF_OFF);
	if (thr->basic.scap_rawdata_wp_off_check && fw_wp_check) {
		tdata->csv_item_cnt++;
		tdata->csv_item_sraw |= 0x02;
		srawdata_tmp = scap_rawdata + srawdata_cnt;
		ret = get_rawdata_mc_sc(WATER_PROOF_OFF, srawdata_tmp);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("get scap(WP_OFF) rawdata fail\n");
			goto restore_reg;
		}
		FTS_TEST_SAVE_INFO("scap_rawdata in waterproof off mode:\n");
		show_data_mc_sc(srawdata_tmp);
		tx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_TX);
		rx_check = get_fw_wp(wc_sel, WATER_PROOF_OFF_RX);
		for (i = 0; i < tdata->sc_node.node_num; i++) {
			if (0 == tdata->node_valid_sc[i])
				continue;
			if ((rx_check && (i < tdata->sc_node.rx_num)) ||
			    (tx_check && (i >= tdata->sc_node.rx_num))) {
				if ((srawdata_tmp[i] < thr->scap_rawdata_off_min[i]) ||
				    (srawdata_tmp[i] > thr->scap_rawdata_off_max[i])) {
					FTS_TEST_SAVE_ERR("test fail,CH%d=%5d,range=(%5d,%5d)\n",
						i + 1, srawdata_tmp[i],
						thr->scap_rawdata_off_min[i],
						thr->scap_rawdata_off_max[i]);
					tmp2_result = false;
				}
			}
		}
		srawdata_cnt += tdata->sc_node.node_num;
	}

restore_reg:
	ret = fts_test_write_reg(FACTORY_REG_DATA_TYPE, data_type);
	if (ret < 0)
		FTS_TEST_SAVE_ERR("restore 0x0A fail,ret=%d\n", ret);	/* [串见 0x21dcc] */

test_err:
	if (tmp_result && tmp2_result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("\n------ scap rawdata test PASS\n");
	} else {
		*test_result = false;
		FTS_TEST_SAVE_INFO("\n------ scap rawdata test NG\n");	/* 0x1e714 */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 8. ft5672_noise_test —— blob 内联，热 0x1e7ec-0x1e9a4
 *    主体（含 get_noise_ft5672 @0x223d4/0x226a0/0x22d24、compare_data_new
 *    @0x22414/0x2274c、get_null_noise @0x23218、compare_array_new @0x23174/0x23208）
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_noise_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0, j = 0;
	int *noise = NULL;
	bool result = false;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	FTS_TEST_FUNC_ENTER();						/* 0x1e7ec */
	FTS_TEST_SAVE_INFO("\n============ Test Item: Noise Test\n");	/* 0x1e82c */
	noise = tdata->item6_data;					/* +0x68 */
	tdata->csv_item_cnt++;						/* 0x1e880-0x1e884 */
	if (!noise || !thr->noise_min || !thr->noise_max) {		/* 0x370/0x378 */
		FTS_TEST_SAVE_ERR("noise/noise_min/noise_max is null\n");/* 0x1e8bc */
		ret = -EINVAL;
		goto test_err;
	}
	/* ★ blob 主路径 → 0x1eba4，其中：
	 *   - 多帧 get_noise_ft5672(noise, fre) 累加/取平均   [0x223d4 / 0x226a0 / 0x22d24]
	 *   - 某些子项 compare_data_new(&fail[11+idx], ...)   [0x22414 / 0x2274c]
	 *   - min/max 统计写 fts_ftest+0x11a0/0x11a4         [0x23184-0x231e0]
	 *   - compare_array_new(&fail[8], noise, noise_min, noise_max) [0x23174]
	 *   - 无有效点 → get_null_noise(tdata)              [0x23218]
	 *   - 触控值 → "noise(touch) value:%d" / "read touch_value fail,ret=%d"
	 * [TODO-VERIFY] 中间 5 个 compare_data_new/array_new 的分组语义（见 recon §6）
	 */
	ret = get_noise_ft5672(tdata, noise, 0, (int)tdata->fre_num, false);/*0x223d4*/
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("get noise fails,ret=%d\n", ret);
		goto test_err;
	}
	for (i = 0; i < tdata->node.node_num; i++)
		noise[i] = noise[i];
	show_data(noise, false);
	result = compare_array_new(&tdata->fail_buf[8], noise,
				   thr->noise_min, thr->noise_max, false);	/*0x23174*/
	if (!result)
		get_null_noise(tdata);					/* 0x23218 */
test_err:
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------ noise test PASS\n");		/* 0x22ff8 */
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("------ noise test NG\n");		/* 0x1e930 */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 9. ft5672_auxiliary_freq_noise_test —— blob 内联，热 0x1e9c4-0x20c3c
 *    (含 scap_cb_ccbypass 的第二次内联体 0x1ed2c 起？→ 见 recon §4 备注)
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_auxiliary_freq_noise_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0;
	int *noise = NULL;
	int *noise_tmp = NULL;
	bool result = false;
	struct mc_sc_threshold *thr = &tdata->ic.mc_sc.thr;

	FTS_TEST_FUNC_ENTER();						/* 0x1e9c4 */
	FTS_TEST_SAVE_INFO("\n============ Test Item: Auxiliary Freq Noise Test\n");/*0x1ea08*/
	noise = tdata->item7_data;
	tdata->csv_item_cnt++;
	if (!noise) {
		FTS_TEST_SAVE_ERR("noise is null\n");			/* 0x1ea98 */
		ret = -EINVAL;
		goto test_err;
	}
	/* 0x1ebxx-0x20c3c：6 个辅助频点循环
	 *   每个频点：compare_data_new(&fail[11+i], noise, min, max, 0, 0, 0) [0x22414] +
	 *   起始 "switch to freq %d to test noise" / 越频 "freq %d jump to freq %d to test noise"
	 *   失败串全覆盖：set data_sel / set reg1A / write 0x1c / write 0x1d /
	 *   read 0x59 fail / read reg1a / read reg1b / read 0x06 / read 0x0A /
	 *   get rawdata fail / read touch_value fail / read GCB_TX/RX fail /
	 *   restore reg1a / restore reg1b / restore 0x0A / restore data_sel /
	 *   wait state update fail / get noise fails
	 *   csv_item_af_noise(+0xd4) 位 0..5 对应 6 个频点（save_data 侧实证）
	 * [TODO-VERIFY] 6 频点各自的 fre 值来自 thr->basic.auxiliary_fre_noise_test_fre{0..5}
	 */
	for (i = 0; i < 6; i++) {
		if (!(tdata->csv_item_af_noise & (1 << i)))
			continue;
		FTS_TEST_INFO("switch to freq %d to test noise", i);
		ret = fts_test_write_reg(FACTORY_REG_FRE_LIST, (u8)i);
		if (ret < 0)
			goto test_err;
		ret = wait_state_update(TEST_RETVAL_AA);
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("wait state update fail\n");
			goto test_err;
		}
		ret = get_noise_ft5672(tdata, noise, (int)i, 1, false); /* [TODO-VERIFY] 形参 */
		if (ret < 0) {
			FTS_TEST_SAVE_ERR("get noise fails,ret=%d\n", ret);
			goto test_err;
		}
	}
	result = true;
test_err:
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------ Auxiliary Freq noise test PASS\n");/*0x20bcc*/
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("------ Auxiliary Freq noise test NG\n");/* 0x20b7c */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 10. ft5672_jump_freq_noise_test —— blob 内联，热 0x20c5c-0x211d8
 *     冷块 0x21a70-0x21f40（region B）
 * ================================================================== */
static __attribute__((always_inline)) inline int ft5672_jump_freq_noise_test(struct fts_test *tdata, bool *test_result)
{
	int ret = 0;
	int i = 0, j = 0;
	u8 fre = 0, data_sel = 0;
	u8 reg1a = 0, reg1b = 0;
	int *noise = NULL;
	bool result = true;

	FTS_TEST_FUNC_ENTER();						/* 0x20c5c */
	FTS_TEST_SAVE_INFO("\n============ Test Item: Jump Freq Noise Test\n");/*0x20ca8*/
	noise = tdata->item7_data;
	if (!noise) {
		FTS_TEST_SAVE_ERR("noise is null\n");			/* 0x20d2c */
		ret = -EINVAL;
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_FRE_LIST, &fre);		/* 0x0A */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x0A fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(FACTORY_REG_DATA_SELECT, &data_sel);	/* 0x06 */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read 0x06 fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(0x1a, &reg1a);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read reg1a fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = fts_test_read_reg(0x1b, &reg1b);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read reg1b fail,ret=%d\n", ret);
		goto test_err;
	}
	FTS_TEST_INFO("fre:%d,data_sel:%d,reg1a:%d,reg1b:%d",		/* 0x21a78 */
		      fre, data_sel, reg1a, reg1b);
	/* 0x20exx-0x211d8：6 频点两两跳频测量
	 *   "freq %d jump to freq %d to test noise"（FTS_TEST_INFO）
	 *   "noise frame num:%d,%d"（FTS_TEST_INFO）
	 *   失败串：set data_sel / set reg1A / write 0x1c / restore reg1a /
	 *           restore reg1b / restore 0x0A / restore data_sel / wait state update fail
	 * [TODO-VERIFY] 具体写寄存器值 0x1c/0x1d 与帧数来源
	 */
	ret = fts_test_write_reg(FACTORY_REG_DATA_SELECT, 0x01);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("set data_sel fail,ret=%d\n", ret);
		goto test_err;
	}
	ret = wait_state_update(TEST_RETVAL_AA);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto test_err;
	}
	result = true;
test_err:
	if (result) {
		*test_result = true;
		FTS_TEST_SAVE_INFO("------ Jump Freq noise test PASS\n");/*0x21168*/
	} else {
		*test_result = false;
		FTS_TEST_SAVE_ERR("------ Jump Freq noise test NG\n");	/* 0x21120 */
	}
	FTS_TEST_FUNC_EXIT();
	return ret;
}

/* ================================================================== *
 * 11. start_test_ft5672 —— 骨架（本段核心）
 *     blob 0x1dc3c-0x240f8
 *     逐块地址映射见 recon §3；位图契约见 recon §2
 * ================================================================== */
static int start_test_ft5672(void)
{
	int ret = 0;
	struct fts_test *tdata = fts_ftest;
	struct mc_sc_testitem *test_item = &tdata->ic.mc_sc.u.item;
	bool temp_result = false;
	bool test_result = true;
	int i = 0;

	FTS_TEST_FUNC_ENTER();						/* 0x1dc74 */
	tdata->item_fail_flag = 0;					/* 0x1dcac (+0x12bc) */
	FTS_TEST_INFO("test item:0x%x", tdata->ic.mc_sc.u.tmp);		/* 0x1dc94 */

	ret = fts_test_read_reg(0x14, &tdata->fre_num);			/* 0x1dcb0 (+0x37) */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("read fre_num fails");
		return ret;						/* b 0x21868 */
	}
	FTS_TEST_INFO("fre_num:%d", tdata->fre_num);			/* 0x1dd24 */

	ret = malloc_item_data(tdata);					/* 0x1dd3c */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("memory malloc fails");
		return -ENOMEM;						/* w20=-12 → 0x21868 */
	}

	/* ---------------- 测试项 dispatch（顺序 = 地址顺序） ---------------- */
	/* 1) bit0 rawdata */
	if (true == test_item->rawdata_test) {				/* 0x1ddb8 */
		ret = ft5672_rawdata_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x1;			/* 0x1df64 */
		}
	}
	/* 2) bit1 uniformity */
	if (true == test_item->rawdata_uniformity_test) {		/* 0x1df5c */
		ret = ft5672_uniformity_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x2;			/* 0x1e210 */
		}
	}
	/* 3) bit2 scap_cb */
	if (true == test_item->scap_cb_test) {				/* 0x1e208 */
		ret = ft5672_scap_cb_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x20;			/* 0x1e564 */
		}
	}
	/* 4) bit3 scap_rawdata */
	if (true == test_item->scap_rawdata_test) {			/* 0x1e55c */
		ret = ft5672_scap_rawdata_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x10;			/* 0x1e784 */
		}
	}
	/* 5) bit4 short（外部函数，2 个调用点故未内联） */
	if (true == test_item->short_test) {				/* 0x1e794 */
		ret = ft5672_short_test(tdata, &temp_result);		/* 0x1e7a0 → 0x28aa4 */
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x40;			/* 0x1e7b0 */
		}
	}
	/* 6) bit5 panel_differ（外部函数） */
	if (true == test_item->panel_differ_test) {			/* 0x1e7c0 */
		ret = ft5672_panel_differ_test(tdata, &temp_result);	/* 0x1e7cc → 0x29188 */
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x4;			/* 0x1e7dc */
		}
	}
	/* 7) bit6 noise */
	if (true == test_item->noise_test) {				/* 0x1e7ec */
		ret = ft5672_noise_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x8;			/* 0x1e9b0 */
		}
	}
	/* 8) bit10 auxiliary_freq_noise */
	if (true == test_item->auxiliary_freq_noise_test) {		/* 0x1e9a8 */
		ret = ft5672_auxiliary_freq_noise_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x200;			/* 0x20c48 */
		}
	}
	/* 9) bit11 jump_freq_noise（blob 独有；save_data 无对应位说明） */
	if (true == test_item->jump_freq_noise_test) {			/* 0x20c40 */
		ret = ft5672_jump_freq_noise_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x400;			/* 0x21328 */
		}
	}
	/* 10) bit8 rst_test */
	if (true == test_item->rst_test) {				/* 0x211dc */
		ret = ft5672_ex_rst_test(tdata, &temp_result);
		if ((ret < 0) || (false == temp_result)) {
			tdata->item_fail_flag |= 0x80;			/* 0x214d8 */
		}
	}
	/* 11) bit9 rawshift：只读两个已存结果，不重跑测试 */
	if (true == test_item->rawshift_test) {				/* 0x214e8 */
		if (tdata->rawshift_pic_white_result == 3 ||			/* +0xc04 */
		    tdata->rawshift_pic_black_result == 3) {			/* +0xc00 */
			tdata->item_fail_flag |= 0x100;			/* 0x21504 */
		}
	}

	/* ---------------- 收尾 ---------------- */
	ret = fts_test_write_reg(FACTORY_REG_NOMAPPING, tdata->mapping);/* 0x21510 (0x54) */
	fts_test_write_reg(0xfc, 0xaa);					/* 0x21534 */
	msleep(5);
	fts_test_write_reg(0xfc, 0x55);					/* 0x21550 */
	msleep(80);
	fts_test_read(0x90, &tdata->lockdown_info[0], 2);		/* [TODO-VERIFY] */
	ft5672_read_lockdown();						/* 0x21530 起内联体 */

	u8 tmp[32] = { 0 };					/* 0x21520-0x2152c：4×stp xzr 清零 */
	fts_test_write_command(0x07);				/* 0x21818（PASS/NG 两路汇合后） */
	msleep(200);						/* 0x2181c */
	enter_factory_mode();						/* 0x21828 */
	(void)tmp;
	FTS_TEST_INFO("============Test result: 0x%x", tdata->item_fail_flag);/*0x21838*/
	test_result = (tdata->item_fail_flag == 0);			/* 0x21860 cset eq */
	FTS_TEST_FUNC_EXIT();						/* 0x2184c */
	return test_result;
}

/* ================================================================== *
 * 12. save_data_ft5672 —— blob 0x24780-0x27f87
 *     CSV/测试数据保存族：约 250 次 snprintf，唯一非 snprintf 调用 = free_item_data
 *     逐节地址映射见 recon §7；item code 表见 recon §8
 * ================================================================== */
static void save_data_ft5672(char *buf, int *data_length)
{
	struct fts_test *tdata = fts_ftest;
	struct mc_sc_testitem *test_item = &tdata->ic.mc_sc.u.item;
	struct mc_sc_threshold *thr = &fts_ftest->ic.mc_sc.thr;
	u32 cnt = 0;
	u32 tmp_cnt = 0;
	int *tmp_data = NULL;
	int line_num = 11;
	int i = 0;
	u8 tx = tdata->node.tx_num;
	u8 rx = tdata->node.rx_num;
	u8 sc_rx = (tdata->sc_node.tx_num > tdata->sc_node.rx_num) ?
		   tdata->sc_node.tx_num : tdata->sc_node.rx_num;

	/* ---- line 1/2：header（0x247f0 / 0x2480c） ---- */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			"ECC, 85, 170, IC Name, %s, IC Code, %x\n",
			tdata->ic_name, 0);
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			"TestItem Num, %d, ", tdata->csv_item_cnt);	/* +0xc0 */

	/* ---- item 清单（0x24818-0x24b20） ---- */
	if (true == test_item->rawdata_test) {				/* bit0 */
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ", "Rawdata Test",
				CODE_M_RAWDATA_TEST, tx, rx, line_num, 2);
		line_num += tx;
	}
	if (true == test_item->rawdata_uniformity_test) {		/* bit1 */
		if (thr->basic.uniformity_check_tx) {			/* +0x108 */
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
					"%s, %d, %d, %d, %d, %d, ",
					"Rawdata Uniformity Test",
					CODE_M_RAWDATA_UNIFORMITY_TEST,
					tx, rx, line_num, 1);
			line_num += tx;
		}
		if (thr->basic.uniformity_check_rx) {			/* +0x10c */
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
					"%s, %d, %d, %d, %d, %d, ",
					"Rawdata Uniformity Test",
					CODE_M_RAWDATA_UNIFORMITY_TEST,
					tx, rx, line_num, 2);
			line_num += tx;
		}
	}
	if (true == test_item->panel_differ_test) {			/* bit5 */
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ", "Panel Differ Test",
				CODE_M_PANELDIFFER_TEST, tx, rx, line_num, 1);
		line_num += tx;
	}
	if (true == test_item->noise_test) {				/* bit6 */
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ", "Noise Test",
				CODE_M_NOISE_TEST, tx, rx, line_num, 1);
		line_num += tx;
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ", "Null Noise",
				41, 1, 1, line_num, 1);
		line_num += 1;
	}
	if (true == test_item->auxiliary_freq_noise_test) {		/* bit10 */
		if (tdata->csv_item_af_noise & 0x01) {			/* +0xd4 */
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 1);
			line_num += tx;
		}
		if (tdata->csv_item_af_noise & 0x02) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 2);
			line_num += tx;
		}
		if (tdata->csv_item_af_noise & 0x04) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 3);
			line_num += tx;
		}
		if (tdata->csv_item_af_noise & 0x08) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 4);
			line_num += tx;
		}
		if (tdata->csv_item_af_noise & 0x10) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 5);
			line_num += tx;
		}
		if (tdata->csv_item_af_noise & 0x20) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"Auxiliary Freq Noise Test", CODE_M_NOISE_TEST,
				tx, rx, line_num, 6);
			line_num += tx;
		}
	}
	if (true == test_item->scap_rawdata_test) {			/* bit3 */
		if (tdata->csv_item_sraw & 0x01) {			/* +0xc4 */
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP Rawdata Test", CODE_M_SCAP_RAWDATA_TEST,
				2, sc_rx, line_num, 1);
			line_num += 2;
		}
		if (tdata->csv_item_sraw & 0x02) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP Rawdata Test", CODE_M_SCAP_RAWDATA_TEST,
				2, sc_rx, line_num, 2);
			line_num += 2;
		}
		if (tdata->csv_item_sraw & 0x04) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP Rawdata Test", CODE_M_SCAP_RAWDATA_TEST,
				2, sc_rx, line_num, 3);
			line_num += 2;
		}
	}
	if (true == test_item->scap_cb_test) {				/* bit2 */
		if (tdata->csv_item_scb & 0x01) {			/* +0xc8 */
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP CB Test", CODE_M_SCAP_CB_TEST,
				2, sc_rx, line_num, 1);
			line_num += 2;
		}
		if (tdata->csv_item_scb & 0x02) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP CB Test", CODE_M_SCAP_CB_TEST,
				2, sc_rx, line_num, 2);
			line_num += 2;
		}
		if (tdata->csv_item_scb & 0x04) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"%s, %d, %d, %d, %d, %d, ",
				"SCAP CB Test", CODE_M_SCAP_CB_TEST,
				2, sc_rx, line_num, 3);
			line_num += 2;
		}
	}
	/* 0x24b24-0x24b30: 9 x \n (rostr 0x63ab, 9 bytes, donor 同) */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n\n\n\n\n\n\n\n\n");

	/* ---- 数据区（0x24b34..0x25740） ---- */
	/* rawdata：node_num 个 "%d,"，每 rx 个换行 */
	if (true == test_item->rawdata_test && tdata->item1_data) {
		for (i = 0; i < tdata->node.node_num; i++) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,",
					tdata->item1_data[i]);
			if (((i + 1) % tdata->node.rx_num) == 0)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
		}
	}
	/* uniformity：check_tx / check_rx 两段 item2_data */
	if (true == test_item->rawdata_uniformity_test && tdata->item2_data) {
		tmp_cnt = 0;
		if (thr->basic.uniformity_check_tx) {
			tmp_data = tdata->item2_data + tmp_cnt;
			for (i = 0; i < tdata->node.node_num; i++) {
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
				if (((i + 1) % tdata->node.rx_num) == 0)
					cnt += snprintf(buf + cnt,
							CSV_BUFFER_LEN - cnt, "\n");
			}
			tmp_cnt += tdata->node.node_num;
		}
		if (thr->basic.uniformity_check_rx) {
			tmp_data = tdata->item2_data + tmp_cnt;
			for (i = 0; i < tdata->node.node_num; i++) {
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
				if (((i + 1) % tdata->node.rx_num) == 0)
					cnt += snprintf(buf + cnt,
							CSV_BUFFER_LEN - cnt, "\n");
			}
			tmp_cnt += tdata->node.node_num;
		}
	}
	/* panel differ：0x25600 区域，item5_data */
	if (true == test_item->panel_differ_test && tdata->item5_data) {
		for (i = 0; i < tdata->node.node_num; i++) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,",
					tdata->item5_data[i]);
			if (((i + 1) % tdata->node.rx_num) == 0)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
		}
	}
	/* noise：item6_data + null_noise_max（0x25700-0x25710 两处 %d,） */
	if (true == test_item->noise_test && tdata->item6_data) {
		for (i = 0; i < tdata->node.node_num; i++) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,",
					tdata->item6_data[i]);
			if (((i + 1) % tdata->node.rx_num) == 0)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
		}
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,\n",
				tdata->null_noise_max);
	}
	/* scap rawdata / scap cb：item4_data / item3_data，sc_node 分段 */
	if (true == test_item->scap_rawdata_test && tdata->item4_data) {
		tmp_cnt = 0;
		if (tdata->csv_item_sraw & 0x01) {
			tmp_data = tdata->item4_data + tmp_cnt;
			for (i = 0; i < tdata->sc_node.rx_num; i++)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
			for (i = tdata->sc_node.rx_num; i < tdata->sc_node.node_num; i++)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
			tmp_cnt += tdata->sc_node.node_num;
		}
		/* 0x02 / 0x04 同构 */
	}
	if (true == test_item->scap_cb_test && tdata->item3_data) {
		tmp_cnt = 0;
		if (tdata->csv_item_scb & 0x01) {
			tmp_data = tdata->item3_data + tmp_cnt;
			for (i = 0; i < tdata->sc_node.rx_num; i++)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
			for (i = tdata->sc_node.rx_num; i < tdata->sc_node.node_num; i++)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
						"%d,", tmp_data[i]);
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
			tmp_cnt += tdata->sc_node.node_num;
		}
		/* 0x02 / 0x04 同构 */
	}
	/* 0x25738-0x25740：'\n\n' 收尾 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n\n");

	/* ---- 总体结果 / IC / 版本 / lockdown（0x25744-0x25864） ---- */
	/* _b582-INTB：blob 0x25744-0x2576c 用 csel 选格式串（两独立字面量，非 %s 参数） */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag == 0) ? "TP test result: OK\n" : "TP test result: NG\n");
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "IC name: %s\n",
			tdata->ic_name);
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "Version: 0x%x\n",
			tdata->fw_ver);
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "Lockdown info: ");
	for (i = 0; i < 8; i++)
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "0x%x ",
				tdata->lockdown_info[i]);		/* +0x1158 */

	/* ---- 每项 pass/fail 文字（0x258e0-0x25a94） ---- */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x1) ? "Item name=\"rawdata test\" reslut=NG\n"
					      : "Item name=\"rawdata test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x2) ? "Item name=\"uniformity test\" reslut=NG\n"
					      : "Item name=\"uniformity test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x4) ? "Item name=\"panel diff test\" reslut=NG\n"
					      : "Item name=\"panel diff test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x8) ? "Item name=\"noise test\" reslut=NG\n"
					      : "Item name=\"noise test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x10) ? "Item name=\"scap rawdata test\" reslut=NG\n"
					      : "Item name=\"scap rawdata test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x20) ? "Item name=\"cb test\" reslut=NG\n"
					      : "Item name=\"cb test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x40) ? "Item name=\"short test\" reslut=NG\n"
					      : "Item name=\"short test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x80) ? "Item name=\"reset test\" reslut=NG\n"
					      : "Item name=\"reset test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x100) ? "Item name=\"rawshift test\" reslut=NG\n"
					      : "Item name=\"rawshift test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag & 0x200) ? "Item name=\"auxiliary freq noise test\" reslut=NG\n"
					      : "Item name=\"auxiliary freq noise test\" reslut=OK\n");   /* _b582-INTB：blob csel 选格式串 */

	/* ---- 阈值表（0x25a98-0x2690c）----
	 * 每张表："XxxLimitStart\n" + node_num 个 "%d,"（每 rx 换行）+ "XxxLimitEnd\n"
	 * 顺序（实证）：
	 *   MaxRawdata / MinRawdata / MaxTxLinearity / MinTxLinearity /
	 *   MaxRxLinearity / MinRxLinearity / MaxScapCBOn / MinScapCBOn /
	 *   MaxScapCBOff / MinScapCBOff / MaxScapRawdataOn / MinScapRawdataOn /
	 *   MaxScapRawdataOff / MinScapRawdataOff / MaxPanelDiff / MinPanelDiff /
	 *   MaxNoise / MinNoise
	 * 每张以对应 thr 指针数组为源（见 recon §7 表）。
	 */
#define SAVE_LIMIT_TABLE(start_str, end_str, ptr)			\
	do {								\
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,	\
				start_str "\n");			\
		for (i = 0; i < tdata->node.node_num; i++) {		\
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,",\
					(ptr)[i]);			\
			if (((i + 1) % tdata->node.rx_num) == 0)	\
				cnt += snprintf(buf + cnt,		\
						CSV_BUFFER_LEN - cnt, "\n");\
		}							\
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,	\
				end_str "\n");				\
	} while (0)

	if (true == test_item->rawdata_test && thr->rawdata_h_max)
		SAVE_LIMIT_TABLE("MaxRawdataLimitStart", "MaxRawdataLimitEnd",
				 thr->rawdata_h_max);
	if (true == test_item->rawdata_test && thr->rawdata_h_min)
		SAVE_LIMIT_TABLE("MinRawdataLimitStart", "MinRawdataLimitEnd",
				 thr->rawdata_h_min);
	if (true == test_item->rawdata_uniformity_test && thr->tx_linearity_max)
		SAVE_LIMIT_TABLE("MaxTxLinearityLimitStart", "MaxTxLinearityLimitEnd",
				 thr->tx_linearity_max);
	if (true == test_item->rawdata_uniformity_test && thr->tx_linearity_min)
		SAVE_LIMIT_TABLE("MinTxLinearityLinitStart", "MinTxLinearityLimitEnd",
				 thr->tx_linearity_min);
	if (true == test_item->rawdata_uniformity_test && thr->rx_linearity_max)
		SAVE_LIMIT_TABLE("MaxRxLinearityLimitStart", "MaxRxLinearityLimitEnd",
				 thr->rx_linearity_max);
	if (true == test_item->rawdata_uniformity_test && thr->rx_linearity_min)
		SAVE_LIMIT_TABLE("MinRxLinearityLinitStart", "MinRxLinearityLimitEnd",
				 thr->rx_linearity_min);
	if (true == test_item->scap_cb_test && thr->scap_cb_on_min)
		SAVE_LIMIT_TABLE("MaxScapCBOnLimitStart", "MaxScapCBOnLimitEnd",
				 thr->scap_cb_on_max);
	if (true == test_item->scap_cb_test && thr->scap_cb_on_min)
		SAVE_LIMIT_TABLE("MinScapCBOnLinitStart", "MinScapCBOnLimitEnd",
				 thr->scap_cb_on_min);
	if (true == test_item->scap_cb_test && thr->scap_cb_off_min)
		SAVE_LIMIT_TABLE("MaxScapCBOffLimitStart", "MaxScapCBOffLimitEnd",
				 thr->scap_cb_off_max);
	if (true == test_item->scap_cb_test && thr->scap_cb_off_min)
		SAVE_LIMIT_TABLE("MinScapCBOffLinitStart", "MinScapCBOffLimitEnd",
				 thr->scap_cb_off_min);
	if (true == test_item->scap_rawdata_test && thr->scap_rawdata_on_min)
		SAVE_LIMIT_TABLE("MaxScapRawdataOnLimitStart",
				 "MaxScapRawdataOnLimitEnd", thr->scap_rawdata_on_max);
	if (true == test_item->scap_rawdata_test && thr->scap_rawdata_on_min)
		SAVE_LIMIT_TABLE("MinScapRawdataOnLinitStart",
				 "MinScapRawdataOnLimitEnd", thr->scap_rawdata_on_min);
	if (true == test_item->scap_rawdata_test && thr->scap_rawdata_off_min)
		SAVE_LIMIT_TABLE("MaxScapRawdataOffLimitStart",
				 "MaxScapRawdataOffLimitEnd", thr->scap_rawdata_off_max);
	if (true == test_item->scap_rawdata_test && thr->scap_rawdata_off_min)
		SAVE_LIMIT_TABLE("MinScapRawdataOffLinitStart",
				 "MinScapRawdataOffLimitEnd", thr->scap_rawdata_off_min);
	if (true == test_item->panel_differ_test && thr->panel_differ_min)
		SAVE_LIMIT_TABLE("MaxPanelDiffLimitStart", "MaxPanelDiffLimitEnd",
				 thr->panel_differ_max);
	if (true == test_item->panel_differ_test && thr->panel_differ_min)
		SAVE_LIMIT_TABLE("MinPanelDiffLimitStart", "MinPanelDiffLimitEnd",
				 thr->panel_differ_min);
	if (true == test_item->noise_test && thr->noise_min)
		SAVE_LIMIT_TABLE("MaxNoiseLimitStart", "MaxNoiseLimitEnd",
				 thr->noise_max);
	if (true == test_item->noise_test && thr->noise_min)
		SAVE_LIMIT_TABLE("MinNoiseLimitStart", "MinNoiseLimitEnd",
				 thr->noise_min);
#undef SAVE_LIMIT_TABLE

	/* ---- 失败记录转写（0x268e4-0x27d50）----
	 * 通用形态（rawdata 段 0x268f0-0x26a00 逐指令实证）：
	 *   标题串 → "(max=%d min=%d)" → 数据阵列 → fail[n] 记录循环
	 * 记录循环体（0x269c0-0x269e8）：
	 *   cmp i,#0x5dc(1500) → brk（0x27f84，编译器插桩）
	 *   ldp w3,w4,[rec-0x10] / ldp w5,w6,[rec-0x8] / ldr w7,[rec],#0x14
	 *   → snprintf("test fail  node(%4d  %4d)=%5d  range=(%5d  %5d)\n",
	 *              tx, rx, val, min, max)     ★ 注意两个空格处均为双空格
	 *   循环次数 = fail_num = *(int *)(tdata+0x12c0+30004*n)（0x2699c / 0x269ec）
	 */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "ItemDataRecord\n");/*0x268e4*/
	if (true == test_item->rawdata_test && tdata->item1_data) {
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
				"RawDataRecord max=%d min=%d\n",
				tdata->rawdata_max, tdata->rawdata_min);/*+0x1160 / +0x1164*/
		for (i = 0; i < tdata->node.node_num; i++) {
			cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "%d,",
					tdata->item1_data[i]);
			if (((i + 1) % tdata->node.rx_num) == 0)
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "\n");
		}
		if (tdata->fail_buf[0].fail_num >= 1) {			/* 0x2699c */
			struct fts_test_fail_node *nd = &tdata->fail_buf[0].node[0];
			for (i = 0; i < tdata->fail_buf[0].fail_num; i++, nd++) {
				BUG_ON(i >= FTS_TEST_FAIL_NODE_MAX);	/* 0x5dc */
				cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
					"test fail  node(%4d  %4d)=%5d  range=(%5d  %5d)\n",
					nd->tx, nd->rx, nd->val, nd->min, nd->max);/*0x269b8*/
			}
		}
	}
	/* ---- 其余各项（0x26a00-0x27d50）----
	 * 结构同上，仅源缓冲 / fail[n] 索引 / max-min 字段不同：
	 *   "RawdataUniformityDataRecord"   → fail[1]（tx 段）、fail[2]（rx 段）
	 *   "PanelDiffRecord max=%d min=%d" → fail[3]，数据源 item5_data
	 *   "NoiseRecord max=%d min=%d"     → fail[8]，数据源 item6_data
	 *   "AuxiliaryFreq[%d]NoiseRecord max=%d min=%d" ×6 → fail[11..16]
	 *   "ScapRawdataRecord"（On/Off 子节）→ item4_data（scap 项不落 fail 缓冲）
	 *   "ScapCBRecord"（On/Off 子节）    → item3_data
	 *   "JumpFreqNoise[i->j]Record max=%d min=%d" ×15（C(6,2)）→ fail[17..31]
	 *   "RawshiftPicRecord" / "RawshiftPic white max=%d min=%d"
	 *   "ShortTestRecord"
	 * [TODO-VERIFY] 逐段的 max/min 字段偏移（自 +0x1160 起）与循环边界
	 *   —— 记录布局与格式串已由 rawdata 段逐指令钉死，其余段为同构复制。
	 */

	/* ---- 尾部 test_result 汇总（0x27d6c-0x27f5c）---- */
	/* _b582-INTB：blob 两独立字面量（test_result:[PASS] / [Failure]，无尾 \n） */
	cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt,
			(tdata->item_fail_flag == 0) ? "test_result:[PASS]" : "test_result:[Failure]");
	if (tdata->item_fail_flag & 0x1)
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "-0F");
	else
		cnt += snprintf(buf + cnt, CSV_BUFFER_LEN - cnt, "-0P");
	/* 依 0x1..0x200 共 10 位同构输出 -1F/-1P … -9F/-9P */

	*data_length = cnt;						/* 0x27f48 */
	free_item_data(tdata);						/* 0x27f5c */
}

/************************************************************************
* 段 3（seg3）：rawshift 测试族 + sysfs 触发面
*
* 来源：focaltech_touch_rodin.ko（小米官方 GPL 发布，自机权益）
*       vermagic 6.6.77-android15-8-g358a69b2ec0f-4k SMP preempt mod_unload modversions aarch64
*       .text 段内地址 = 符号表 Value（relocatable，addr=0，节内偏移）
*
* 本段覆盖（blob 地址/大小/symtab 绑定）：
*   fts_rawshift_fre_test              0x27f8c /1776 LOCAL  ← test_funcs +0x60
*   fts_rawshift_pic_black_test        0x28680 / 248 LOCAL  ← test_funcs +0x68
*   fts_rawshift_pic_white_test        0x2877c / 248 LOCAL  ← test_funcs +0x70
*   get_rawshift_fre_data              0x2ab80 / 900 LOCAL
*   malloc_item_data / free_item_data  （非本段，骨架代理）
*   fts_test_rawshift_fre_show         0x179d4 /  68 LOCAL
*   fts_test_rawshift_fre_store        0x17a1c / 496 LOCAL
*   fts_rawshift_fre_test_entry        0x17c0c / 496 LOCAL
*   fts_test_rawshift_pic_black_show   0x17e00 /  68 LOCAL
*   fts_test_rawshift_pic_black_store  0x17e48 / 496 LOCAL
*   fts_rawshift_pic_black_test_entry  0x18038 / 516 LOCAL
*   fts_test_rawshift_pic_white_show   0x18240 / 332 LOCAL
*   fts_test_rawshift_pic_white_store  0x18390 / 496 LOCAL
*   fts_rawshift_pic_white_test_entry  0x18580 / 624 LOCAL
*   dev_attr_fts_test_rawshift_fre       .data 0x620 / 32 LOWC
*   dev_attr_fts_test_rawshift_pic_black .data 0x640 / 32 LOCAL
*   dev_attr_fts_test_rawshift_pic_white .data 0x660 / 32 LOCAL
*
* 关键机器码事实（详见 ft5672_rawshift_recon.md）：
*  1) ft5672_rawshift_fre_test 被 GCC 内联进 fts_rawshift_fre_test（唯一调用方），
*     故 blob 无独立符号；两条 __func__ 常量证明源码是两个函数：
*       - malloc 失败打印 __func__ = "fts_rawshift_fre_test"   (str +0xac19)
*       - 主体打印     __func__ = "ft5672_rawshift_fre_test"    (str +0x98bf)
*  2) 三个测试体 kCFI 类型哈希同为 0x36b1c5a6 → 原型 int (*)(void)；
*     test_func_ft5672(.data+0x26d8, 128B) +0x60/0x68/0x70/0x78 分别 reloc 到
*     0x27f8c / 0x28680 / 0x2877c / 0x28878(free_item_data)。
*  3) 本版 FTS_TEST_SAVE_INFO 带打印（level KERN_INFO + "[FTS_TS/I][TEST]%s:" fmt "\n"），
*     FTS_TEST_SAVE_ERR 带打印（KERN_ERR + "[FTS_TS/E][TEST]%s:" fmt "\n"）；
*     用于纯打印的 FTS_TEST_INFO 是 KERN_ERR + "[FTS_TS/I][TEST]%s:"（活树同形）。
*     见 recon.md §日志宏。
*
* 本段用到的 struct fts_test 字段（偏移为 blob 实证，名字供整合者对表；
* 整合者 struct 必须保证下列偏移一致）：
*   +0x014 node.node_num              (int)
*   +0x078 rawshift_fre_data          (int *)  9*node_num 个 int：6 频点 + max/min/differ
*   +0x0c0 test_item_cnt              (int)    每跑一个测试项 ++（start_test/save_data 也读写）
*   +0x0cc data_valid_mask            (u32)    bit0..5=6 频点，bit6..8=min/max/differ
*   +0x0d0 rawshift_result_mask       (u32)    pic 测试用（本段 white_show/entry 读写）
*   +0x26c rawshift_fre               (int)    使能（blob 无任何写入点 → 恒 0）
*   +0x270 rawshift_fre_frames        (int)    每频点采样次数
*   +0x274 rawshift_fre_threshold     (int)    compare_data_new 的 max
*   +0x278..0x28c rawshift_fre_freq[6] (int)   各频点使能
*   +0x125c fre_max[6] (int)  各频点 max
*   +0x1274 fre_min[6] (int)  各频点 min
*   +0x128c raw_max / +0x1290 raw_min / +0x1294 raw_max2 / +0x1298 raw_min2 (int)
*   +0x129c differ_max / +0x12a0 differ_min (int)
*   +0x3c0 test_item_flag (u16, bit9 = rawshift 使能)   ← 与 start_test_ft5672/pic 测试共判
*   +0x3c8 func (struct test_funcs *)
*   +0xbe0 testresult (char *)  +0xbe8 testresult_len (int)
*   +0xbfc rawshift_pic_code   (int)  2=PASS 3=NG（white entry 写，white show 读）
*   +0xc00 rawshift_pic_black_result (int) 2=PASS 3=NG
*   +0xc04 rawshift_pic_white_result (int) 2=PASS 3=NG
*   +0xc08 rawshift_fre_result       (int) 2=PASS 3=NG
*   +0x43194 compare_data_new 用的 struct fts_test_fail_buf（fre 项专用槽）
*   +0x12bc 报告位掩码（start_test/save_data 读写；本段不直接引用）
*
* 契约（外部，由其他分段提供，签名已用活树 focaltech_test.h:655-685 对齐）：
*   int  fts_test_main_init(void);                       (LOCAL 0x150e8，与 entry 同 TU)
*   int  fts_test_get_testparam_from_ini(char *config_name);  (GLOBAL 0x18d10)
*   int  fts_enter_test_environment(bool test_state);
*   void fts_esdcheck_switch(struct fts_ts_data *ts_data, bool enable);
*   void fts_irq_enable(void); void fts_irq_disable(void);
*   int  enter_factory_mode(void); int start_scan(void); int get_rawdata(int *data);
*   int  wait_state_update(u8 retval);        (本段传 TEST_RETVAL_AA)
*   void show_data(int *data, bool key);      (blob 内是空实现，FTS_TEST_SHOW_DATA 关闭)
*   void sys_delay(int ms); void *fts_malloc(size_t size); void fts_free_proc(void *p);
*   int  fts_test_write_reg(u8 addr, u8 val);
*   bool compare_data_new(struct fts_test_fail_buf *out, int *data, int min, int max,
*                         int min_vk, int max_vk, bool key);
*   int  malloc_item_data(struct fts_test *tdata);   (LOCAL 0x2891c，pic 测试调用)
*   int  ft5672_rawshift_pic_test(struct fts_test *tdata, bool *test_result, u8 pic_type);
*        (LOCAL 0x2af04；seg1 定稿签名，本段据此传 bool* / 0|1，返回值忽略)
*
* == 跨分段落差（整合者需统一）==
*  1) 三个测试体：blob 绑定 = LOCAL(static)；本段按任务书写成非 static
*     （便于跨分段/对象引用）。同文件合并时加 static 即与 blob 一致；
*     seg1 的注释块按 static 声明（见 seg1 头注释“由 F2c 提供”）。
*  2) 结果字段命名与 seg2 冲突（偏移一致、名字不同，合并时二选一）：
*       +0xc00 本段 rawshift_pic_black_result   ←→ seg2 rawshift_fre_result
*       +0xc04 本段 rawshift_pic_white_result   ←→ seg2 rawshift_pic_result
*       +0xc08 本段 rawshift_fre_result         (seg2 未用)
*       +0xd0  本段 rawshift_result_mask        (start_test/save_data 也读写)
*  3) 0x290/0x294/0x298 三个 ini 参数 seg1 归入 struct mc_sc_threshold_b
*     新字段（rawshift_pic / _frames / _threshold），本段只按 fts_test 相对偏移写。
************************************************************************/

/* ==================================================================
 * 1. 原始数据读取（get_rawshift_fre_data，blob 0x2ab80/900B）
 *    形参类型与 seg1 声明对齐：u8 fre / u8 frames / bool *result
 * ================================================================== */
static int get_rawshift_fre_data(struct fts_test *tdata, int *data, u8 fre,
				 u8 frames, bool *valid)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	int ok = 0;		/* 成功累加的帧数（blob w25）*/
	int *buf = NULL;

	/* 0x2aba0: cbz x1 → SAVE_ERR("data is null\n")；返回 -EINVAL（*valid=0）*/
	if (!data) {
		FTS_TEST_SAVE_ERR("data is null\n");
		*valid = 0;
		return -EINVAL;
	}

	/* 0x2abb8: fts_malloc(node_num * sizeof(int))；失败 → 返回 0（*valid=0）*/
	buf = fts_malloc(tdata->node.node_num * sizeof(int));
	if (!buf) {
		FTS_TEST_SAVE_ERR("memory temp_rawdata malloc fails");
		*valid = 0;
		return 0;
	}

	/* 0x2abc8: 切频——写寄存器 0x0A */
	ret = fts_test_write_reg(0x0A, fre);
	if (ret < 0) {
		/* 0x2add0: SAVE_INFO 用真 snprintf（带 %d 实参）*/
		FTS_TEST_SAVE_ERR("set fre%d fail,ret=%d\n", fre, ret);   /* _b582-INTB：blob 为 E 族（SAVE_ERR） */
		goto out_valid;
	}
	sys_delay(18);				/* 0x2abd8: w0=18 */
	ret = wait_state_update(TEST_RETVAL_AA);	/* 0x2abe4: w0=0xAA */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("wait state update fail\n");
		goto out_valid;
	}
	ret = start_scan();			/* 0x2abec */
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("scan fail\n");
		goto out_valid;
	}

	/* blob 0x2abf8/0x2ac00/0x2ac08：采样前无条件丢 3 帧（返回值丢弃，vendor 预热惯例） */
	(void)get_rawdata(buf);
	(void)get_rawdata(buf);
	(void)get_rawdata(buf);

	/* 0x2ac0c..0x2ac74: 采样 frames 次，成功帧累加 */
	for (i = 0; i < frames; i++) {
		if (get_rawdata(buf) == 0) {
			for (j = 0; j < tdata->node.node_num; j++)
				data[j] += buf[j];
			ok++;
		}
	}

	/* 0x2ac78: ok==0 → *valid=0；否则按帧平均后 *valid=1 */
	if (ok) {
		for (j = 0; j < tdata->node.node_num; j++)
			data[j] /= ok;
		*valid = 1;
	} else {
		*valid = 0;
	}

	fts_free_proc(buf);
	return ret;

out_valid:
	/* blob 原样：三个错误出口都落到 *valid = 1 的赋值（0x2aed4），
	 * 详见 recon.md [TODO-VERIFY] #4 */
	fts_free_proc(buf);
	*valid = 1;
	return ret;
}

/* ==================================================================
 * 2. rawshift fre 测试体
 *    blob: ft5672_rawshift_fre_test 被内联进 fts_rawshift_fre_test，
 *    故此处写两个函数（static worker + 导出 wrapper），GCC 单调用点会内联，
 *    与 blob 机器码同形（wrapper 符号 0x27f8c size 1776 含内联体）。
 * ================================================================== */
static int ft5672_rawshift_fre_test(void)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;
	int i = 0;
	int idx = 0;		/* 已测频点在 rawshift_fre_data 中的偏移（int 个数）*/
	int data_valid = 0;
	int fail = 0;
	int frames = 0;
	int *rawdata = NULL;
	int *fre_max = NULL;	/* rawshift_fre_data + 6*node_num */
	int *fre_min = NULL;	/* rawshift_fre_data + 7*node_num */
	int *differ = NULL;	/* rawshift_fre_data + 8*node_num */
	bool valid[6] = { false };
	int d = 0;

	frames = tdata->ic.mc_sc.thr.basic.rawshift_fre_frames;		/* blob +0x270 */
	FTS_TEST_SAVE_INFO("\n============ Test Item: RawShift Fre Test\n");
	tdata->csv_item_cnt++;				/* blob +0xc0 */

	rawdata = tdata->rawshift_fre_data;		/* blob +0x78 */
	if (!rawdata || !tdata->ic.mc_sc.thr.basic.rawshift_fre || !tdata->ic.mc_sc.thr.basic.rawshift_fre_threshold) {
		FTS_TEST_SAVE_ERR("rawshift_fre_data/rawshift_fre/rawshift_fre_threshold is null\n");
		ret = -EINVAL;
		data_valid = 0;
		goto check;
	}

	enter_factory_mode();				/* 0x28280 */

	fre_min = rawdata + 7 * tdata->node.node_num;
	fre_max = rawdata + 6 * tdata->node.node_num;
	differ = rawdata + 8 * tdata->node.node_num;

	for (i = 0; i < tdata->node.node_num; i++) {
		fre_max[i] = 0;
		fre_min[i] = 0xFFFF;
	}
	tdata->rawshift_raw_max = 0;
	tdata->rawshift_raw_min = 0xFFFF;
	tdata->rawshift_raw_max2 = 0;
	tdata->rawshift_raw_min2 = 0xFFFF;

	for (i = 0; i < 6; i++) {
		valid[i] = 1;
		tdata->rawshift_fre_max[i] = 0;			/* blob +0x125c+4i */
		tdata->rawshift_fre_min[i] = 0xFFFF;		/* blob +0x1274+4i */
		switch (i) {
		case 0:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[0])
				continue;
			break;
		case 1:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[1])
				continue;
			break;
		case 2:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[2])
				continue;
			break;
		case 3:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[3])
				continue;
			break;
		case 4:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[4])
				continue;
			break;
		case 5:
			if (!tdata->ic.mc_sc.thr.basic.rawshift_fre_freq[5])
				continue;
			break;
		}
		FTS_TEST_INFO("switch to freq %d to test rawshift", i);
		ret = get_rawshift_fre_data(tdata, rawdata + idx, i, frames, &valid[i]);
		if (ret < 0) {
			valid[i] = 0;
			goto check;
		}
		for (d = 0; d < tdata->node.node_num; d++) {
			int v = (rawdata + idx)[d];

			if (tdata->rawshift_raw_max < v)
				tdata->rawshift_raw_max = v;
			if (tdata->rawshift_raw_min > v)
				tdata->rawshift_raw_min = v;
			if (tdata->rawshift_raw_max2 < v)
				tdata->rawshift_raw_max2 = v;
			if (tdata->rawshift_raw_min2 > v)
				tdata->rawshift_raw_min2 = v;
			if (tdata->rawshift_fre_max[i] < v)
				tdata->rawshift_fre_max[i] = v;
			if (tdata->rawshift_fre_min[i] > v)
				tdata->rawshift_fre_min[i] = v;
			if (fre_min[d] > v)
				fre_min[d] = v;
			if (fre_max[d] < v)
				fre_max[d] = v;
		}
		tdata->data_valid_mask |= (1 << i);	/* blob +0xcc */
		idx += tdata->node.node_num;
	}

	/* 0x2857c: 没有任何频点成功 → NG */
	if (!tdata->data_valid_mask) {
		ret = 0;
		data_valid = 0;
		goto check;
	}
	tdata->data_valid_mask |= 0x1C0;		/* 0x2859c */

	for (d = 0; d < tdata->node.node_num; d++) {
		int v = fre_max[d] - fre_min[d];

		differ[d] = (v < 0) ? -v : v;		/* 0x285b0: subs/cneg → abs */
	}
	show_data(differ, 0);
	data_valid = compare_data_new((struct fts_test_fail_buf *)((char *)tdata + 0x43194),
				      differ, 0, tdata->ic.mc_sc.thr.basic.rawshift_fre_threshold, 0, 0, false);
	for (d = 0; d < tdata->node.node_num; d++) {
		int v = differ[d];

		if (tdata->rawshift_differ_max < v)
			tdata->rawshift_differ_max = v;
		if (tdata->rawshift_differ_min > v)
			tdata->rawshift_differ_min = v;
	}

check:
	/* blob 只算一次条件（w19）并复用两次 */
	fail = (!data_valid || !valid[0] || !valid[1] || !valid[2] ||
		!valid[3] || !valid[4] || !valid[5]) ? 1 : 0;
	if (fail)
		FTS_TEST_SAVE_ERR("rawshift fre test is NG\n");
	else
		FTS_TEST_SAVE_INFO("rawshift fre test is OK\n");
	FTS_TEST_FUNC_EXIT();

	if (ret < 0 || fail)
		return 0;
	return 1;
}

/* test_funcs +0x60（blob 0x27f8c，kCFI int(*)(void)）*/
static int fts_rawshift_fre_test(void)
{
	int ret = 0;

	fts_ftest->rawshift_fre_data =
	    fts_malloc(fts_ftest->node.node_num * 9 * sizeof(int));
	if (!fts_ftest->rawshift_fre_data) {
		FTS_TEST_SAVE_ERR("memory malloc fails");
		return -ENOMEM;
	}
	if (!(fts_ftest->ic.mc_sc.u.item.rawshift_test))	/* blob +0x3c0 bit9 */
		return 0;
	ret = ft5672_rawshift_fre_test();
	return ret;
}

/* ==================================================================
 * 3. rawshift pic 测试体（black / white）
 *    blob 0x28680 / 0x2877c，各 248B，均调 ft5672_rawshift_pic_test
 *    （seg1 定义：static int ft5672_rawshift_pic_test(struct fts_test *,
 *      bool *test_result, u8 pic_type)；blob 实参 (tdata,&result,0/1)）
 * ================================================================== */
static int fts_rawshift_pic_black_test(void)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;
	bool result = false;

	ret = malloc_item_data(fts_ftest);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("memory malloc fails");
		return -ENOMEM;
	}
	if (tdata->ic.mc_sc.u.item.rawshift_test) {		/* blob +0x3c0 bit9 */
		ft5672_rawshift_pic_test(tdata, &result, 0);
		ret = result ? 1 : 0;
	}
	return ret;
}

static int fts_rawshift_pic_white_test(void)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;
	bool result = false;

	ret = malloc_item_data(fts_ftest);
	if (ret < 0) {
		FTS_TEST_SAVE_ERR("memory malloc fails");
		return -ENOMEM;
	}
	if (tdata->ic.mc_sc.u.item.rawshift_test) {		/* blob +0x3c0 bit9 */
		ft5672_rawshift_pic_test(tdata, &result, 1);
		ret = result ? 1 : 0;
	}
	return ret;
}

/* ==================================================================
 * 4. sysfs 触发面：show
 * ================================================================== */
static ssize_t fts_test_rawshift_fre_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	if (!fts_ftest)
		return -EINVAL;
	/* blob +0xc08（entry 写 2=PASS/3=NG；非 0 → PASS，机器码无歧义）*/
	if (fts_ftest->rawshift_fre_result)
		return snprintf(buf, 5, "PASS");
	else
		return snprintf(buf, 5, "FAIL");
}

static ssize_t fts_test_rawshift_pic_black_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	if (!fts_ftest)
		return -EINVAL;
	/* blob +0xc00 */
	if (fts_ftest->rawshift_pic_black_result)
		return snprintf(buf, 5, "PASS");
	else
		return snprintf(buf, 5, "FAIL");
}

static ssize_t fts_test_rawshift_pic_white_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	if (!fts_ftest)
		return -EINVAL;
	if (fts_ftest->rawshift_result_mask == 4) {	/* blob +0xd0 == 4 */
		if (fts_ftest->rawshift_pic_code == 2)	/* blob +0xbfc */
			FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic test pass.\n");
		else
			FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic test failure.\n");
		fts_ftest->rawshift_result_mask = 0;
	}
	if (fts_ftest->rawshift_pic_white_result)	/* blob +0xc04 */
		return snprintf(buf, 5, "PASS");
	else
		return snprintf(buf, 5, "FAIL");
}

/* ==================================================================
 * 5. sysfs 触发面：entry（static，blob LOCAL；仅被本文件 store 调用）
 * ================================================================== */
static void fts_rawshift_fre_test_entry(char *fwname)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;

	ret = fts_test_main_init();
	if (ret < 0) {
		FTS_TEST_ERROR("fts_test_main_init fail");
		ret = 3;
		goto out;
	}
	FTS_TEST_SAVE_INFO("ini_file_name:%s\n", fwname);
	ret = fts_test_get_testparam_from_ini(fwname);
	if (ret < 0) {
		FTS_TEST_ERROR("get testparam fail");
		ret = 3;
		goto out;
	}
	if (tdata && tdata->func && tdata->func->rawshift_fre_test) {
		ret = tdata->func->rawshift_fre_test();
		if (ret == 1) {
			ret = 2;
			FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift fre test pass.\n");
			goto out;
		}
	} else {
		FTS_TEST_ERROR("test func/rawshift_fre_test func is null");
	}
	ret = 3;
	FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift fre test failure.\n");
out:
	fts_ftest->rawshift_fre_result = ret;
}

static void fts_rawshift_pic_black_test_entry(char *fwname)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;

	ret = fts_test_main_init();
	if (ret < 0) {
		FTS_TEST_ERROR("fts_test_main_init fail");
		ret = 3;
		goto out;
	}
	FTS_TEST_SAVE_INFO("ini_file_name:%s\n", fwname);
	ret = fts_test_get_testparam_from_ini(fwname);
	if (ret < 0) {
		FTS_TEST_ERROR("get testparam fail");
		ret = 3;
		goto out;
	}
	if (tdata && tdata->func && tdata->func->rawshift_pic_black_test) {
		ret = tdata->func->rawshift_pic_black_test();
		if (ret == 1) {
			ret = 2;
			FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic_black test pass.\n");
			goto out;
		}
	} else {
		FTS_TEST_ERROR("test func/rawshift_pic_black_test func is null");
	}
	ret = 3;
	FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic_black test failure.\n");
out:
	fts_ftest->rawshift_pic_black_result = ret;
}

static void fts_rawshift_pic_white_test_entry(char *fwname)
{
	struct fts_test *tdata = fts_ftest;
	int ret = 0;

	ret = fts_test_main_init();
	if (ret < 0) {
		FTS_TEST_ERROR("fts_test_main_init fail");
		fts_ftest->rawshift_pic_code = 3;
		return;
	}
	FTS_TEST_SAVE_INFO("ini_file_name:%s\n", fwname);
	ret = fts_test_get_testparam_from_ini(fwname);
	if (ret < 0) {
		FTS_TEST_ERROR("get testparam fail");
		fts_ftest->rawshift_pic_code = 3;
		return;
	}
	if (tdata && tdata->func && tdata->func->rawshift_pic_white_test) {
		ret = tdata->func->rawshift_pic_white_test();
		if (ret == 1) {
			if (tdata->rawshift_result_mask == 4) {	/* blob +0xd0 == 4 */
				fts_ftest->rawshift_pic_code = 2;
				FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic test pass.\n");
				return;		/* blob: 此支路不写 +0xc04 */
			}
			FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic white test pass.\n");
			ret = 2;
			fts_ftest->rawshift_pic_white_result = ret;
			return;
		}
	} else {
		FTS_TEST_ERROR("test func/rawshift_pic_white_test func is null");
	}
	FTS_TEST_SAVE_INFO("\n\n=======Tp rawshift pic white test failure.\n");
	fts_ftest->rawshift_pic_white_result = 3;
}

/* ==================================================================
 * 6. sysfs 触发面：store（三段同形；差异仅 entry 与 __func__/行号）
 *    与 blob fts_test_store 的差异（实测）：mutex_lock 在前，
 *    无 fts_irq_disable()/fts_esdcheck_switch(DISABLE)，
 *    只有末尾的 fts_esdcheck_switch(ENABLE)+fts_irq_enable()。
 * ================================================================== */
static ssize_t fts_test_rawshift_fre_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int ret = 0;
	char fwname[FILE_NAME_LENGTH] = { 0 };
	struct fts_ts_data *ts_data = dev_get_drvdata(dev);
	struct input_dev *input_dev = ts_data->input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}
	if (ts_data->fw_loading) {
		FTS_INFO("fw upgrade in process, no test");
		return 0;
	}
	mutex_lock(&input_dev->mutex);
	memset(fwname, 0, sizeof(fwname));
	snprintf(fwname, FILE_NAME_LENGTH, "%s", buf);
	fwname[count - 1] = '\0';
	FTS_TEST_DBG("fwname:%s.", fwname);
	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
	} else {
		fts_rawshift_fre_test_entry(fwname);
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
	}
	fts_esdcheck_switch(ts_data, ENABLE);
	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);
	return count;
}

static ssize_t fts_test_rawshift_pic_black_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	int ret = 0;
	char fwname[FILE_NAME_LENGTH] = { 0 };
	struct fts_ts_data *ts_data = dev_get_drvdata(dev);
	struct input_dev *input_dev = ts_data->input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}
	if (ts_data->fw_loading) {
		FTS_INFO("fw upgrade in process, no test");
		return 0;
	}
	mutex_lock(&input_dev->mutex);
	memset(fwname, 0, sizeof(fwname));
	snprintf(fwname, FILE_NAME_LENGTH, "%s", buf);
	fwname[count - 1] = '\0';
	FTS_TEST_DBG("fwname:%s.", fwname);
	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
	} else {
		fts_rawshift_pic_black_test_entry(fwname);
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
	}
	fts_esdcheck_switch(ts_data, ENABLE);
	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);
	return count;
}

static ssize_t fts_test_rawshift_pic_white_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	int ret = 0;
	char fwname[FILE_NAME_LENGTH] = { 0 };
	struct fts_ts_data *ts_data = dev_get_drvdata(dev);
	struct input_dev *input_dev = ts_data->input_dev;

	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}
	if (ts_data->fw_loading) {
		FTS_INFO("fw upgrade in process, no test");
		return 0;
	}
	mutex_lock(&input_dev->mutex);
	memset(fwname, 0, sizeof(fwname));
	snprintf(fwname, FILE_NAME_LENGTH, "%s", buf);
	fwname[count - 1] = '\0';
	FTS_TEST_DBG("fwname:%s.", fwname);
	ret = fts_enter_test_environment(1);
	if (ret < 0) {
		FTS_ERROR("enter test environment fail");
	} else {
		fts_rawshift_pic_white_test_entry(fwname);
	}
	ret = fts_enter_test_environment(0);
	if (ret < 0) {
		FTS_ERROR("enter normal environment fail");
	}
	fts_esdcheck_switch(ts_data, ENABLE);
	fts_irq_enable();
	mutex_unlock(&input_dev->mutex);
	return count;
}

/* ==================================================================
 * 7. device_attribute 对象（blob: .data 0x620/0x640/0x660，32B/个
 *    = {name, mode=0x1a4(S_IRUGO|S_IWUSR), show, store}）
 * ================================================================== */
struct device_attribute dev_attr_fts_test_rawshift_fre = {
	.attr	= { .name = __stringify(fts_test_rawshift_fre), .mode = 0644 },
	.show	= fts_test_rawshift_fre_show,
	.store	= fts_test_rawshift_fre_store,
};
struct device_attribute dev_attr_fts_test_rawshift_pic_black = {
	.attr	= { .name = __stringify(fts_test_rawshift_pic_black), .mode = 0644 },
	.show	= fts_test_rawshift_pic_black_show,
	.store	= fts_test_rawshift_pic_black_store,
};
struct device_attribute dev_attr_fts_test_rawshift_pic_white = {
	.attr	= { .name = __stringify(fts_test_rawshift_pic_white), .mode = 0644 },
	.show	= fts_test_rawshift_pic_white_show,
	.store	= fts_test_rawshift_pic_white_store,
};

/* ==================================================================
 * 8. 注册面补丁（在 focaltech_test.c，非本段文件；blob .data 实证）
 *
 * blob .data+0x5b0 fts_test_attribute_group (40B LOCAL):
 *     { name=NULL, is_visible=NULL, is_bin_visible=NULL, attrs=&fts_test_attributes, bin_attrs=NULL }
 * blob .data+0x5d8 fts_test_attributes (40B LOCAL) —— 4 项 + NULL，顺序：
 *     [0] &dev_attr_fts_test.attr                  (.data+0x600)
 *     [1] &dev_attr_fts_test_rawshift_fre.attr     (.data+0x620)
 *     [2] &dev_attr_fts_test_rawshift_pic_black.attr (.data+0x640)
 *     [3] &dev_attr_fts_test_rawshift_pic_white.attr (.data+0x660)
 *     [4] NULL
 *
 * 活树 focaltech_test.c:2295 现为 { &dev_attr_fts_test.attr, NULL }，精确补丁：
 *
 *   static struct attribute *fts_test_attributes[] = {
 *       &dev_attr_fts_test.attr,
 *       &dev_attr_fts_test_rawshift_fre.attr,
 *       &dev_attr_fts_test_rawshift_pic_black.attr,
 *       &dev_attr_fts_test_rawshift_pic_white.attr,
 *       NULL
 *   };
 *
 * （顺序即 blob 顺序；dev_attr_fts_test_rawshift_* 三对象由本文件的
 *   DEVICE_ATTR_RW(...) 三条生成，符号名/尺寸与 blob symtab 一一对应。）
 * ================================================================== */

/* ==== 对象初始化（blob .data+0x26d8 逐字段实测）==== */
struct test_funcs test_func_ft5672 = {
    .ctype = {0x90, 0x92},          // blob .data+0x00 实测 u16[0]=144, u16[1]=146
    .hwtype = IC_HW_MC_SC,          // .data+0x08 = 2
    .startscan_mode = SCAN_NORMAL,  // .data+0x0c = 0
    .key_num_total = 0,             // .data+0x10 = 0
    .rawdata2_support = false,      // .data+0x14 = 0
    .force_touch = false,           // .data+0x15 = 0
    .mc_sc_short_v2 = true,         // .data+0x16 = 1
    .raw_u16 = false,               // .data+0x17 = 0
    .cb_high_support = true,        // .data+0x18 = 1
    .param_update_support = true,   // .data+0x19 = 1
    .param_init = param_init_ft5672,          // +0x20 -> 0x1d74c
    .init = NULL,                             // +0x28 无重定位
    .start_test = start_test_ft5672,          // +0x30 -> 0x1dc3c   (F2b)
    .open_test = fts_open_test,               // +0x38 -> 0x240fc
    .short_test = fts_short_test,             // +0x40 -> 0x242d4
    .spi_test = fts_spi_test,                 // +0x48 -> 0x244ac
    .data_dump = ft3658_data_dump,            // +0x50 -> 0x246bc
    .save_data_private = save_data_ft5672,    // +0x58 -> 0x24780   (F2b)
    .rawshift_fre_test = fts_rawshift_fre_test,           // +0x60 -> 0x27f8c (F2c)
    .rawshift_pic_black_test = fts_rawshift_pic_black_test, // +0x68 -> 0x28680 (F2c)
    .rawshift_pic_white_test = fts_rawshift_pic_white_test, // +0x70 -> 0x2877c (F2c)
    .free_item_data = free_item_data,         // +0x78 -> 0x28878
};
