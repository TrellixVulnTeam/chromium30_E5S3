#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Entry point for fully-annotated builds.

This script is part of the effort to move all builds to annotator-based
systems. Any builder configured to use the AnnotatorFactory.BaseFactory()
found in scripts/master/factory/annotator_factory.py executes a single
AddAnnotatedScript step. That step (found in annotator_commands.py) calls
this script with the build- and factory-properties passed on the command
line.

The main mode of operation is for factory_properties to contain a single
property 'recipe' whose value is the basename (without extension) of a python
script in one of the following locations (looked up in this order):
  * build_internal/scripts/slave-internal/recipes
  * build_internal/scripts/slave/recipes
  * build/scripts/slave/recipes

For example, these factory_properties would run the 'run_presubmit' recipe
located in build/scripts/slave/recipes:
    { 'recipe': 'run_presubmit' }

Annotated_run.py will then import the recipe and expect to call a function whose
signature is:
  GetSteps(api, properties) -> iterable_of_things.

properties is a merged view of factory_properties with build_properties.

Items in iterable_of_things must be one of:
  * A step dictionary (as accepted by annotator.py)
  * A sequence of step dictionaries
  * A step generator
Iterable_of_things is also permitted to be a raw step generator.

A step generator is called with the following protocol:
  * The generator is initialized with 'step_history' and 'failed'.
  * Each iteration of the generator is passed the current value of 'failed'.

On each iteration, a step generator may yield:
  * A single step dictionary
  * A sequence of step dictionaries
    * If a sequence of dictionaries is yielded, and the first step dictionary
      does not have a 'seed_steps' key, the first step will be augmented with
      a 'seed_steps' key containing the names of all the steps in the sequence.

For steps yielded by the generator, if annotated_run enters the failed state,
it will only continue to call the generator if the generator sets the
'keep_going' key on the steps which it has produced. Otherwise annoated_run will
cease calling the generator and move on to the next item in iterable_of_things.

'step_history' is an OrderedDict of {stepname -> StepData}, always representing
    the current history of what steps have run, what they returned, and any
    json data they emitted. Additionally, the OrderedDict has the following
    convenience functions defined:
      * last_step   - Returns the last step that ran or None
      * nth_step(n) - Returns the N'th step that ran or None

