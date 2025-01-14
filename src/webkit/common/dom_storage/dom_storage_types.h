// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_COMMON_DOM_STORAGE_DOM_STORAGE_TYPES_H_
#define WEBKIT_COMMON_DOM_STORAGE_DOM_STORAGE_TYPES_H_

#include <map>

#include "base/basictypes.h"
#include "base/strings/nullable_string16.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "url/gurl.h"
#include "webkit/common/webkit_storage_common_export.h"

namespace dom_storage {

// The quota for each storage area.
// This value is enforced in renderer processes and the browser process.
const size_t kPerAreaQuota = 10 * 1024 * 1024;

// In the browser process we allow some overage to
// accomodate concurrent writes from different renderers
// that were allowed because the limit imposed in the renderer
// wasn't exceeded.
const size_t kPerAreaOverQuotaAllowance = 100 * 1024;

// Value to indicate the localstorage namespace vs non-zero
// values for sessionstorage namespaces.
const int64 kLocalStorageNamespaceId = 0;

const int64 kInvalidSessionStorageNamespaceId = kLocalStorageNamespaceId;

// Start purging memory if the number of in-memory areas exceeds this.
const int64 kMaxInMemoryAreas = 100;

// Value to indicate an area that not be opened.
const int kInvalidAreaId = -1;

typedef std::map<base::string16, base::NullableString16> ValuesMap;

struct WEBKIT_STORAGE_COMMON_EXPORT LocalStorageUsageInfo {
  GURL origin;
  size_t data_size;
  base::Time last_modified;

  LocalStorageUsageInfo();
  ~LocalStorageUsageInfo();
};

struct WEBKIT_STORAGE_COMMON_EXPORT SessionStorageUsageInfo {
  GURL origin;
  std::string persistent_namespace_id;

  SessionStorageUsageInfo();
  ~SessionStorageUsageInfo();
};

}  // namespace dom_storage

#endif  // WEBKIT_COMMON_DOM_STORAGE_DOM_STORAGE_TYPES_H_
