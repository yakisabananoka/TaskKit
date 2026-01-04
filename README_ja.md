# TaskKit

[English](README.md) | [日本語](README_ja.md)

## 概要

TaskKitは、フレームベースのスケジューリングで非同期タスクを管理するための軽量なヘッダーオンリーC++20コルーチンライブラリです。モダンなC++コルーチンを使用して、直感的なAPIで非同期コードを記述できます。

## 特徴

- **ヘッダーオンリーライブラリ** - 簡単に統合可能、インクルードするだけで使用可能
- **C++20コルーチン** - モダンなasync/await構文
- **フレームベーススケジューリング** - ゲームループやリアルタイムアプリケーションに最適
- **時間ベースの遅延** - フレーム遅延と時間ベースの待機の両方をサポート
- **タスク合成** - `WhenAll`で複数のタスクを結合
- **ストップトークンサポート** - `std::stop_token`によるキャンセル可能なタスク
- **依存関係ゼロ** - C++20標準ライブラリのみを必要とします
- **軽量** - 最小限のオーバーヘッド設計

## 必要要件

- C++20コルーチンサポートを持つコンパイラ:
  - **GCC 11以降**（推奨） - `-std=c++20`でコルーチンがデフォルトで有効化
    - GCC 10は実験的サポートですが、`-fcoroutines`フラグが必要で既知のバグがあります
  - **Clang 14以降**（推奨） - 完全なC++20コルーチンサポート
    - Clang 8-13は部分的サポートですが、本番環境では推奨されません
  - **MSVC 19.28以降**（Visual Studio 2019 16.8以降） - フィーチャー完全なC++20コルーチン
    - `/std:c++20`または`/std:c++latest`を使用
  - **AppleClang 12以降** - 完全なコルーチンサポート
- CMake 3.15以降

## クイックスタート

```cpp
#include <TaskKit.h>
using namespace TKit;

Task<> ExampleTask()
{
    std::printf("タスク開始\n");

    // 3フレーム待機
    co_await DelayFrame(3);

    std::printf("3フレーム後\n");

    // 2秒待機
    co_await WaitFor(2s);

    std::printf("タスク完了\n");
}

int main()
{
    // TaskSystemを初期化（スレッドごとに1回）
    TaskSystem::Initialize();

    {
        // スケジューラーを作成して登録
        auto id = TaskSystem::CreateScheduler();
        auto registration = TaskSystem::RegisterScheduler(id);

        // タスクを開始（ファイア・アンド・フォーゲット）
        ExampleTask().Forget();

        // 毎フレームスケジューラーを更新
        auto& scheduler = TaskSystem::GetScheduler(id);
        while (scheduler.GetPendingTaskCount() > 0)
        {
            scheduler.Update();
            std::this_thread::sleep_for(16ms); // 約60 FPS
        }
    }

    // クリーンアップ
    TaskSystem::Shutdown();
    return 0;
}
```

## インストール

### ヘッダーオンリーライブラリとして

1. `include`ディレクトリをプロジェクトにコピー
2. コンパイラ設定にインクルードパスを追加
3. コード内で`TaskKit.h`をインクルード

### CMakeを使用

```cmake
add_subdirectory(TaskKit)
target_link_libraries(your_target PRIVATE TaskKit)
```

## 使用例

### 遅延実行

```cpp
Task<> DelayExample()
{
    // フレーム数による遅延
    co_await DelayFrame(5);  // 5フレーム待機

    // 時間による遅延
    co_await WaitFor(1s);    // 1秒待機

    // 特定の時刻まで遅延
    auto targetTime = std::chrono::steady_clock::now() + 5s;
    co_await WaitUntil(targetTime);
}
```

### 戻り値を持つタスク

```cpp
Task<int> CalculateAsync()
{
    co_await DelayFrame(1);
    co_return 42;
}

Task<> UseResult()
{
    int result = co_await CalculateAsync();
    std::printf("結果: %d\n", result);
}
```

### 並行タスク

```cpp
Task<> ConcurrentExample()
{
    auto task1 = []() -> Task<> {
        co_await WaitFor(2s);
        std::printf("タスク1完了\n");
    };

    auto task2 = []() -> Task<> {
        co_await WaitFor(3s);
        std::printf("タスク2完了\n");
    };

    // すべてのタスクが完了するまで待機
    co_await WhenAll(task1(), task2());
    std::printf("全タスク完了\n");
}
```

### キャンセル可能なタスク

```cpp
Task<> CancellableTask(std::stop_token stopToken)
{
    for (int i = 0; i < 10; ++i)
    {
        // キャンセル確認
        TASKKIT_STOP_TOKEN_PROCESS(stopToken);

        co_await DelayFrame(1);
        std::printf("反復 %d\n", i);
    }
}
```

## ビルド

```bash
# 構成
cmake -B build -DCMAKE_BUILD_TYPE=Release

# ビルド
cmake --build build

# サンプルなしでビルド
cmake -B build -DBUILD_SAMPLES=OFF

# テストなしでビルド
cmake -B build -DBUILD_TESTS=OFF
```

## テストの実行

```bash
# すべてのテストを実行
ctest --test-dir build

# 特定のテストを実行
./build/bin/tests/TaskKit_tests --gtest_filter=TaskKitTest.DelayFrame*
```

## APIリファレンス

### コア型

- `Task<T>` - オプションの戻り値を持つコルーチンタスク型
- `TaskScheduler` - タスク実行のためのフレームベーススケジューラ
- `TaskSystem` - 複数のスケジューラを管理するシングルトン

### ユーティリティ関数

- `DelayFrame(int frames)` - フレーム数による実行遅延
- `WaitFor(duration)` - 時間による実行遅延
- `WaitUntil(timepoint)` - 特定の時刻まで遅延
- `WhenAll(tasks...)` - 複数のタスクを並行的に待機
- `GetCompletedTask()` - 即座に完了したタスクを取得

### タスクメソッド

- `.Forget()` - ファイア・アンド・フォーゲット実行（自動クリーンアップ）
- `.IsDone()` - タスクが完了したかを確認
- `.IsReady()` - タスクの結果が準備できているかを確認

## ライセンス

このプロジェクトはMITライセンスの下でライセンスされています。詳細は[LICENSE](LICENSE)ファイルを参照してください。
