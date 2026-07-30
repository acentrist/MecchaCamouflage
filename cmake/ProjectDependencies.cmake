include_guard(GLOBAL)

include(FetchContent)

# Glaze is shared by the project-owned strict manifest/config readers and the
# pinned UE4SS graph. Declaring it at the composition root guarantees one
# immutable checkout in both secret-free and full builds.
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG 3a850807501d98d23bab4bdc5af64d8d4e83e6bc
)
FetchContent_MakeAvailable(glaze)

# Only the static WebP decoder is linked into the native Image Paint import
# path. Tooling, encoders, muxers, viewers, and threaded decode are excluded
# from the source graph.
set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG 4fa21912338357f89e4fd51cf2368325b59e9bd9
)
FetchContent_MakeAvailable(libwebp)
set_target_properties(
    sharpyuv
    webpencode
    webpdsp
    webputils
    webp
    webpdemux
    PROPERTIES EXCLUDE_FROM_ALL TRUE
)
