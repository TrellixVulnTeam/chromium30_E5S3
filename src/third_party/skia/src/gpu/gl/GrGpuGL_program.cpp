/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGpuGL.h"

#include "GrEffect.h"
#include "GrGLEffect.h"
#include "SkTSearch.h"

typedef GrGLUniformManager::UniformHandle UniformHandle;
static const UniformHandle kInvalidUniformHandle = GrGLUniformManager::kInvalidUniformHandle;

struct GrGpuGL::ProgramCache::Entry {
    SK_DECLARE_INST_COUNT_ROOT(Entry);
    Entry() : fProgram(NULL), fLRUStamp(0) {}

    SkAutoTUnref<GrGLProgram>   fProgram;
    unsigned int                fLRUStamp;
};

SK_DEFINE_INST_COUNT(GrGpuGL::ProgramCache::Entry);

struct GrGpuGL::ProgramCache::ProgDescLess {
    bool operator() (const GrGLProgramDesc& desc, const Entry* entry) {
        GrAssert(NULL != entry->fProgram.get());
        return GrGLProgramDesc::Less(desc, entry->fProgram->getDesc());
    }

    bool operator() (const Entry* entry, const GrGLProgramDesc& desc) {
        GrAssert(NULL != entry->fProgram.get());
        return GrGLProgramDesc::Less(entry->fProgram->getDesc(), desc);
    }
};

GrGpuGL::ProgramCache::ProgramCache(const GrGLContext& gl)
    : fCount(0)
    , fCurrLRUStamp(0)
    , fGL(gl)
#ifdef PROGRAM_CACHE_STATS
    , fTotalRequests(0)
    , fCacheMisses(0)
    , fHashMisses(0)
#endif
{
    for (int i = 0; i < 1 << kHashBits; ++i) {
        fHashTable[i] = NULL;
    }
}

GrGpuGL::ProgramCache::~ProgramCache() {
    for (int i = 0; i < fCount; ++i){
        SkDELETE(fEntries[i]);
    }
    // dump stats
#ifdef PROGRAM_CACHE_STATS
    SkDebugf("--- Program Cache ---\n");
    SkDebugf("Total requests: %d\n", fTotalRequests);
    SkDebugf("Cache misses: %d\n", fCacheMisses);
    SkDebugf("Cache miss %%: %f\n", (fTotalRequests > 0) ?
                                        100.f * fCacheMisses / fTotalRequests :
                                        0.f);
    int cacheHits = fTotalRequests - fCacheMisses;
    SkDebugf("Hash miss %%: %f\n", (cacheHits > 0) ? 100.f * fHashMisses / cacheHits : 0.f);
    SkDebugf("---------------------\n");
#endif
}

void GrGpuGL::ProgramCache::abandon() {
    for (int i = 0; i < fCount; ++i) {
        GrAssert(NULL != fEntries[i]->fProgram.get());
        fEntries[i]->fProgram->abandon();
        fEntries[i]->fProgram.reset(NULL);
    }
    fCount = 0;
}

int GrGpuGL::ProgramCache::search(const GrGLProgramDesc& desc) const {
    ProgDescLess less;
    return SkTSearch(fEntries, fCount, desc, sizeof(Entry*), less);
}