'failed' is a boolean representing if the build is in a 'failed' state.
"""

import copy
import inspect
import optparse
import os
import subprocess
import sys

import cStringIO

import common.python26_polyfill  # pylint: disable=W0611
import collections  # Import after polyfill to get OrderedDict on 2.6

from common import annotator
from common import chromium_utils
from slave import recipe_api

SCRIPT_PATH = os.path.dirname(os.path.abspath(__file__))
BUILD_ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))
ROOT_PATH = os.path.abspath(os.path.join(
  SCRIPT_PATH, os.pardir, os.pardir, os.pardir))
MODULE_DIRS = [os.path.join(x, 'recipe_modules') for x in [
  SCRIPT_PATH,
  os.path.join(ROOT_PATH, 'build_internal', 'scripts', 'slave')
]]


class StepPresentation(object):
  STATUSES = set(('SUCCESS', 'FAILURE', 'WARNING', 'EXCEPTION'))

  def __init__(self):
    self._finalized = False

    self._logs = collections.OrderedDict()
    self._perf_logs = collections.OrderedDict()
    self._status = None
    self._step_summary_text = None
    self._step_text = None

  # (E0202) pylint bug: http://www.logilab.org/ticket/89092
  @property
  def status(self):  # pylint: disable=E0202
    return self._status

  @status.setter
  def status(self, val):  # pylint: disable=E0202
    assert not self._finalized
    assert val in self.STATUSES
    self._status = val

  @property
  def step_text(self):
    return self._step_text

  @step_text.setter
  def step_text(self, val):
    assert not self._finalized
    self._step_text = val

  @property
  def step_summary_text(self):
    return self._step_summary_text

  @step_summary_text.setter
  def step_summary_text(self, val):
    assert not self._finalized
    self._step_summary_text = val

  @property
  def logs(self):
    if not self._finalized:
      return self._logs
    else:
      return copy.deepcopy(self._logs)

  @property
  def perf_logs(self):
    if not self._finalized:
      return self._perf_logs
    else:
      return copy.deepcopy(self._perf_logs)

  def finalize(self, annotator_step):
    self._finalized = True
    if self.step_text:
      annotator_step.step_text(self.step_text)
    if self.step_summary_text:
      annotator_step.step_summary_text(self.step_summary_text)
    for name, lines in self.logs.iteritems():
      annotator_step.write_log_lines(name, lines)
    for name, lines in self.perf_logs.iteritems():
      annotator_step.write_log_lines(name, lines, perf=True)
    status_mapping = {
      'WARNING': annotator_step.step_warnings,
      'FAILURE': annotator_step.step_failure,
      'EXCEPTION': annotator_step.step_exception,
    }
    status_mapping.get(self.status, lambda: None)()


class StepData(object):
  def __init__(self, step, retcode):
    self._retcode = retcode
    self._step = step

    self._presentation = StepPresentation()

  @property
  def step(self):
    return copy.deepcopy(self._step)

  @property
  def retcode(self):
    return self._retcode

  @property
  def presentation(self):
    return self._presentation


def flattened(sequence):
  for item in sequence:
    if isinstance(item, collections.Sequence):
      for sub_item in flattened(item):
        yield sub_item
    else:
      yield item


def fixup_seed_steps(sequence):
  """Takes a sequence of step dicts and adds seed_steps to the first entry
  if appropriate.

  Returns the sequence for convenience.
  """
  seed_steps = None
  for step in sequence:
    if not seed_steps:
      if 'seed_steps' in step:
        break
      seed_steps = step['seed_steps'] = []
    seed_steps.append(step['name'])
  return sequence


def ensure_sequence_of_steps(step_or_steps):
  """Generates one or more fixed steps, given a step or a sequence of steps."""
  if isinstance(step_or_steps, dict):
    yield step_or_steps
  elif isinstance(step_or_steps, collections.Sequence):
    for s in fixup_seed_steps(list(flattened(step_or_steps))):
      yield s
  elif inspect.isgenerator(step_or_steps):
    for i in step_or_steps:
      for s in ensure_sequence_of_steps(i):
        yield s
  else:
    assert False, 'Item is not a sequence or a step: %s' % (step_or_steps,)


def render_step(step, test_data):
  """Renders a step so that it can be fed to annotator.py.

  Args:
    test_data: The test data json dictionary for this step, if any.
               Passed through unaltered to each placeholder.

  Returns any placeholder instances that were found while rendering the step.
  """
  placeholders = collections.defaultdict(list)
  new_cmd = []
  for item in step['cmd']:
    if isinstance(item, recipe_api.Placeholder):
      # __module__ is in the form of ...recipe_modules.<api_name>.*
      api_name = item.__module__.split('.')[-2]
      tdata = None if test_data is None else test_data.get(api_name, {})
      new_cmd.extend(item.render(tdata))
      placeholders[api_name].append(item)
    else:
      new_cmd.append(item)
  step['cmd'] = new_cmd
  return placeholders


def call_placeholders(step_result, placeholders, test_data):
  class BlankObject(object):
    pass
  additions = {}
  for api, items in placeholders.iteritems():
    additions[api] = BlankObject()
    test_datum = None if test_data is None else test_data.get(api, {})
    for placeholder in items:
      placeholder.step_finished(step_result.presentation, additions[api],
                                test_datum)
  for api, obj in additions.iteritems():
    setattr(step_result, api, obj)


def step_callback(step, step_history, placeholders, test_data_item):
  followup_fn = step.pop('followup_fn', None)

  def _inner(annotator_step, retcode):
    step_result = StepData(step, retcode)
    if retcode > 0:
      step_result.presentation.status = 'FAILURE'

    step_history[step['name']] = step_result
    annotator_step.annotation_stream.step_cursor(step['name'])
    if test_data_item is None:
      # To avoid cluttering the expectations, don't emit this in testmode.
      annotator_step.emit('step returned non-zero exit code: %d' %
                          step_result.retcode)

    call_placeholders(step_result, placeholders, test_data_item)

    if followup_fn:
      followup_fn(step_result)

    step_result.presentation.finalize(annotator_step)
    return step_result

  return _inner


def get_args(argv):
  """Process command-line arguments."""

  parser = optparse.OptionParser(
      description='Entry point for annotated builds.')
  parser.add_option('--build-properties',
                    action='callback', callback=chromium_utils.convert_json,
                    type='string', default={},
                    help='build properties in JSON format')
  parser.add_option('--factory-properties',
                    action='callback', callback=chromium_utils.convert_json,
                    type='string', default={},
                    help='factory properties in JSON format')
  parser.add_option('--keep-stdin', action='store_true', default=False,
                    help='don\'t close stdin when running recipe steps')
  return parser.parse_args(argv)


def main(argv=None):
  opts, _ = get_args(argv)

  stream = annotator.StructuredAnnotationStream(seed_steps=['setup_build'])

  ret = run_steps(stream, opts.build_properties, opts.factory_properties)
  return ret.status_code


def run_steps(stream, build_properties, factory_properties,
              api=recipe_api.CreateRecipeApi, test_data=None):
  """Returns a tuple of (status_code, steps_ran).

  Only one of these values will be set at a time. This is mainly to support the
  testing interface used by unittests/recipes_test.py.

  test_data should be a dictionary of step_name -> (retcode, json_data)
  """
  stream.honor_zero_return_code()
  MakeStepsRetval = collections.namedtuple('MakeStepsRetval',
                                           'status_code steps_ran')

  # TODO(iannucci): Stop this when blamelist becomes sane data.
  if ('blamelist_real' in build_properties and
      'blamelist' in build_properties):
    build_properties['blamelist'] = build_properties['blamelist_real']
    del build_properties['blamelist_real']

  step_history = collections.OrderedDict()
  with stream.step('setup_build') as s:
    assert 'recipe' in factory_properties
    recipe = factory_properties['recipe']

    # If the recipe is specified as "module:recipe", then it is an recipe
    # contained in a recipe_module as an example. Look for it in the modules
    # imported by load_recipe_modules instead of the normal search paths.
    if ':' in recipe:
      module_name, recipe = recipe.split(':')
      assert recipe.endswith('example')
      RECIPE_MODULES = recipe_api.load_recipe_modules(MODULE_DIRS)
      try:
        recipe_module = getattr(getattr(RECIPE_MODULES, module_name), recipe)
      except AttributeError:
        s.step_text('recipe not found')
        s.step_failure()
        return MakeStepsRetval(2, None)

    else:
      recipe_dirs = (os.path.abspath(p) for p in (
          os.path.join(SCRIPT_PATH, '..', '..', '..', 'build_internal',
                       'scripts', 'slave-internal', 'recipes'),
          os.path.join(SCRIPT_PATH, '..', '..', '..', 'build_internal',
                       'scripts', 'slave', 'recipes'),
          os.path.join(SCRIPT_PATH, 'recipes'),
      ))

      for recipe_path in (os.path.join(p, recipe) for p in recipe_dirs):
        recipe_module = chromium_utils.IsolatedImportFromPath(recipe_path)
        if recipe_module:
          break
      else:
        s.step_text('recipe not found')
        s.step_failure()
        return MakeStepsRetval(2, None)

    properties = factory_properties.copy()
    properties.update(build_properties)
    stream.emit('Running recipe with %s' % (properties,))
    steps = recipe_module.GenSteps(api(recipe_module.DEPS,
                                       mod_dirs=MODULE_DIRS,
                                       properties=properties,
                                       step_history=step_history))
    assert inspect.isgenerator(steps)

  # Execute annotator.py with steps if specified.
  # annotator.py handles the seeding, execution, and annotation of each step.
  failed = False

  test_mode = test_data is not None

  for step in ensure_sequence_of_steps(steps):
    test_data_item = test_data.pop(step['name'], {}) if test_mode else None
    placeholders = render_step(step, test_data_item)

    assert step['name'] not in step_history, (
      'Step "%s" is already in step_history!' % step['name'])

    callback = step_callback(step, step_history, placeholders, test_data_item)

    if not test_mode:
      step_result = annotator.run_step(
        stream, failed, followup_fn=callback, **step)
    else:
      with stream.step(step['name']) as s:
        s.stream = cStringIO.StringIO()
        step_result = callback(s, test_data_item.pop('$R', 0))
        lines = filter(None, s.stream.getvalue().splitlines())
        if lines:
          # Note that '~' sorts after 'z' so that this will be last on each
          # step. Also use _step to get access to the mutable step dictionary.
          # pylint: disable=W0212
          step_result._step['~followup_annotations'] = lines

    # TODO(iannucci): Pull this failure calculation into callback.
    failed = annotator.update_build_failure(failed, step_result.retcode, **step)

  assert not test_mode or test_data == {}, (
    "Unconsumed test data! %s" % (test_data,))

  return MakeStepsRetval(0 if not failed else 1, step_history)


def UpdateScripts():
  if os.environ.get('RUN_SLAVE_UPDATED_SCRIPTS'):
    os.environ.pop('RUN_SLAVE_UPDATED_SCRIPTS')
    return False
  stream = annotator.StructuredAnnotationStream(seed_steps=['update_scripts'])
  with stream.step('update_scripts') as s:
    build_root = os.path.join(SCRIPT_PATH, '..', '..')
    gclient_name = 'gclient'
    if sys.platform.startswith('win'):
      gclient_name += '.bat'
    gclient_path = os.path.join(build_root, '..', 'depot_tools', gclient_name)
    if subprocess.call([gclient_path, 'sync', '--force'], cwd=build_root) != 0:
      s.step_text('gclient sync failed!')
      s.step_warnings()
    os.environ['RUN_SLAVE_UPDATED_SCRIPTS'] = '1'
    return True


def shell_main(argv):
  if UpdateScripts():
    return subprocess.call([sys.executable] + argv)
  else:
    return main(argv)


if __name__ == '__main__':
  sys.exit(shell_main(sys.argv))
