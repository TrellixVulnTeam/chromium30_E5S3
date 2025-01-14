// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/omnibox/omnibox_field_trial.h"

#include <string>

#include "base/metrics/field_trial.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/metrics/metrics_util.h"
#include "chrome/common/metrics/variations/variation_ids.h"
#include "chrome/common/metrics/variations/variations_util.h"

namespace {

// Field trial names.
const char kHUPCullRedirectsFieldTrialName[] = "OmniboxHUPCullRedirects";
const char kHUPCreateShorterMatchFieldTrialName[] =
    "OmniboxHUPCreateShorterMatch";
const char kStopTimerFieldTrialName[] = "OmniboxStopTimer";
const char kShortcutsScoringFieldTrialName[] = "OmniboxShortcutsScoring";
const char kBundledExperimentFieldTrialName[] = "OmniboxBundledExperimentV1";

// Rule names used by the bundled experiment.
const char kSearchHistoryRule[] = "SearchHistory";

// The autocomplete dynamic field trial name prefix.  Each field trial is
// configured dynamically and is retrieved automatically by Chrome during
// the startup.
const char kAutocompleteDynamicFieldTrialPrefix[] = "AutocompleteDynamicTrial_";
// The maximum number of the autocomplete dynamic field trials (aka layers).
const int kMaxAutocompleteDynamicFieldTrials = 5;

// Field trial experiment probabilities.

// For HistoryURL provider cull redirects field trial, put 0% ( = 0/100 )
// of the users in the don't-cull-redirects experiment group.
// TODO(mpearson): Remove this field trial and the code it uses once I'm
// sure it's no longer needed.
const base::FieldTrial::Probability kHUPCullRedirectsFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kHUPCullRedirectsFieldTrialExperimentFraction = 0;

// For HistoryURL provider create shorter match field trial, put 0%
// ( = 25/100 ) of the users in the don't-create-a-shorter-match
// experiment group.
// TODO(mpearson): Remove this field trial and the code it uses once I'm
// sure it's no longer needed.
const base::FieldTrial::Probability
    kHUPCreateShorterMatchFieldTrialDivisor = 100;
const base::FieldTrial::Probability
    kHUPCreateShorterMatchFieldTrialExperimentFraction = 0;

// Experiment group names.

const char kStopTimerExperimentGroupName[] = "UseStopTimer";

// Field trial IDs.
// Though they are not literally "const", they are set only once, in
// ActivateStaticTrials() below.

// Whether the static field trials have been initialized by
// ActivateStaticTrials() method.
bool static_field_trials_initialized = false;

// Field trial ID for the HistoryURL provider cull redirects experiment group.
int hup_dont_cull_redirects_experiment_group = 0;

// Field trial ID for the HistoryURL provider create shorter match
// experiment group.
int hup_dont_create_shorter_match_experiment_group = 0;


// Concatenates the autocomplete dynamic field trial prefix with a field trial
// ID to form a complete autocomplete field trial name.
std::string DynamicFieldTrialName(int id) {
  return base::StringPrintf("%s%d", kAutocompleteDynamicFieldTrialPrefix, id);
}

}  // namespace


void OmniboxFieldTrial::ActivateStaticTrials() {
  DCHECK(!static_field_trials_initialized);

  // Create the HistoryURL provider cull redirects field trial.
  // Make it expire on March 1, 2013.
  scoped_refptr<base::FieldTrial> trial(
      base::FieldTrialList::FactoryGetFieldTrial(
          kHUPCullRedirectsFieldTrialName, kHUPCullRedirectsFieldTrialDivisor,
          "Standard", 2013, 3, 1, base::FieldTrial::ONE_TIME_RANDOMIZED, NULL));
  hup_dont_cull_redirects_experiment_group =
      trial->AppendGroup("DontCullRedirects",
                         kHUPCullRedirectsFieldTrialExperimentFraction);

  // Create the HistoryURL provider create shorter match field trial.
  // Make it expire on March 1, 2013.
  trial = base::FieldTrialList::FactoryGetFieldTrial(
      kHUPCreateShorterMatchFieldTrialName,
      kHUPCreateShorterMatchFieldTrialDivisor, "Standard", 2013, 3, 1,
      base::FieldTrial::ONE_TIME_RANDOMIZED, NULL);
  hup_dont_create_shorter_match_experiment_group =
      trial->AppendGroup("DontCreateShorterMatch",
                         kHUPCreateShorterMatchFieldTrialExperimentFraction);

  static_field_trials_initialized = true;
}

void OmniboxFieldTrial::ActivateDynamicTrials() {
  // Initialize all autocomplete dynamic field trials.  This method may be
  // called multiple times.
  for (int i = 0; i < kMaxAutocompleteDynamicFieldTrials; ++i)
    base::FieldTrialList::FindValue(DynamicFieldTrialName(i));
}

int OmniboxFieldTrial::GetDisabledProviderTypes() {
  // Make sure that Autocomplete dynamic field trials are activated.  It's OK to
  // call this method multiple times.
  ActivateDynamicTrials();

  // Look for group names in form of "DisabledProviders_<mask>" where "mask"
  // is a bitmap of disabled provider types (AutocompleteProvider::Type).
  int provider_types = 0;
  for (int i = 0; i < kMaxAutocompleteDynamicFieldTrials; ++i) {
    std::string group_name = base::FieldTrialList::FindFullName(
        DynamicFieldTrialName(i));
    const char kDisabledProviders[] = "DisabledProviders_";
    if (!StartsWithASCII(group_name, kDisabledProviders, true))
      continue;
    int types = 0;
    if (!base::StringToInt(base::StringPiece(
            group_name.substr(strlen(kDisabledProviders))), &types)) {
      LOG(WARNING) << "Malformed DisabledProviders string: " << group_name;
      continue;
    }
    if (types == 0)
      LOG(WARNING) << "Expecting a non-zero bitmap; group = " << group_name;
    else
      provider_types |= types;
  }
  return provider_types;
}

