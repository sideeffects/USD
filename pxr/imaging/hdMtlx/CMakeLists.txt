set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdMtlx)

if (${PXR_ENABLE_MATERIALX_SHARED_LIBS})
    add_definitions(-DMATERIALX_BUILD_SHARED_LIBS)
endif()

pxr_library(hdMtlx
    LIBRARIES
        gf
        hd
        sdf
        sdr
        tf
        trace
        usdMtlx
        vt
        ${MATERIALX_LIBRARIES}

    INCLUDE_DIRS
        ${MATERIALX_INCLUDE_DIRS}

    PUBLIC_CLASSES
        hdMtlx

    PUBLIC_HEADERS
        api.h
)
