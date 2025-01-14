/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"
#include "core/platform/LifecycleObserver.h"

#include "core/platform/LifecycleContext.h"

namespace WebCore {

LifecycleObserver::LifecycleObserver(LifecycleContext* lifecycleContext, Type type)
    : m_lifecycleContext(0)
{
    observeContext(lifecycleContext, type);
}

LifecycleObserver::~LifecycleObserver()
{
    if (m_lifecycleContext)
        observeContext(0, GenericType);
}

void LifecycleObserver::observeContext(LifecycleContext* context, Type type)
{
    if (m_lifecycleContext) {
        ASSERT(m_lifecycleContext->isContextThread());
        m_lifecycleContext->wasUnobservedBy(this, type);
    }

    m_lifecycleContext = context;

    if (m_lifecycleContext) {
        ASSERT(m_lifecycleContext->isContextThread());
        m_lifecycleContext->wasObservedBy(this, type);
    }
}

void LifecycleObserver::contextDestroyed()
{
    m_lifecycleContext = 0;
}

} // namespace WebCore
