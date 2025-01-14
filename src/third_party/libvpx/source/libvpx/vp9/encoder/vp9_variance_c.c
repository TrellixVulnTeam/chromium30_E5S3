/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vp9/encoder/vp9_variance.h"
#include "vp9/common/vp9_filter.h"
#include "vp9/common/vp9_subpelvar.h"
#include "vpx/vpx_integer.h"
#include "vpx_ports/mem.h"
#include "./vp9_rtcd.h"

unsigned int vp9_get_mb_ss_c(const int16_t *src_ptr) {
  unsigned int i, sum = 0;

  for (i = 0; i < 256; i++) {
    sum += (src_ptr[i] * src_ptr[i]);
  }

  return sum;
}

unsigned int vp9_variance64x32_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 64, 32, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 11));
}

unsigned int vp9_sub_pixel_variance64x32_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 64, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 64, 64, 32, 64, vfilter);

  return vp9_variance64x32(temp2, 64, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance64x32_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 64 * 64);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 64, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 64, 64, 32, 64, vfilter);
  comp_avg_pred(temp3, second_pred, 64, 32, temp2, 64);
  return vp9_variance64x32(temp3, 64, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_variance32x64_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 32, 64, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 11));
}

unsigned int vp9_sub_pixel_variance32x64_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 65, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 64, 32, vfilter);

  return vp9_variance32x64(temp2, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance32x64_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 32 * 64);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 65, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 64, 32, vfilter);
  comp_avg_pred(temp3, second_pred, 32, 64, temp2, 32);
  return vp9_variance32x64(temp3, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_variance32x16_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 32, 16, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 9));
}

unsigned int vp9_sub_pixel_variance32x16_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 16, 32, vfilter);

  return vp9_variance32x16(temp2, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance32x16_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 32 * 16);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 16, 32, vfilter);
  comp_avg_pred(temp3, second_pred, 32, 16, temp2, 32);
  return vp9_variance32x16(temp3, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_variance16x32_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 32, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 9));
}

unsigned int vp9_sub_pixel_variance16x32_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 32, 16, vfilter);

  return vp9_variance16x32(temp2, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance16x32_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 16 * 32);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 32, 16, vfilter);
  comp_avg_pred(temp3, second_pred, 16, 32, temp2, 16);
  return vp9_variance16x32(temp3, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_variance64x64_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 64, 64, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 12));
}

unsigned int vp9_variance32x32_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 32, 32, &var, &avg);
  *sse = var;
  return (var - (((int64_t)avg * avg) >> 10));
}

unsigned int vp9_variance16x16_c(const uint8_t *src_ptr,
                                 int  source_stride,
                                 const uint8_t *ref_ptr,
                                 int  recon_stride,
                                 unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 16, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 8));
}

unsigned int vp9_variance8x16_c(const uint8_t *src_ptr,
                                int  source_stride,
                                const uint8_t *ref_ptr,
                                int  recon_stride,
                                unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 8, 16, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 7));
}

unsigned int vp9_variance16x8_c(const uint8_t *src_ptr,
                                int  source_stride,
                                const uint8_t *ref_ptr,
                                int  recon_stride,
                                unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 8, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 7));
}

void vp9_get_sse_sum_8x8_c(const uint8_t *src_ptr, int source_stride,
                       const uint8_t *ref_ptr, int ref_stride,
                       unsigned int *sse, int *sum) {
  variance(src_ptr, source_stride, ref_ptr, ref_stride, 8, 8, sse, sum);
}

unsigned int vp9_variance8x8_c(const uint8_t *src_ptr,
                               int  source_stride,
                               const uint8_t *ref_ptr,
                               int  recon_stride,
                               unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 8, 8, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 6));
}

unsigned int vp9_variance8x4_c(const uint8_t *src_ptr,
                               int  source_stride,
                               const uint8_t *ref_ptr,
                               int  recon_stride,
                               unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 8, 4, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 5));
}

unsigned int vp9_variance4x8_c(const uint8_t *src_ptr,
                               int  source_stride,
                               const uint8_t *ref_ptr,
                               int  recon_stride,
                               unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 4, 8, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 5));
}

unsigned int vp9_variance4x4_c(const uint8_t *src_ptr,
                               int  source_stride,
                               const uint8_t *ref_ptr,
                               int  recon_stride,
                               unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 4, 4, &var, &avg);
  *sse = var;
  return (var - (((unsigned int)avg * avg) >> 4));
}


