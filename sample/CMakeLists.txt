# サンプルプログラムのソースファイルを検索
file(GLOB SAMPLE_SOURCES "*.cpp")

# ソースファイルが存在しない場合は警告を出して終了
if(NOT SAMPLE_SOURCES)
    message(STATUS "No sample source files found in sample/. Skipping sample build.")
    return()
endif()

# 各サンプルファイルごとに実行ファイルを作成
foreach(sample_file ${SAMPLE_SOURCES})
    # ファイル名から拡張子を除いた名前を取得
    get_filename_component(sample_name ${sample_file} NAME_WE)

    # 実行ファイルを作成
    add_executable(${sample_name} ${sample_file})

    # TaskKitライブラリをリンク
    target_link_libraries(${sample_name} PRIVATE TaskKit)

    # 出力ディレクトリの設定
    set_target_properties(${sample_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/samples
    )
endforeach()

# 特定のサンプルを手動で追加する場合の例：
# add_executable(example_01 example_01.cpp)
# target_link_libraries(example_01 PRIVATE TaskKit)
