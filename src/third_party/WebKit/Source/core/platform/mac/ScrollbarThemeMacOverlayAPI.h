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

#ifndef ScrollbarThemeMacOverlayAPI_h
#define ScrollbarThemeMacOverlayAPI_h

#include "core/platform/mac/ScrollbarThemeMac.h"

typedef id ScrollbarPainter;

namespace WebCore {

class ScrollbarThemeMacOverlayAPI : public ScrollbarThemeMacCommon {
public:
    virtual void updateEnabledState(ScrollbarThemeClient*);
    virtual int scrollbarThickness(ScrollbarControlSize = RegularScrollbar);
    virtual bool usesOverlayScrollbars() const;
    virtual void updateScrollbarOverlayStyle(ScrollbarThemeClient*);
    virtual ScrollbarButtonsPlacement buttonsPlacement() const;

    virtual void registerScrollbar(ScrollbarThemeClient*);
    virtual void unregisterScrollbar(ScrollbarThemeClient*);

    void setNewPainterForScrollbar(ScrollbarThemeClient*, ScrollbarPainter);
    ScrollbarPainter painterForScrollbar(ScrollbarThemeClient*);

    virtual void paintTrackBackground(GraphicsContext*, ScrollbarThemeClient*, const IntRect&);
    virtual void paintThumb(GraphicsContext*, ScrollbarThemeClient*, const IntRect&);

protected:
    virtual IntRect trackRect(ScrollbarThemeClient*, bool painting = false);
    virtual IntRect backButtonRect(ScrollbarThemeClient*, ScrollbarPart, bool painting = false);
    virtual IntRect forwardButtonRect(ScrollbarThemeClient*, ScrollbarPart, bool painting = false);

    virtual bool hasButtons(ScrollbarThemeClient*) { return false; }
    virtual bool hasThumb(ScrollbarThemeClient*);

    virtual int minimumThumbLength(ScrollbarThemeClient*);
};

}

#endif