GrGLProgram* GrGpuGL::ProgramCache::getProgram(const GrGLProgramDesc& desc,
                                               const GrEffectStage* colorStages[],
                                               const GrEffectStage* coverageStages[]) {
#ifdef PROGRAM_CACHE_STATS
    ++fTotalRequests;
#endif

    Entry* entry = NULL;

    uint32_t hashIdx = desc.getChecksum();
    hashIdx ^= hashIdx >> 16;
    if (kHashBits <= 8) {
        hashIdx ^= hashIdx >> 8;
    }
    hashIdx &=((1 << kHashBits) - 1);
    Entry* hashedEntry = fHashTable[hashIdx];
    if (NULL != hashedEntry && hashedEntry->fProgram->getDesc() == desc) {
        GrAssert(NULL != hashedEntry->fProgram);
        entry = hashedEntry;
    }

    int entryIdx;
    if (NULL == entry) {
        entryIdx = this->search(desc);
        if (entryIdx >= 0) {
            entry = fEntries[entryIdx];
#ifdef PROGRAM_CACHE_STATS
            ++fHashMisses;
#endif
        }
    }

    if (NULL == entry) {
        // We have a cache miss
#ifdef PROGRAM_CACHE_STATS
        ++fCacheMisses;
#endif
        GrGLProgram* program = GrGLProgram::Create(fGL, desc, colorStages, coverageStages);
        if (NULL == program) {
            return NULL;
        }
        int purgeIdx = 0;
        if (fCount < kMaxEntries) {
            entry = SkNEW(Entry);
            purgeIdx = fCount++;
            fEntries[purgeIdx] = entry;
        } else {
            GrAssert(fCount == kMaxEntries);
            purgeIdx = 0;
            for (int i = 1; i < kMaxEntries; ++i) {
                if (fEntries[i]->fLRUStamp < fEntries[purgeIdx]->fLRUStamp) {
                    purgeIdx = i;
                }
            }
            entry = fEntries[purgeIdx];
            int purgedHashIdx = entry->fProgram->getDesc().getChecksum() & ((1 << kHashBits) - 1);
            if (fHashTable[purgedHashIdx] == entry) {
                fHashTable[purgedHashIdx] = NULL;
            }
        }
        GrAssert(fEntries[purgeIdx] == entry);
        entry->fProgram.reset(program);
        // We need to shift fEntries around so that the entry currently at purgeIdx is placed
        // just before the entry at ~entryIdx (in order to keep fEntries sorted by descriptor).
        entryIdx = ~entryIdx;
        if (entryIdx < purgeIdx) {
            //  Let E and P be the entries at index entryIdx and purgeIdx, respectively.
            //  If the entries array looks like this:
            //       aaaaEbbbbbPccccc
            //  we rearrange it to look like this:
            //       aaaaPEbbbbbccccc
            size_t copySize = (purgeIdx - entryIdx) * sizeof(Entry*);
            memmove(fEntries + entryIdx + 1, fEntries + entryIdx, copySize);
            fEntries[entryIdx] = entry;
        } else if (purgeIdx < entryIdx) {
            //  If the entries array looks like this:
            //       aaaaPbbbbbEccccc
            //  we rearrange it to look like this:
            //       aaaabbbbbPEccccc
            size_t copySize = (entryIdx - purgeIdx - 1) * sizeof(Entry*);
            memmove(fEntries + purgeIdx, fEntries + purgeIdx + 1, copySize);
            fEntries[entryIdx - 1] = entry;
        }
#if GR_DEBUG
        GrAssert(NULL != fEntries[0]->fProgram.get());
        for (int i = 0; i < fCount - 1; ++i) {
            GrAssert(NULL != fEntries[i + 1]->fProgram.get());
            const GrGLProgramDesc& a = fEntries[i]->fProgram->getDesc();
            const GrGLProgramDesc& b = fEntries[i + 1]->fProgram->getDesc();
            GrAssert(GrGLProgramDesc::Less(a, b));
            GrAssert(!GrGLProgramDesc::Less(b, a));
        }
#endif
    }

    fHashTable[hashIdx] = entry;
    entry->fLRUStamp = fCurrLRUStamp;

    if (SK_MaxU32 == fCurrLRUStamp) {
        // wrap around! just trash our LRU, one time hit.
        for (int i = 0; i < fCount; ++i) {
            fEntries[i]->fLRUStamp = 0;
        }
    }
    ++fCurrLRUStamp;
    return entry->fProgram;
}

////////////////////////////////////////////////////////////////////////////////

void GrGpuGL::abandonResources(){
    INHERITED::abandonResources();
    fProgramCache->abandon();
    fHWProgramID = 0;
}

////////////////////////////////////////////////////////////////////////////////

#define GL_CALL(X) GR_GL_CALL(this->glInterface(), X)

