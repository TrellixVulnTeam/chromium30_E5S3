/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
 */

#include "config.h"
#include "wtf/ArrayBufferContents.h"

#include "wtf/ArrayBufferDeallocationObserver.h"
#include <string.h>

namespace WTF {

ArrayBufferContents::ArrayBufferContents()
    : m_data(0)
    , m_sizeInBytes(0)
    , m_deallocationObserver(0) { }

ArrayBufferContents::ArrayBufferContents(unsigned numElements, unsigned elementByteSize, ArrayBufferContents::InitializationPolicy policy)
    : m_data(0)
    , m_sizeInBytes(0)
    , m_deallocationObserver(0)
{
    // Do not allow 32-bit overflow of the total size.
    // FIXME: Why not? The tryFastCalloc function already checks its arguments,
    // and will fail if there is any overflow, so why should we include a
    // redudant unnecessarily restrictive check here?
    if (numElements) {
        unsigned totalSize = numElements * elementByteSize;
        if (totalSize / numElements != elementByteSize) {
            m_data = 0;
            return;
        }
    }
    if (allocateMemory(numElements * elementByteSize, policy, m_data)) {
        m_sizeInBytes = numElements * elementByteSize;
        return;
    }
    m_data = 0;
}

ArrayBufferContents::ArrayBufferContents(void* data, unsigned sizeInBytes)
    : m_data(data)
    , m_sizeInBytes(sizeInBytes)
    , m_deallocationObserver(0)
{
    if (!m_data) {
        ASSERT(!m_sizeInBytes);
        m_sizeInBytes = 0;
        // Allow null data if size is 0 bytes, make sure m_data is valid pointer.
        // (fastMalloc guarantees valid pointer for size 0)
        if (!allocateMemory(0, ZeroInitialize, m_data))
            ASSERT_NOT_REACHED();
    }
}

ArrayBufferContents::~ArrayBufferContents()
{
    freeMemory(m_data);
    clear();
}

void ArrayBufferContents::clear()
{
    if (m_data && m_deallocationObserver)
        m_deallocationObserver->ArrayBufferDeallocated(m_sizeInBytes);
    m_data = 0;
    m_sizeInBytes = 0;
    m_deallocationObserver = 0;
}

void ArrayBufferContents::transfer(ArrayBufferContents& other)
{
    ASSERT(!other.m_data);
    other.m_data = m_data;
    other.m_sizeInBytes = m_sizeInBytes;
    clear();
}

void ArrayBufferContents::copyTo(ArrayBufferContents& other)
{
    ASSERT(!other.m_sizeInBytes);
    other.freeMemory(other.m_data);
    allocateMemory(m_sizeInBytes, DontInitialize, other.m_data);
    if (!other.m_data)
        return;
    memcpy(other.m_data, m_data, m_sizeInBytes);
    other.m_sizeInBytes = m_sizeInBytes;
}

bool ArrayBufferContents::allocateMemory(size_t size, InitializationPolicy policy, void*& data)
{
    if (policy == ZeroInitialize) {
        return WTF::tryFastCalloc(size, 1).getValue(data);
    }
    ASSERT(policy == DontInitialize);
    return WTF::tryFastMalloc(size).getValue(data);
}

void ArrayBufferContents::freeMemory(void * data)
{
    WTF::fastFree(data);
}

} // namespace WTF
