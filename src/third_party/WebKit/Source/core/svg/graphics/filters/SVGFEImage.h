/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2010 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SVGFEImage_h
#define SVGFEImage_h

#include "core/platform/graphics/filters/FilterEffect.h"
#include "core/svg/SVGPreserveAspectRatio.h"

namespace WebCore {

class Document;
class Image;
class RenderObject;

class FEImage : public FilterEffect {
public:
    static PassRefPtr<FEImage> createWithImage(Filter*, PassRefPtr<Image>, const SVGPreserveAspectRatio&);
    static PassRefPtr<FEImage> createWithIRIReference(Filter*, Document*, const String&, const SVGPreserveAspectRatio&);

    virtual void determineAbsolutePaintRect();

    virtual FilterEffectType filterEffectType() const { return FilterEffectTypeImage; }

    virtual TextStream& externalRepresentation(TextStream&, int indention) const;
    virtual SkImageFilter* createImageFilter(SkiaImageFilterBuilder*);

private:
    virtual ~FEImage() { }
    FEImage(Filter*, PassRefPtr<Image>, const SVGPreserveAspectRatio&);
    FEImage(Filter*, Document*, const String&, const SVGPreserveAspectRatio&);
    RenderObject* referencedRenderer() const;

    virtual void applySoftware() OVERRIDE;

    RefPtr<Image> m_image;

    // m_document will never be a dangling reference. See https://bugs.webkit.org/show_bug.cgi?id=99243
    Document* m_document;
    String m_href;
    SVGPreserveAspectRatio m_preserveAspectRatio;
};

} // namespace WebCore

#endif // SVGFEImage_h
