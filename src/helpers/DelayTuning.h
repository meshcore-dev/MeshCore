#pragma once

#include <stdint.h>

/**
 * Empirically tuned retransmit delay factors indexed by active neighbor count.
 *
 * Each entry is a multiplier applied to the packet airtime (see
 * Mesh::getRetransmitDelay / getDirectRetransmitDelay). The formulas used are:
 *
 *     tx_delay_factor(n)        = 2.4 + 0.2 * n^1.9
 *     direct_tx_delay_factor(n) = 1.1 + 0.6 * n^1.9
 *
 * Values were derived from mesh simulator sweeps across sparse / medium /
 * dense / very_dense topologies (max neighbor count = 24). Compared to the
 * legacy fixed defaults (tx=0.5, direct=0.3), the auto-tuned curve trades
 * a small amount of raw flood delivery for a large ACK-reliability gain in
 * congested topologies.
 *
 * Indices beyond DELAY_TUNING_TABLE_SIZE-1 clamp to the last entry.
 */

struct DelayTuning {
  float tx_delay_factor;
  float direct_tx_delay_factor;
};

static const DelayTuning DELAY_TUNING_TABLE[] = {
  {  2.40f,    1.10f},  //  0 neighbors (isolated)
  {  2.60f,    1.70f},  //  1
  {  3.15f,    3.34f},  //  2
  {  4.01f,    5.94f},  //  3
  {  5.19f,    9.46f},  //  4
  {  6.66f,   13.87f},  //  5
  {  8.42f,   19.16f},  //  6
  { 10.47f,   25.30f},  //  7
  { 12.80f,   32.29f},  //  8
  { 15.40f,   40.11f},  //  9
  { 18.29f,   48.76f},  // 10
  { 21.44f,   58.22f},  // 11
  { 24.86f,   68.49f},  // 12
  { 28.55f,   79.56f},  // 13
  { 32.51f,   91.42f},  // 14
  { 36.72f,  104.07f},  // 15
  { 41.20f,  117.51f},  // 16
  { 45.94f,  131.72f},  // 17
  { 50.93f,  146.70f},  // 18
  { 56.19f,  162.46f},  // 19
  { 61.69f,  178.97f},  // 20
  { 67.45f,  196.25f},  // 21
  { 73.46f,  214.28f},  // 22
  { 79.72f,  233.07f},  // 23
  { 86.24f,  252.61f},  // 24+ (very dense)
};

#define DELAY_TUNING_TABLE_SIZE  (sizeof(DELAY_TUNING_TABLE) / sizeof(DELAY_TUNING_TABLE[0]))

static inline const DelayTuning& lookupDelayTuning(int n) {
  if (n < 0) n = 0;
  if (n >= (int)DELAY_TUNING_TABLE_SIZE) n = DELAY_TUNING_TABLE_SIZE - 1;
  return DELAY_TUNING_TABLE[n];
}
