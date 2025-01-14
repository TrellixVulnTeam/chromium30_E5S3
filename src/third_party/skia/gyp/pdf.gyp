{
  'targets': [
    {
      'target_name': 'pdf',
      'product_name': 'skia_pdf',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'skia_lib.gyp:skia_lib',
        'zlib.gyp:zlib',
      ],
      'include_dirs': [
        '../include/pdf',
        '../src/core', # needed to get SkGlyphCache.h and SkTextFormatParams.h
        '../src/utils', # needed to get SkBitSet.h
      ],
      'sources': [
        '../include/pdf/SkPDFDevice.h',
        '../include/pdf/SkPDFDocument.h',

        '../src/pdf/SkPDFCatalog.cpp',
        '../src/pdf/SkPDFCatalog.h',
        '../src/pdf/SkPDFDevice.cpp',
        '../src/pdf/SkPDFDocument.cpp',
        '../src/pdf/SkPDFFont.cpp',
        '../src/pdf/SkPDFFont.h',
        '../src/pdf/SkPDFFontImpl.h',
        '../src/pdf/SkPDFFormXObject.cpp',
        '../src/pdf/SkPDFFormXObject.h',
        '../src/pdf/SkPDFGraphicState.cpp',
        '../src/pdf/SkPDFGraphicState.h',
        '../src/pdf/SkPDFImage.cpp',
        '../src/pdf/SkPDFImage.h',
        '../src/pdf/SkPDFImageStream.cpp',
        '../src/pdf/SkPDFImageStream.h',
        '../src/pdf/SkPDFPage.cpp',
        '../src/pdf/SkPDFPage.h',
        '../src/pdf/SkPDFResourceDict.cpp',
        '../src/pdf/SkPDFResourceDict.h',
        '../src/pdf/SkPDFShader.cpp',
        '../src/pdf/SkPDFShader.h',
        '../src/pdf/SkPDFStream.cpp',
        '../src/pdf/SkPDFStream.h',
        '../src/pdf/SkPDFTypes.cpp',
        '../src/pdf/SkPDFTypes.h',
        '../src/pdf/SkPDFUtils.cpp',
        '../src/pdf/SkPDFUtils.h',
        '../src/pdf/SkTSet.h',

        '../src/doc/SkDocument_PDF.cpp',
      ],
      # This section makes all targets that depend on this target
      # #define SK_SUPPORT_PDF and have access to the pdf header files.
      'direct_dependent_settings': {
        'defines': [
          'SK_SUPPORT_PDF',
        ],
        'include_dirs': [
          '../include/pdf',
        ],
      },
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
