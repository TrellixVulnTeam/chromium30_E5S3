# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from slave import recipe_api

class GeneratorScriptApi(recipe_api.RecipeApi):
  def __call__(self, path_to_script, *args):  # pragma: no cover
    """Run a script and generate the steps emitted by that script."""
    yield self.m.step(
      'gen step(%s)' % self.m.path.basename(path_to_script),
      [path_to_script,] + list(args) + [self.m.json.output()],
      cwd=self.m.path.checkout())
    new_steps = self.m.step_history.last_step().json.output
    assert isinstance(new_steps, list)
    yield new_steps