void GrGpuGL::flushPathStencilMatrix() {
    const SkMatrix& viewMatrix = this->getDrawState().getViewMatrix();
    const GrRenderTarget* rt = this->getDrawState().getRenderTarget();
    SkISize size;
    size.set(rt->width(), rt->height());
    const SkMatrix& vm = this->getDrawState().getViewMatrix();

    if (fHWPathStencilMatrixState.fRenderTargetOrigin != rt->origin() ||
        fHWPathStencilMatrixState.fViewMatrix.cheapEqualTo(viewMatrix) ||
        fHWPathStencilMatrixState.fRenderTargetSize!= size) {
        // rescale the coords from skia's "device" coords to GL's normalized coords,
        // and perform a y-flip if required.
        SkMatrix m;
        if (kBottomLeft_GrSurfaceOrigin == rt->origin()) {
            m.setScale(SkIntToScalar(2) / rt->width(), SkIntToScalar(-2) / rt->height());
            m.postTranslate(-SK_Scalar1, SK_Scalar1);
        } else {
            m.setScale(SkIntToScalar(2) / rt->width(), SkIntToScalar(2) / rt->height());
            m.postTranslate(-SK_Scalar1, -SK_Scalar1);
        }
        m.preConcat(vm);

        // GL wants a column-major 4x4.
        GrGLfloat mv[]  = {
            // col 0
            SkScalarToFloat(m[SkMatrix::kMScaleX]),
            SkScalarToFloat(m[SkMatrix::kMSkewY]),
            0,
            SkScalarToFloat(m[SkMatrix::kMPersp0]),

            // col 1
            SkScalarToFloat(m[SkMatrix::kMSkewX]),
            SkScalarToFloat(m[SkMatrix::kMScaleY]),
            0,
            SkScalarToFloat(m[SkMatrix::kMPersp1]),

            // col 2
            0, 0, 0, 0,

            // col3
            SkScalarToFloat(m[SkMatrix::kMTransX]),
            SkScalarToFloat(m[SkMatrix::kMTransY]),
            0.0f,
            SkScalarToFloat(m[SkMatrix::kMPersp2])
        };
        GL_CALL(MatrixMode(GR_GL_PROJECTION));
        GL_CALL(LoadMatrixf(mv));
        fHWPathStencilMatrixState.fViewMatrix = vm;
        fHWPathStencilMatrixState.fRenderTargetSize = size;
        fHWPathStencilMatrixState.fRenderTargetOrigin = rt->origin();
    }
}

bool GrGpuGL::flushGraphicsState(DrawType type, const GrDeviceCoordTexture* dstCopy) {
    const GrDrawState& drawState = this->getDrawState();

    // GrGpu::setupClipAndFlushState should have already checked this and bailed if not true.
    GrAssert(NULL != drawState.getRenderTarget());

    if (kStencilPath_DrawType == type) {
        this->flushPathStencilMatrix();
    } else {
        this->flushMiscFixedFunctionState();

        GrBlendCoeff srcCoeff;
        GrBlendCoeff dstCoeff;
        GrDrawState::BlendOptFlags blendOpts = drawState.getBlendOpts(false, &srcCoeff, &dstCoeff);
        if (GrDrawState::kSkipDraw_BlendOptFlag & blendOpts) {
            return false;
        }

        SkSTArray<8, const GrEffectStage*, true> colorStages;
        SkSTArray<8, const GrEffectStage*, true> coverageStages;
        GrGLProgramDesc desc;
        GrGLProgramDesc::Build(this->getDrawState(),
                               kDrawPoints_DrawType == type,
                               blendOpts,
                               srcCoeff,
                               dstCoeff,
                               this,
                               dstCopy,
                               &colorStages,
                               &coverageStages,
                               &desc);

        fCurrentProgram.reset(fProgramCache->getProgram(desc,
                                                        colorStages.begin(),
                                                        coverageStages.begin()));
        if (NULL == fCurrentProgram.get()) {
            GrAssert(!"Failed to create program!");
            return false;
        }
        fCurrentProgram.get()->ref();

        GrGLuint programID = fCurrentProgram->programID();
        if (fHWProgramID != programID) {
            GL_CALL(UseProgram(programID));
            fHWProgramID = programID;
        }

        fCurrentProgram->overrideBlend(&srcCoeff, &dstCoeff);
        this->flushBlend(kDrawLines_DrawType == type, srcCoeff, dstCoeff);

        fCurrentProgram->setData(this,
                                 blendOpts,
                                 colorStages.begin(),
                                 coverageStages.begin(),
                                 dstCopy,
                                 &fSharedGLProgramState);
    }
    this->flushStencil(type);
    this->flushScissor();
    this->flushAAState(type);

    SkIRect* devRect = NULL;
    SkIRect devClipBounds;
    if (drawState.isClipState()) {
        this->getClip()->getConservativeBounds(drawState.getRenderTarget(), &devClipBounds);
        devRect = &devClipBounds;
    }
    // This must come after textures are flushed because a texture may need
    // to be msaa-resolved (which will modify bound FBO state).
    this->flushRenderTarget(devRect);

    return true;
}

