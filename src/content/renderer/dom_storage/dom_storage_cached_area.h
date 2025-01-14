// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_DOM_STORAGE_DOM_STORAGE_CACHED_AREA_H_
#define CONTENT_RENDERER_DOM_STORAGE_DOM_STORAGE_CACHED_AREA_H_

#include <map>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/nullable_string16.h"
#include "content/common/content_export.h"
#include "url/gurl.h"

namespace dom_storage {
class DomStorageMap;
}

namespace content {

class DomStorageProxy;

// Unlike the other classes in the dom_storage library, this one is intended
// for use in renderer processes. It maintains a complete cache of the
// origin's Map of key/value pairs for fast access. The cache is primed on
// first access and changes are written to the backend thru the |proxy|.
// Mutations originating in other processes are applied to the cache via
// the ApplyMutation method.
class CONTENT_EXPORT DomStorageCachedArea
    : public base::RefCounted<DomStorageCachedArea> {
 public:
  DomStorageCachedArea(int64 namespace_id,
                       const GURL& origin,
                       DomStorageProxy* proxy);

  int64 namespace_id() const { return namespace_id_; }
  const GURL& origin() const { return origin_; }

  unsigned GetLength(int connection_id);
  base::NullableString16 GetKey(int connection_id, unsigned index);
  base::NullableString16 GetItem(int connection_id, const base::string16& key);
  bool SetItem(int connection_id,
               const base::string16& key,
               const base::string16& value,
               const GURL& page_url);
  void RemoveItem(int connection_id,
                  const base::string16& key,
                  const GURL& page_url);
  void Clear(int connection_id, const GURL& page_url);

  void ApplyMutation(const base::NullableString16& key,
                     const base::NullableString16& new_value);

  size_t MemoryBytesUsedByCache() const;

 private:
  friend class DomStorageCachedAreaTest;
  friend class base::RefCounted<DomStorageCachedArea>;
  ~DomStorageCachedArea();

  // Primes the cache, loading all values for the area.
  void Prime(int connection_id);
  void PrimeIfNeeded(int connection_id) {
    if (!map_.get())
      Prime(connection_id);
  }

  // Resets the object back to its newly constructed state.
  void Reset();

  // Async completion callbacks for proxied operations.
  // These are used to maintain cache consistency by preventing
  // mutation events from other processes from overwriting local
  // changes made after the mutation.
  void OnLoadComplete(bool success);
  void OnSetItemComplete(const base::string16& key, bool success);
  void OnClearComplete(bool success);
  void OnRemoveItemComplete(const base::string16& key, bool success);

  bool should_ignore_key_mutation(const base::string16& key) const {
    return ignore_key_mutations_.find(key) != ignore_key_mutations_.end();
  }

  bool ignore_all_mutations_;
  std::map<base::string16, int> ignore_key_mutations_;

  int64 namespace_id_;
  GURL origin_;
  scoped_refptr<dom_storage::DomStorageMap> map_;
  scoped_refptr<DomStorageProxy> proxy_;
  base::WeakPtrFactory<DomStorageCachedArea> weak_factory_;
};

}  // namespace content

#endif  // CONTENT_RENDERER_DOM_STORAGE_DOM_STORAGE_CACHED_AREA_H_
