/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#ifndef MediaSourceBase_h
#define MediaSourceBase_h

#include "core/dom/ActiveDOMObject.h"
#include "core/dom/EventTarget.h"
#include "core/html/HTMLMediaSource.h"
#include "core/html/URLRegistry.h"
#include "core/platform/graphics/MediaSourcePrivate.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"

namespace WebCore {

class ExceptionState;
class GenericEventQueue;

class MediaSourceBase : public RefCounted<MediaSourceBase>, public HTMLMediaSource, public ActiveDOMObject, public EventTarget {
public:
    static const AtomicString& openKeyword();
    static const AtomicString& closedKeyword();
    static const AtomicString& endedKeyword();

    virtual ~MediaSourceBase();

    void addedToRegistry();
    void removedFromRegistry();
    void openIfInEndedState();
    bool isOpen() const;

    // HTMLMediaSource
    virtual bool attachToElement() OVERRIDE;
    virtual void setPrivateAndOpen(PassOwnPtr<MediaSourcePrivate>) OVERRIDE;
    virtual void close() OVERRIDE;
    virtual bool isClosed() const OVERRIDE;
    virtual double duration() const OVERRIDE;
    virtual PassRefPtr<TimeRanges> buffered() const OVERRIDE;
    virtual void refHTMLMediaSource() OVERRIDE { ref(); }
    virtual void derefHTMLMediaSource() OVERRIDE { deref(); }

    void setDuration(double, ExceptionState&);
    const AtomicString& readyState() const { return m_readyState; }
    void setReadyState(const AtomicString&);
    void endOfStream(const AtomicString& error, ExceptionState&);


    // ActiveDOMObject interface
    virtual bool hasPendingActivity() const OVERRIDE;
    virtual void stop() OVERRIDE;

    // EventTarget interface
    virtual ScriptExecutionContext* scriptExecutionContext() const OVERRIDE;
    virtual EventTargetData* eventTargetData() OVERRIDE;
    virtual EventTargetData* ensureEventTargetData() OVERRIDE;
    virtual void refEventTarget() OVERRIDE { ref(); }
    virtual void derefEventTarget() OVERRIDE { deref(); }

    // URLRegistrable interface
    virtual URLRegistry& registry() const OVERRIDE;

    using RefCounted<MediaSourceBase>::ref;
    using RefCounted<MediaSourceBase>::deref;

protected:
    explicit MediaSourceBase(ScriptExecutionContext*);

    virtual void onReadyStateChange(const AtomicString& oldState, const AtomicString& newState) = 0;
    virtual Vector<RefPtr<TimeRanges> > activeRanges() const = 0;

    PassOwnPtr<SourceBufferPrivate> createSourceBufferPrivate(const String& type, const MediaSourcePrivate::CodecsArray&, ExceptionState&);
    void scheduleEvent(const AtomicString& eventName);
    GenericEventQueue* asyncEventQueue() const { return m_asyncEventQueue.get(); }

private:
    OwnPtr<MediaSourcePrivate> m_private;
    EventTargetData m_eventTargetData;
    AtomicString m_readyState;
    OwnPtr<GenericEventQueue> m_asyncEventQueue;
    bool m_attached;
};

}

#endif