void GrGpuGL::setupGeometry(const DrawInfo& info, size_t* indexOffsetInBytes) {

    GrGLsizei stride = this->getDrawState().getVertexSize();

    size_t vertexOffsetInBytes = stride * info.startVertex();

    const GeometryPoolState& geoPoolState = this->getGeomPoolState();

    GrGLVertexBuffer* vbuf;
    switch (this->getGeomSrc().fVertexSrc) {
        case kBuffer_GeometrySrcType:
            vbuf = (GrGLVertexBuffer*) this->getGeomSrc().fVertexBuffer;
            break;
        case kArray_GeometrySrcType:
        case kReserved_GeometrySrcType:
            this->finalizeReservedVertices();
            vertexOffsetInBytes += geoPoolState.fPoolStartVertex * this->getGeomSrc().fVertexSize;
            vbuf = (GrGLVertexBuffer*) geoPoolState.fPoolVertexBuffer;
            break;
        default:
            vbuf = NULL; // suppress warning
            GrCrash("Unknown geometry src type!");
    }

    GrAssert(NULL != vbuf);
    GrAssert(!vbuf->isLocked());
    vertexOffsetInBytes += vbuf->baseOffset();

    GrGLIndexBuffer* ibuf = NULL;
    if (info.isIndexed()) {
        GrAssert(NULL != indexOffsetInBytes);

        switch (this->getGeomSrc().fIndexSrc) {
        case kBuffer_GeometrySrcType:
            *indexOffsetInBytes = 0;
            ibuf = (GrGLIndexBuffer*)this->getGeomSrc().fIndexBuffer;
            break;
        case kArray_GeometrySrcType:
        case kReserved_GeometrySrcType:
            this->finalizeReservedIndices();
            *indexOffsetInBytes = geoPoolState.fPoolStartIndex * sizeof(GrGLushort);
            ibuf = (GrGLIndexBuffer*) geoPoolState.fPoolIndexBuffer;
            break;
        default:
            ibuf = NULL; // suppress warning
            GrCrash("Unknown geometry src type!");
        }

        GrAssert(NULL != ibuf);
        GrAssert(!ibuf->isLocked());
        *indexOffsetInBytes += ibuf->baseOffset();
    }
    GrGLAttribArrayState* attribState =
        fHWGeometryState.bindArrayAndBuffersToDraw(this, vbuf, ibuf);

    uint32_t usedAttribArraysMask = 0;
    const GrVertexAttrib* vertexAttrib = this->getDrawState().getVertexAttribs();
    int vertexAttribCount = this->getDrawState().getVertexAttribCount();
    for (int vertexAttribIndex = 0; vertexAttribIndex < vertexAttribCount;
         ++vertexAttribIndex, ++vertexAttrib) {

        usedAttribArraysMask |= (1 << vertexAttribIndex);
        GrVertexAttribType attribType = vertexAttrib->fType;
        attribState->set(this,
                         vertexAttribIndex,
                         vbuf,
                         GrGLAttribTypeToLayout(attribType).fCount,
                         GrGLAttribTypeToLayout(attribType).fType,
                         GrGLAttribTypeToLayout(attribType).fNormalized,
                         stride,
                         reinterpret_cast<GrGLvoid*>(
                         vertexOffsetInBytes + vertexAttrib->fOffset));
    }

    attribState->disableUnusedAttribArrays(this, usedAttribArraysMask);
}
