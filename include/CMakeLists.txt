# ヘッダーオンリーライブラリとしてインターフェースライブラリを作成
add_library(TaskKit INTERFACE)

# インクルードディレクトリの設定
target_include_directories(TaskKit INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# C++標準の要求
target_compile_features(TaskKit INTERFACE cxx_std_20)

# インストール設定（オプション）
install(TARGETS TaskKit
    EXPORT TaskKitTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/TaskKit
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
