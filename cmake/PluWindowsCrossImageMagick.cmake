# ImageMagick (Magick++) for the Linux -> Windows cross build, included from the root CMakeLists.txt.
#
# ImageMagick is not in vcpkg, and on Linux PluRender takes it from the system through
# FindImageMagick. A Linux host has no Windows build of it, so the official one is fetched instead:
# the "-dll" installer from the ImageMagick GitHub release. It is built with MSVC — the same ABI as
# clang-cl here, which matters for a C++ library like Magick++ — and carries the headers and the
# import libraries next to the DLLs. It is an Inno Setup installer, unpacked with innoextract
# (one-time host setup: `pacman -S innoextract` / `apt install innoextract`).
#
# Defines the same imported targets FindImageMagick does (ImageMagick::Magick++,
# ImageMagick::MagickCore), so LibEngine skips find_package and PluRender links them unchanged. The
# quantum depth / HDRI defines need no flags here: the Windows build bakes them into
# MagickCore/magick-baseconfig.h. Keep the variant (Q16-HDRI) equal to the Linux one.
#
# Updating: change the version, then the hash (sha256sum of the installer, or the "digest" of the
# release asset on GitHub).

set(PLU_WINDOWS_IMAGEMAGICK_VERSION "7.1.2-36")
set(PLU_WINDOWS_IMAGEMAGICK_SHA256 "cf116682e4af5634781f8418bbe304e4399a3303d8bd0322e56bbb2025501cea")

set(_plu_im_name "ImageMagick-${PLU_WINDOWS_IMAGEMAGICK_VERSION}-Q16-HDRI-x64-dll")
set(_plu_im_root "${CMAKE_BINARY_DIR}/WindowsImageMagick/${PLU_WINDOWS_IMAGEMAGICK_VERSION}")
set(_plu_im_app "${_plu_im_root}/app")

if(NOT EXISTS "${_plu_im_root}/.complete")
    find_program(PLU_INNOEXTRACT innoextract)
    if(NOT PLU_INNOEXTRACT)
        message(FATAL_ERROR "Windows ImageMagick: innoextract not found. It unpacks the official "
                "ImageMagick installer for the cross build; install it once (pacman -S innoextract).")
    endif()

    set(_plu_im_url "https://github.com/ImageMagick/ImageMagick/releases/download/${PLU_WINDOWS_IMAGEMAGICK_VERSION}/${_plu_im_name}.exe")
    set(_plu_im_archive "${_plu_im_root}/${_plu_im_name}.exe")
    message(STATUS "Windows ImageMagick: downloading ${_plu_im_url}")
    file(DOWNLOAD "${_plu_im_url}" "${_plu_im_archive}"
            EXPECTED_HASH SHA256=${PLU_WINDOWS_IMAGEMAGICK_SHA256}
            STATUS _plu_im_status
            TLS_VERIFY ON
    )
    list(GET _plu_im_status 0 _plu_im_code)
    if(NOT _plu_im_code EQUAL 0)
        list(GET _plu_im_status 1 _plu_im_message)
        file(REMOVE "${_plu_im_archive}")
        message(FATAL_ERROR "Windows ImageMagick: download of ${_plu_im_url} failed: ${_plu_im_message}")
    endif()

    # innoextract puts the installer's {app} files under app/.
    file(REMOVE_RECURSE "${_plu_im_app}")
    execute_process(
            COMMAND "${PLU_INNOEXTRACT}" --silent --output-dir "${_plu_im_root}" "${_plu_im_archive}"
            RESULT_VARIABLE _plu_im_result
            ERROR_VARIABLE _plu_im_error
    )
    if(NOT _plu_im_result EQUAL 0)
        message(FATAL_ERROR "Windows ImageMagick: innoextract failed (${_plu_im_result}):\n${_plu_im_error}")
    endif()
    file(REMOVE "${_plu_im_archive}")
    file(TOUCH "${_plu_im_root}/.complete")
endif()

foreach(_plu_im_component IN ITEMS MagickCore Magick++)
    set(_plu_im_implib "${_plu_im_app}/lib/CORE_RL_${_plu_im_component}_.lib")
    set(_plu_im_dll "${_plu_im_app}/CORE_RL_${_plu_im_component}_.dll")
    if(NOT EXISTS "${_plu_im_implib}" OR NOT EXISTS "${_plu_im_dll}")
        message(FATAL_ERROR "Windows ImageMagick: ${_plu_im_component} missing in ${_plu_im_app}; "
                "delete ${_plu_im_root} to fetch it again.")
    endif()
    add_library(ImageMagick::${_plu_im_component} SHARED IMPORTED)
    set_target_properties(ImageMagick::${_plu_im_component} PROPERTIES
            IMPORTED_IMPLIB "${_plu_im_implib}"
            IMPORTED_LOCATION "${_plu_im_dll}"
            INTERFACE_INCLUDE_DIRECTORIES "${_plu_im_app}/include"
    )
endforeach()
set_target_properties(ImageMagick::Magick++ PROPERTIES INTERFACE_LINK_LIBRARIES ImageMagick::MagickCore)

# The unpacked installer; the distribution takes the DLLs, coder/filter modules and configuration
# files from here.
set(PLU_WINDOWS_IMAGEMAGICK_DIR "${_plu_im_app}" CACHE INTERNAL "Unpacked Windows ImageMagick (cross build)")
