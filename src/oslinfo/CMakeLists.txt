# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

set ( oslinfo_srcs oslinfo.cpp )
# add_executable ( oslinfo ${oslinfo_srcs} )
# target_link_libraries (oslinfo
#                        PRIVATE oslquery)
# install_targets (oslinfo)

if (OSL_OSLQUERY_USE_STATIC_OIIO AND TARGET OpenImageIO::OpenImageIO_Util_static)
    set (oiio_util_libs OpenImageIO::OpenImageIO_Util_static ${CMAKE_DL_LIBS})
else ()
    set (oiio_util_libs OpenImageIO::OpenImageIO_Util)
endif ()


fancy_add_executable (
    NAME            oslinfo
    SRC             ${oslinfo_srcs}
    LINK_LIBRARIES  oslquery ${oiio_util_libs} 
    )
