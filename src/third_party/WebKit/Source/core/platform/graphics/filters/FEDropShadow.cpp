/*
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
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

#include "config.h"

#include "core/platform/graphics/filters/FEDropShadow.h"

#include "core/platform/graphics/GraphicsContext.h"
#include "core/platform/graphics/ShadowBlur.h"
#include "core/platform/graphics/filters/FEGaussianBlur.h"
#include "core/platform/graphics/filters/Filter.h"
#include "core/platform/text/TextStream.h"
#include "core/rendering/RenderTreeAsText.h"
#include "wtf/MathExtras.h"
#include "wtf/Uint8ClampedArray.h"

using namespace std;

namespace WebCore {

FEDropShadow::FEDropShadow(Filter* filter, float stdX, float stdY, float dx, float dy, const Color& shadowColor, float shadowOpacity)
    : FilterEffect(filter)
    , m_stdX(stdX)
    , m_stdY(stdY)
    , m_dx(dx)
    , m_dy(dy)
    , m_shadowColor(shadowColor)
    , m_shadowOpacity(shadowOpacity)
{
}

PassRefPtr<FEDropShadow> FEDropShadow::create(Filter* filter, float stdX, float stdY, float dx, float dy, const Color& shadowColor, float shadowOpacity)
{
    return adoptRef(new FEDropShadow(filter, stdX, stdY, dx, dy, shadowColor, shadowOpacity));
}

void FEDropShadow::determineAbsolutePaintRect()
{
    Filter* filter = this->filter();
    ASSERT(filter);

    FloatRect absolutePaintRect = mapRect(inputEffect(0)->absolutePaintRect());

    if (clipsToBounds())
        absolutePaintRect.intersect(maxEffectRect());
    else
        absolutePaintRect.unite(maxEffectRect());

    setAbsolutePaintRect(enclosingIntRect(absolutePaintRect));
}

FloatRect FEDropShadow::mapRect(const FloatRect& rect, bool forward)
{
    FloatRect result = rect;
    Filter* filter = this->filter();
    ASSERT(filter);

    FloatRect offsetRect = rect;
    if (forward)
        offsetRect.move(filter->applyHorizontalScale(m_dx), filter->applyVerticalScale(m_dy));
    else
        offsetRect.move(-filter->applyHorizontalScale(m_dx), -filter->applyVerticalScale(m_dy));
    result.unite(offsetRect);

    unsigned kernelSizeX = 0;
    unsigned kernelSizeY = 0;
    FEGaussianBlur::calculateKernelSize(filter, kernelSizeX, kernelSizeY, m_stdX, m_stdY);

    // We take the half kernel size and multiply it with three, because we run box blur three times.
    result.inflateX(3 * kernelSizeX * 0.5f);
    result.inflateY(3 * kernelSizeY * 0.5f);
    return result;
}

void FEDropShadow::applySoftware()
{
    FilterEffect* in = inputEffect(0);

    ImageBuffer* resultImage = createImageBufferResult();
    if (!resultImage)
        return;

    Filter* filter = this->filter();
    FloatSize blurRadius(filter->applyHorizontalScale(m_stdX), filter->applyVerticalScale(m_stdY));
    FloatSize offset(filter->applyHorizontalScale(m_dx), filter->applyVerticalScale(m_dy));

    FloatRect drawingRegion = drawingRegionOfInputImage(in->absolutePaintRect());
    FloatRect drawingRegionWithOffset(drawingRegion);
    drawingRegionWithOffset.move(offset);

    ImageBuffer* sourceImage = in->asImageBuffer();
    ASSERT(sourceImage);
    GraphicsContext* resultContext = resultImage->context();
    ASSERT(resultContext);
    resultContext->setAlpha(m_shadowOpacity);
    resultContext->drawImageBuffer(sourceImage, drawingRegionWithOffset);
    resultContext->setAlpha(1);

    ShadowBlur contextShadow(blurRadius, offset, m_shadowColor);

    // TODO: Direct pixel access to ImageBuffer would avoid copying the ImageData.
    IntRect shadowArea(IntPoint(), resultImage->internalSize());
    RefPtr<Uint8ClampedArray> srcPixelArray = resultImage->getPremultipliedImageData(shadowArea);

    contextShadow.blurLayerImage(srcPixelArray->data(), shadowArea.size(), 4 * shadowArea.size().width());

    resultImage->putByteArray(Premultiplied, srcPixelArray.get(), shadowArea.size(), shadowArea, IntPoint());

    resultContext->setCompositeOperation(CompositeSourceIn);
    resultContext->fillRect(FloatRect(FloatPoint(), absolutePaintRect().size()), m_shadowColor);
    resultContext->setCompositeOperation(CompositeDestinationOver);

    resultImage->context()->drawImageBuffer(sourceImage, drawingRegion);
}

TextStream& FEDropShadow::externalRepresentation(TextStream& ts, int indent) const
{
    writeIndent(ts, indent);
    ts << "[feDropShadow";
    FilterEffect::externalRepresentation(ts);
    ts << " stdDeviation=\"" << m_stdX << ", " << m_stdY << "\" dx=\"" << m_dx << "\" dy=\"" << m_dy << "\" flood-color=\"" << m_shadowColor.nameForRenderTreeAsText() <<"\" flood-opacity=\"" << m_shadowOpacity << "]\n";
    inputEffect(0)->externalRepresentation(ts, indent + 1);
    return ts;
}

} // namespace WebCore
