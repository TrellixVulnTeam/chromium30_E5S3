/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vp9/common/vp9_blockd.h"
#include "vp9/encoder/vp9_onyx_int.h"
#include "vp9/encoder/vp9_treewriter.h"
#include "vp9/common/vp9_entropymode.h"


void vp9_init_mode_costs(VP9_COMP *c) {
  VP9_COMMON *x = &c->common;
  const vp9_tree_p KT = vp9_intra_mode_tree;
  int i, j;

  for (i = 0; i < VP9_INTRA_MODES; i++) {
    for (j = 0; j < VP9_INTRA_MODES; j++) {
      vp9_cost_tokens((int *)c->mb.y_mode_costs[i][j],
                      x->kf_y_mode_prob[i][j], KT);
    }
  }

  // TODO(rbultje) separate tables for superblock costing?
  vp9_cost_tokens(c->mb.mbmode_cost, x->fc.y_mode_prob[1],
                  vp9_intra_mode_tree);
  vp9_cost_tokens(c->mb.intra_uv_mode_cost[1],
                  x->fc.uv_mode_prob[VP9_INTRA_MODES - 1], vp9_intra_mode_tree);
  vp9_cost_tokens(c->mb.intra_uv_mode_cost[0],
                  x->kf_uv_mode_prob[VP9_INTRA_MODES - 1], vp9_intra_mode_tree);

  for (i = 0; i <= VP9_SWITCHABLE_FILTERS; ++i)
    vp9_cost_tokens((int *)c->mb.switchable_interp_costs[i],
                    x->fc.switchable_interp_prob[i],
                    vp9_switchable_interp_tree);
}
