# テストソースファイルを検索
file(GLOB TEST_SOURCES "*.cpp")

# ソースファイルが存在しない場合は警告を出して終了
if(NOT TEST_SOURCES)
    message(STATUS "No test source files found in tests/. Skipping test build.")
    return()
endif()

# テストフレームワークの選択（Google TestまたはCatch2）
# オプション1: Google Test（FetchContentを使用）
option(USE_GTEST "Use Google Test framework" ON)

if(USE_GTEST)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    # Windows用の設定
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # テスト実行ファイルを作成
    add_executable(TaskKit_tests ${TEST_SOURCES})

    # ライブラリとテストフレームワークをリンク
    target_link_libraries(TaskKit_tests
        PRIVATE
            TaskKit
            GTest::gtest_main
    )

    # テストを登録
    include(GoogleTest)
    gtest_discover_tests(TaskKit_tests)

else()
    # オプション2: Catch2を使用する場合
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(TaskKit_tests ${TEST_SOURCES})

    target_link_libraries(TaskKit_tests
        PRIVATE
            TaskKit
            Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(TaskKit_tests)
endif()

# 出力ディレクトリの設定
set_target_properties(TaskKit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
