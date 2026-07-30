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