void OmniboxFieldTrial::GetActiveSuggestFieldTrialHashes(
    std::vector<uint32>* field_trial_hashes) {
  field_trial_hashes->clear();
  for (int i = 0; i < kMaxAutocompleteDynamicFieldTrials; ++i) {
    const std::string& trial_name = DynamicFieldTrialName(i);
    if (base::FieldTrialList::TrialExists(trial_name))
      field_trial_hashes->push_back(metrics::HashName(trial_name));
  }
}

bool OmniboxFieldTrial::InHUPCullRedirectsFieldTrial() {
  return base::FieldTrialList::TrialExists(kHUPCullRedirectsFieldTrialName);
}

bool OmniboxFieldTrial::InHUPCullRedirectsFieldTrialExperimentGroup() {
  if (!base::FieldTrialList::TrialExists(kHUPCullRedirectsFieldTrialName))
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kHUPCullRedirectsFieldTrialName);
  return group == hup_dont_cull_redirects_experiment_group;
}

bool OmniboxFieldTrial::InHUPCreateShorterMatchFieldTrial() {
  return
      base::FieldTrialList::TrialExists(kHUPCreateShorterMatchFieldTrialName);
}

bool OmniboxFieldTrial::InHUPCreateShorterMatchFieldTrialExperimentGroup() {
  if (!base::FieldTrialList::TrialExists(kHUPCreateShorterMatchFieldTrialName))
    return false;

  // Return true if we're in the experiment group.
  const int group = base::FieldTrialList::FindValue(
      kHUPCreateShorterMatchFieldTrialName);
  return group == hup_dont_create_shorter_match_experiment_group;
}

bool OmniboxFieldTrial::InStopTimerFieldTrialExperimentGroup() {
  return (base::FieldTrialList::FindFullName(kStopTimerFieldTrialName) ==
          kStopTimerExperimentGroupName);
}

bool OmniboxFieldTrial::InZeroSuggestFieldTrial() {
  // Make sure that Autocomplete dynamic field trials are activated.  It's OK to
  // call this method multiple times.
  ActivateDynamicTrials();

  // Look for group names starting with "EnableZeroSuggest"
  for (int i = 0; i < kMaxAutocompleteDynamicFieldTrials; ++i) {
    const std::string& group_name = base::FieldTrialList::FindFullName(
        DynamicFieldTrialName(i));
    const char kEnableZeroSuggest[] = "EnableZeroSuggest";
    if (StartsWithASCII(group_name, kEnableZeroSuggest, true))
      return true;
  }
  return false;
}

// If the active group name starts with "MaxRelevance_", extract the
// int that immediately following that, returning true on success.
bool OmniboxFieldTrial::ShortcutsScoringMaxRelevance(int* max_relevance) {
  std::string group_name =
      base::FieldTrialList::FindFullName(kShortcutsScoringFieldTrialName);
  const char kMaxRelevanceGroupPrefix[] = "MaxRelevance_";
  if (!StartsWithASCII(group_name, kMaxRelevanceGroupPrefix, true))
    return false;
  if (!base::StringToInt(base::StringPiece(
          group_name.substr(strlen(kMaxRelevanceGroupPrefix))),
                         max_relevance)) {
    LOG(WARNING) << "Malformed MaxRelevance string: " << group_name;
    return false;
  }
  return true;
}

bool OmniboxFieldTrial::SearchHistoryPreventInlining(
    AutocompleteInput::PageClassification current_page_classification) {
  return OmniboxFieldTrial::GetValueForRuleInContext(
      kSearchHistoryRule, current_page_classification) == "PreventInlining";
}

bool OmniboxFieldTrial::SearchHistoryDisable(
    AutocompleteInput::PageClassification current_page_classification) {
  return OmniboxFieldTrial::GetValueForRuleInContext(
      kSearchHistoryRule, current_page_classification) == "Disable";
}

// Background and implementation details:
//
// Each experiment group in any field trial can come with an optional set of
// parameters (key-value pairs).  In the bundled omnibox experiment
// (kBundledExperimentFieldTrialName), each experiment group comes with a
// list of parameters in the form:
//   key=<Rule>:<AutocompleteInput::PageClassification (as an int)>
//   value=<arbitrary string>
// The AutocompleteInput::PageClassification can also be "*", which means
// this rule applies in all page classification contexts.
// One example parameter is
//   key=SearchHistory:6
//   value=PreventInlining
// This means in page classification context 6 (a search result page doing
// search term replacement), the SearchHistory experiment should
// PreventInlining.
//
// In short, this function tries to find the value associated with key
// |rule|:|page_classification|, failing that it looks up |rule|:*,
// and failing that it returns the empty string.
std::string OmniboxFieldTrial::GetValueForRuleInContext(
    const std::string& rule,
    AutocompleteInput::PageClassification page_classification) {
  std::map<std::string, std::string> params;
  if (!chrome_variations::GetVariationParams(kBundledExperimentFieldTrialName,
                                             &params)) {
    return std::string();
  }
  // Look up rule in this exact context.
  std::map<std::string, std::string>::iterator it =
      params.find(rule + ":" + base::IntToString(
          static_cast<int>(page_classification)));
  if (it != params.end())
    return it->second;
  // Look up rule in the global context.
  it = params.find(rule + ":*");
  return (it != params.end()) ? it->second : std::string();
}
