/* SPDX-License-Identifier: BSD-1-Clause */
#ifndef IMSPROG_CH347_WIN32_H
#define IMSPROG_CH347_WIN32_H
#include "ch347.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Strings are bounded process-local snapshots, valid until the next operation. */
const char *ch347_windows_last_error(void);
const char *ch347_windows_last_report(void);
int ch347_windows_diagnose(void);
void ch347_windows_set_error_stage(const char *stage, int error_code);
int ch347_windows_open(struct ch347_priv *p);
int ch341_windows_open(struct ch347_priv *p);
int ch341_windows_diagnose(void);
int ch341_windows_exchange(struct ch347_priv *p, const uint8_t *tx, unsigned int tx_len,
                           uint8_t *rx, unsigned int rx_len);
void ch347_windows_close(struct ch347_priv *p);
int ch347_windows_transfer(struct ch347_priv *p, unsigned char endpoint,
                           unsigned char *data, int length, int *transferred,
                           unsigned int timeout);
#ifdef __cplusplus
}
#endif
#endif
