/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rodin: stand-in for the xiaomi fingerprint SPI interface header. The
 * vendor tree ships a symlink into the proprietary mifingerprint tree,
 * which is not part of this release; its only includer (spi-mt65xx.c)
 * consumes no symbol from it, so the guard-only form is sufficient for
 * the built-in build. Revisit if a real consumer appears.
 */
#ifndef __RODIN_FP_SPI_STUB_H
#define __RODIN_FP_SPI_STUB_H
#endif
