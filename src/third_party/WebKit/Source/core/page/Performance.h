/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2012 Intel Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Performance_h
#define Performance_h

#include "bindings/v8/ScriptWrappable.h"
#include "core/dom/EventTarget.h"
#include "core/page/DOMWindowProperty.h"
#include "core/page/MemoryInfo.h"
#include "core/page/PerformanceEntry.h"
#include "core/page/PerformanceNavigation.h"
#include "core/page/PerformanceTiming.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/RefPtr.h"
#include "wtf/text/WTFString.h"

namespace WebCore {

class Document;
class ResourceRequest;
class ResourceResponse;
class ResourceTimingInfo;
class UserTiming;

class Performance : public ScriptWrappable, public RefCounted<Performance>, public DOMWindowProperty, public EventTarget {
public:
    static PassRefPtr<Performance> create(Frame* frame) { return adoptRef(new Performance(frame)); }
    ~Performance();

    virtual const AtomicString& interfaceName() const;
    virtual ScriptExecutionContext* scriptExecutionContext() const;

    PassRefPtr<MemoryInfo> memory() const;
    PerformanceNavigation* navigation() const;
    PerformanceTiming* timing() const;
    double now() const;

    Vector<RefPtr<PerformanceEntry> > getEntries() const;
    Vector<RefPtr<PerformanceEntry> > getEntriesByType(const String& entryType);
    Vector<RefPtr<PerformanceEntry> > getEntriesByName(const String& name, const String& entryType);

    void webkitClearResourceTimings();
    void webkitSetResourceTimingBufferSize(unsigned int);

    DEFINE_ATTRIBUTE_EVENT_LISTENER(webkitresourcetimingbufferfull);

    void addResourceTiming(const ResourceTimingInfo&, Document*);

    using RefCounted<Performance>::ref;
    using RefCounted<Performance>::deref;

    void mark(const String& markName, ExceptionCode&);
    void clearMarks(const String& markName);

    void measure(const String& measureName, const String& startMark, const String& endMark, ExceptionCode&);
    void clearMeasures(const String& measureName);

private:
    explicit Performance(Frame*);

    virtual void refEventTarget() { ref(); }
    virtual void derefEventTarget() { deref(); }
    virtual EventTargetData* eventTargetData();
    virtual EventTargetData* ensureEventTargetData();
    bool isResourceTimingBufferFull();
    void addResourceTimingBuffer(PassRefPtr<PerformanceEntry>);

    EventTargetData m_eventTargetData;

    mutable RefPtr<PerformanceNavigation> m_navigation;
    mutable RefPtr<PerformanceTiming> m_timing;

    Vector<RefPtr<PerformanceEntry> > m_resourceTimingBuffer;
    unsigned m_resourceTimingBufferSize;

    RefPtr<UserTiming> m_userTiming;
};

}

#endif // Performance_h
