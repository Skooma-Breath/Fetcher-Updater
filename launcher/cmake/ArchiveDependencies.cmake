# Native archive decoders. Source archives are pinned by release SHA-256.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fetcher_zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
    URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16)
foreach(tool XZ XZDEC LZMADEC LZMAINFO SCRIPTS)
    set(XZ_TOOL_${tool} OFF CACHE BOOL "" FORCE)
endforeach()
set(XZ_NLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fetcher_xz
    URL https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz
    URL_HASH SHA256=0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9)
FetchContent_MakeAvailable(fetcher_zlib fetcher_xz)
FetchContent_Declare(fetcher_bzip2
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269)
FetchContent_MakeAvailable(fetcher_bzip2)
add_library(fetcher-bzip2 STATIC)
foreach(source blocksort huffman crctable randtable compress decompress bzlib)
    target_sources(fetcher-bzip2 PRIVATE "${fetcher_bzip2_SOURCE_DIR}/${source}.c")
endforeach()
target_include_directories(fetcher-bzip2 PUBLIC "${fetcher_bzip2_SOURCE_DIR}")


# Link the pinned in-tree codec targets explicitly. Do not discover arbitrary
# machine-installed codec DLLs, or build libarchive command-line programs.
foreach(feature OPENSSL MBEDTLS NETTLE MD LIBB2 LZ4 LZO LZMA ZSTD ZLIB BZip2
                LIBXML2 BSDXML EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX LIBGCC
                CNG TAR CPIO CAT UNZIP XATTR ACL ICONV TEST COVERAGE INSTALL)
    set(ENABLE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE)
set(HAVE_LIBLZMA 1)
set(HAVE_LZMA_H 1)
set(HAVE_ZLIB_H 1)
set(HAVE_BZLIB_H 1)
set(MSVC_USE_STATIC_CRT ON CACHE BOOL "" FORCE)
FetchContent_Declare(fetcher_libarchive
    URL https://github.com/libarchive/libarchive/releases/download/v3.8.10/libarchive-3.8.10.zip
    URL_HASH SHA256=a15b2890f31c935e5fb5b4026e0c463ed03e87fa6365060659e761e97f401183)
FetchContent_MakeAvailable(fetcher_libarchive)
target_compile_definitions(archive_static PRIVATE HAVE_LIBLZMA=1 HAVE_LZMA_H=1 HAVE_ZLIB_H=1 HAVE_BZLIB_H=1)
target_link_libraries(archive_static liblzma zlibstatic fetcher-bzip2)
target_include_directories(archive_static PRIVATE "${fetcher_xz_SOURCE_DIR}/src/liblzma/api")
target_include_directories(archive_static INTERFACE "${fetcher_libarchive_SOURCE_DIR}/libarchive")
target_compile_definitions(archive_static INTERFACE LIBARCHIVE_STATIC)
