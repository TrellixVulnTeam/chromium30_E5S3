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

#include "config.h"
#include "core/animation/AnimatableValue.h"

#include "core/animation/AnimatableNeutral.h"
#include "core/animation/AnimatableNumber.h"
#include "core/animation/AnimatableUnknown.h"
#include "core/animation/DeferredAnimatableValue.h"

#include <algorithm>

namespace WebCore {

PassRefPtr<AnimatableValue> AnimatableValue::create(CSSValue* value)
{
    // FIXME: Move this logic to a separate factory class.
    // FIXME: Handle all animatable CSSValue types.
    if (AnimatableNumber::canCreateFrom(value))
        return AnimatableNumber::create(value);
    return AnimatableUnknown::create(value);
}

const AnimatableValue* AnimatableValue::neutralValue()
{
    static AnimatableNeutral* neutralSentinelValue = AnimatableNeutral::create().leakRef();
    return neutralSentinelValue;
}

const AnimatableValue* AnimatableValue::deferredSnapshotValue()
{
    static DeferredAnimatableValue* deferredAnimatableValueSentinel = DeferredAnimatableValue::create().leakRef();
    return deferredAnimatableValueSentinel;
}

PassRefPtr<AnimatableValue> AnimatableValue::interpolate(const AnimatableValue* left, const AnimatableValue* right, double fraction)
{
    ASSERT(left);
    ASSERT(right);
    ASSERT(!left->isNeutral());
    ASSERT(!right->isNeutral());

    if (fraction && fraction != 1 && left->isSameType(right))
        return left->interpolateTo(right, fraction);

    return defaultInterpolateTo(left, right, fraction);
}

PassRefPtr<AnimatableValue> AnimatableValue::add(const AnimatableValue* left, const AnimatableValue* right)
{
    ASSERT(left);
    ASSERT(right);

    if (left->isNeutral())
        return takeConstRef(right);
    if (right->isNeutral())
        return takeConstRef(left);

    if (left->isSameType(right))
        return left->addWith(right);

    return defaultAddWith(left, right);
}

} // namespace WebCore