unsigned int vp9_mse16x16_c(const uint8_t *src_ptr,
                            int  source_stride,
                            const uint8_t *ref_ptr,
                            int  recon_stride,
                            unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 16, &var, &avg);
  *sse = var;
  return var;
}

unsigned int vp9_mse16x8_c(const uint8_t *src_ptr,
                           int  source_stride,
                           const uint8_t *ref_ptr,
                           int  recon_stride,
                           unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 8, &var, &avg);
  *sse = var;
  return var;
}

unsigned int vp9_mse8x16_c(const uint8_t *src_ptr,
                           int  source_stride,
                           const uint8_t *ref_ptr,
                           int  recon_stride,
                           unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 8, 16, &var, &avg);
  *sse = var;
  return var;
}

unsigned int vp9_mse8x8_c(const uint8_t *src_ptr,
                          int  source_stride,
                          const uint8_t *ref_ptr,
                          int  recon_stride,
                          unsigned int *sse) {
  unsigned int var;
  int avg;

  variance(src_ptr, source_stride, ref_ptr, recon_stride, 8, 8, &var, &avg);
  *sse = var;
  return var;
}


unsigned int vp9_sub_pixel_variance4x4_c(const uint8_t *src_ptr,
                                         int  src_pixels_per_line,
                                         int  xoffset,
                                         int  yoffset,
                                         const uint8_t *dst_ptr,
                                         int dst_pixels_per_line,
                                         unsigned int *sse) {
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;
  uint16_t fdata3[5 * 4];  // Temp data bufffer used in filtering

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  // First filter 1d Horizontal
  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 5, 4, hfilter);

  // Now filter Verticaly
  var_filter_block2d_bil_second_pass(fdata3, temp2, 4,  4,  4,  4, vfilter);

  return vp9_variance4x4(temp2, 4, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance4x4_c(const uint8_t *src_ptr,
                                             int  src_pixels_per_line,
                                             int  xoffset,
                                             int  yoffset,
                                             const uint8_t *dst_ptr,
                                             int dst_pixels_per_line,
                                             unsigned int *sse,
                                             const uint8_t *second_pred) {
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 4 * 4);  // compound pred buffer
  uint16_t fdata3[5 * 4];  // Temp data bufffer used in filtering

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  // First filter 1d Horizontal
  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 5, 4, hfilter);

  // Now filter Verticaly
  var_filter_block2d_bil_second_pass(fdata3, temp2, 4,  4,  4,  4, vfilter);
  comp_avg_pred(temp3, second_pred, 4, 4, temp2, 4);
  return vp9_variance4x4(temp3, 4, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance8x8_c(const uint8_t *src_ptr,
                                         int  src_pixels_per_line,
                                         int  xoffset,
                                         int  yoffset,
                                         const uint8_t *dst_ptr,
                                         int dst_pixels_per_line,
                                         unsigned int *sse) {
  uint16_t fdata3[9 * 8];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 8, 8, vfilter);

  return vp9_variance8x8(temp2, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance8x8_c(const uint8_t *src_ptr,
                                             int  src_pixels_per_line,
                                             int  xoffset,
                                             int  yoffset,
                                             const uint8_t *dst_ptr,
                                             int dst_pixels_per_line,
                                             unsigned int *sse,
                                             const uint8_t *second_pred) {
  uint16_t fdata3[9 * 8];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 8 * 8);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 8, 8, vfilter);
  comp_avg_pred(temp3, second_pred, 8, 8, temp2, 8);
  return vp9_variance8x8(temp3, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance16x16_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[17 * 16];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 16, 16, vfilter);

  return vp9_variance16x16(temp2, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance16x16_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[17 * 16];
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 16 * 16);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 16, 16, vfilter);

  comp_avg_pred(temp3, second_pred, 16, 16, temp2, 16);
  return vp9_variance16x16(temp3, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance64x64_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 65, 64, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 64, 64, 64, 64, vfilter);

  return vp9_variance64x64(temp2, 64, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance64x64_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[65 * 64];  // Temp data bufffer used in filtering
  uint8_t temp2[68 * 64];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 64 * 64);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 65, 64, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 64, 64, 64, 64, vfilter);
  comp_avg_pred(temp3, second_pred, 64, 64, temp2, 64);
  return vp9_variance64x64(temp3, 64, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance32x32_c(const uint8_t *src_ptr,
                                           int  src_pixels_per_line,
                                           int  xoffset,
                                           int  yoffset,
                                           const uint8_t *dst_ptr,
                                           int dst_pixels_per_line,
                                           unsigned int *sse) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 32, 32, vfilter);

  return vp9_variance32x32(temp2, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance32x32_c(const uint8_t *src_ptr,
                                               int  src_pixels_per_line,
                                               int  xoffset,
                                               int  yoffset,
                                               const uint8_t *dst_ptr,
                                               int dst_pixels_per_line,
                                               unsigned int *sse,
                                               const uint8_t *second_pred) {
  uint16_t fdata3[33 * 32];  // Temp data bufffer used in filtering
  uint8_t temp2[36 * 32];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 32 * 32);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 33, 32, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 32, 32, 32, 32, vfilter);
  comp_avg_pred(temp3, second_pred, 32, 32, temp2, 32);
  return vp9_variance32x32(temp3, 32, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_variance_halfpixvar16x16_h_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance16x16_c(src_ptr, source_stride, 8, 0,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar32x32_h_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance32x32_c(src_ptr, source_stride, 8, 0,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar64x64_h_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance64x64_c(src_ptr, source_stride, 8, 0,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar16x16_v_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance16x16_c(src_ptr, source_stride, 0, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar32x32_v_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance32x32_c(src_ptr, source_stride, 0, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar64x64_v_c(const uint8_t *src_ptr,
                                              int  source_stride,
                                              const uint8_t *ref_ptr,
                                              int  recon_stride,
                                              unsigned int *sse) {
  return vp9_sub_pixel_variance64x64_c(src_ptr, source_stride, 0, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar16x16_hv_c(const uint8_t *src_ptr,
                                               int  source_stride,
                                               const uint8_t *ref_ptr,
                                               int  recon_stride,
                                               unsigned int *sse) {
  return vp9_sub_pixel_variance16x16_c(src_ptr, source_stride, 8, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar32x32_hv_c(const uint8_t *src_ptr,
                                               int  source_stride,
                                               const uint8_t *ref_ptr,
                                               int  recon_stride,
                                               unsigned int *sse) {
  return vp9_sub_pixel_variance32x32_c(src_ptr, source_stride, 8, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_variance_halfpixvar64x64_hv_c(const uint8_t *src_ptr,
                                               int  source_stride,
                                               const uint8_t *ref_ptr,
                                               int  recon_stride,
                                               unsigned int *sse) {
  return vp9_sub_pixel_variance64x64_c(src_ptr, source_stride, 8, 8,
                                       ref_ptr, recon_stride, sse);
}

unsigned int vp9_sub_pixel_mse16x16_c(const uint8_t *src_ptr,
                                      int  src_pixels_per_line,
                                      int  xoffset,
                                      int  yoffset,
                                      const uint8_t *dst_ptr,
                                      int dst_pixels_per_line,
                                      unsigned int *sse) {
  vp9_sub_pixel_variance16x16_c(src_ptr, src_pixels_per_line,
                                xoffset, yoffset, dst_ptr,
                                dst_pixels_per_line, sse);
  return *sse;
}

unsigned int vp9_sub_pixel_mse32x32_c(const uint8_t *src_ptr,
                                      int  src_pixels_per_line,
                                      int  xoffset,
                                      int  yoffset,
                                      const uint8_t *dst_ptr,
                                      int dst_pixels_per_line,
                                      unsigned int *sse) {
  vp9_sub_pixel_variance32x32_c(src_ptr, src_pixels_per_line,
                                xoffset, yoffset, dst_ptr,
                                dst_pixels_per_line, sse);
  return *sse;
}

unsigned int vp9_sub_pixel_mse64x64_c(const uint8_t *src_ptr,
                                      int  src_pixels_per_line,
                                      int  xoffset,
                                      int  yoffset,
                                      const uint8_t *dst_ptr,
                                      int dst_pixels_per_line,
                                      unsigned int *sse) {
  vp9_sub_pixel_variance64x64_c(src_ptr, src_pixels_per_line,
                                xoffset, yoffset, dst_ptr,
                                dst_pixels_per_line, sse);
  return *sse;
}

unsigned int vp9_sub_pixel_variance16x8_c(const uint8_t *src_ptr,
                                          int  src_pixels_per_line,
                                          int  xoffset,
                                          int  yoffset,
                                          const uint8_t *dst_ptr,
                                          int dst_pixels_per_line,
                                          unsigned int *sse) {
  uint16_t fdata3[16 * 9];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 8, 16, vfilter);

  return vp9_variance16x8(temp2, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance16x8_c(const uint8_t *src_ptr,
                                              int  src_pixels_per_line,
                                              int  xoffset,
                                              int  yoffset,
                                              const uint8_t *dst_ptr,
                                              int dst_pixels_per_line,
                                              unsigned int *sse,
                                              const uint8_t *second_pred) {
  uint16_t fdata3[16 * 9];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 16 * 8);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 16, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 16, 16, 8, 16, vfilter);
  comp_avg_pred(temp3, second_pred, 16, 8, temp2, 16);
  return vp9_variance16x8(temp3, 16, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance8x16_c(const uint8_t *src_ptr,
                                          int  src_pixels_per_line,
                                          int  xoffset,
                                          int  yoffset,
                                          const uint8_t *dst_ptr,
                                          int dst_pixels_per_line,
                                          unsigned int *sse) {
  uint16_t fdata3[9 * 16];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 16, 8, vfilter);

  return vp9_variance8x16(temp2, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance8x16_c(const uint8_t *src_ptr,
                                              int  src_pixels_per_line,
                                              int  xoffset,
                                              int  yoffset,
                                              const uint8_t *dst_ptr,
                                              int dst_pixels_per_line,
                                              unsigned int *sse,
                                              const uint8_t *second_pred) {
  uint16_t fdata3[9 * 16];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 8 * 16);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 17, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 16, 8, vfilter);
  comp_avg_pred(temp3, second_pred, 8, 16, temp2, 8);
  return vp9_variance8x16(temp3, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance8x4_c(const uint8_t *src_ptr,
                                         int  src_pixels_per_line,
                                         int  xoffset,
                                         int  yoffset,
                                         const uint8_t *dst_ptr,
                                         int dst_pixels_per_line,
                                         unsigned int *sse) {
  uint16_t fdata3[8 * 5];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 5, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 4, 8, vfilter);

  return vp9_variance8x4(temp2, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance8x4_c(const uint8_t *src_ptr,
                                             int  src_pixels_per_line,
                                             int  xoffset,
                                             int  yoffset,
                                             const uint8_t *dst_ptr,
                                             int dst_pixels_per_line,
                                             unsigned int *sse,
                                             const uint8_t *second_pred) {
  uint16_t fdata3[8 * 5];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 8 * 4);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 5, 8, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 8, 8, 4, 8, vfilter);
  comp_avg_pred(temp3, second_pred, 8, 4, temp2, 8);
  return vp9_variance8x4(temp3, 8, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_variance4x8_c(const uint8_t *src_ptr,
                                         int  src_pixels_per_line,
                                         int  xoffset,
                                         int  yoffset,
                                         const uint8_t *dst_ptr,
                                         int dst_pixels_per_line,
                                         unsigned int *sse) {
  uint16_t fdata3[5 * 8];  // Temp data bufffer used in filtering
  // FIXME(jingning,rbultje): this temp2 buffer probably doesn't need to be
  // of this big? same issue appears in all other block size settings.
  uint8_t temp2[20 * 16];
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 4, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 4, 4, 8, 4, vfilter);

  return vp9_variance4x8(temp2, 4, dst_ptr, dst_pixels_per_line, sse);
}

unsigned int vp9_sub_pixel_avg_variance4x8_c(const uint8_t *src_ptr,
                                             int  src_pixels_per_line,
                                             int  xoffset,
                                             int  yoffset,
                                             const uint8_t *dst_ptr,
                                             int dst_pixels_per_line,
                                             unsigned int *sse,
                                             const uint8_t *second_pred) {
  uint16_t fdata3[5 * 8];  // Temp data bufffer used in filtering
  uint8_t temp2[20 * 16];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, temp3, 4 * 8);  // compound pred buffer
  const int16_t *hfilter, *vfilter;

  hfilter = VP9_BILINEAR_FILTERS_2TAP(xoffset);
  vfilter = VP9_BILINEAR_FILTERS_2TAP(yoffset);

  var_filter_block2d_bil_first_pass(src_ptr, fdata3, src_pixels_per_line,
                                    1, 9, 4, hfilter);
  var_filter_block2d_bil_second_pass(fdata3, temp2, 4, 4, 8, 4, vfilter);
  comp_avg_pred(temp3, second_pred, 4, 8, temp2, 4);
  return vp9_variance4x8(temp3, 4, dst_ptr, dst_pixels_per_line, sse);
}
