# TaskKit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

C++20コルーチン向けの直感的で軽量なタスクシステムをフレームベーススケジューリングで提供します。

<!-- LANG_LINKS_START -->
**📖 Languages: [English](README.md) | [日本語](README_ja.md)**
<!-- LANG_LINKS_END -->

* **ヘッダーオンリーライブラリ** - 簡単に統合可能、インクルードするだけで使用可能
* **C++20コルーチン** - `co_await`によるモダンなasync/await構文
* **フレームベーススケジューリング** - ゲームループやリアルタイムアプリケーションに最適
* **時間ベースの遅延** - フレーム遅延（`DelayFrame`）と時間ベースの待機（`WaitFor`）の両方をサポート
* **タスク合成** - `WhenAll`で複数のタスクを結合
* **設計段階からのスレッドローカル** - スレッドローカルストレージによる自動スレッドセーフティ
* **依存関係ゼロ** - C++20標準ライブラリのみを必要とします

---

- [はじめに](#はじめに)
  - [必要要件](#必要要件)
  - [インストール](#インストール)
- [クイックスタート](#クイックスタート)
- [コアコンセプト](#コアコンセプト)
  - [タスクのライフサイクル](#タスクのライフサイクル)
  - [TaskSystemとScheduler](#tasksystemとscheduler)
- [使用例](#使用例)
  - [遅延実行](#遅延実行)
  - [戻り値を持つタスク](#戻り値を持つタスク)
  - [並行タスク](#並行タスク)
  - [キャンセル可能なタスク](#キャンセル可能なタスク)
- [APIリファレンス](#apiリファレンス)
  - [コア型](#コア型)
  - [ユーティリティ関数](#ユーティリティ関数)
  - [タスクメソッド](#タスクメソッド)
- [ビルドとテスト](#ビルドとテスト)
  - [ビルドオプション](#ビルドオプション)
  - [テストの実行](#テストの実行)
- [ライセンス](#ライセンス)

---

## はじめに

### 必要要件

C++20コルーチンサポートを持つコンパイラ:

- **GCC 11以降**（推奨） - `-std=c++20`でコルーチンがデフォルトで有効化
  - GCC 10は実験的サポートですが、`-fcoroutines`フラグが必要で既知のバグがあります
- **Clang 14以降**（推奨） - 完全なC++20コルーチンサポート
  - Clang 8-13は部分的サポートですが、本番環境では推奨されません
- **MSVC 19.28以降**（Visual Studio 2019 16.8以降） - フィーチャー完全なC++20コルーチン
  - `/std:c++20`または`/std:c++latest`を使用
- **AppleClang 12以降** - 完全なコルーチンサポート

CMake 3.15以降

### インストール

#### ヘッダーオンリーライブラリとして

1. `include`ディレクトリをプロジェクトにコピー
2. コンパイラ設定にインクルードパスを追加
3. コード内で`TaskKit.h`をインクルード

#### CMakeを使用

```cmake
add_subdirectory(TaskKit)
target_link_libraries(your_target PRIVATE TaskKit)
```

---

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

---

## コアコンセプト

### タスクのライフサイクル

TaskKitのタスクはシンプルなライフサイクルに従います：

1. **作成** - タスクコルーチンは即座に開始（エントリで中断されない）
2. **中断** - `co_yield {}`またはユーティリティのawaitにより次フレームでの再開がスケジュールされる
3. **完了** - `co_return`が結果を設定し、継続を再開
4. **忘れられたタスク** - `.Forget()`が完了時の自動破棄をマーク（ファイア・アンド・フォーゲット）

> **注意**: タスクはムーブオンリーで、`[[nodiscard]]`でマークされており、偶発的なリソースリークを防ぎます。

### TaskSystemとScheduler

**TaskSystem**はスレッドローカルストレージを使用して自動的にスレッド分離を行います。各スレッドは独自の独立したスケジューラセットを維持します。

**主な特徴:**
- 使用前にスレッドごとに`TaskSystem::Initialize()`を呼び出す必要があります
- スケジューラはスレッドIDを含む一意のIDで識別されます
- `SchedulerRegistration` RAIIガードが現在のスケジューラコンテキストを管理
- 設計段階からスレッドセーフ - クロススレッドスケジューラアクセスは不可

**典型的なパターン:**

```cpp
// スレッドローカルTaskSystemを初期化
TaskSystem::Initialize();

// スケジューラーを作成
auto id = TaskSystem::CreateScheduler();

// 現在として登録（RAIIガード）
{
    auto reg = TaskSystem::RegisterScheduler(id);

    // ここで作成されたタスクはこのスケジューラを使用
    MyTask().Forget();

} // 自動的に登録解除

// クリーンアップ
TaskSystem::Shutdown();
```

---

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

---

## APIリファレンス

### コア型

#### `Task<T>`

非同期操作を表すメインのコルーチン型。

- **テンプレートパラメータ**: `T` - 戻り値の型（voidの場合は`Task<>`を使用）
- **ムーブオンリー**: コピー不可、ムーブのみ
- **即座実行**: 作成時に即座に開始

#### `TaskScheduler`

コルーチン実行を管理するフレームベーススケジューラ。

- `Update()` - 現在のフレームで保留中のすべてのタスクを処理
- `GetPendingTaskCount()` - 実行待ちのタスク数を返す

#### `TaskSystem`

スレッドローカルストレージで複数のスケジューラを管理する静的クラス。

- `Initialize(config)` - スレッドローカル状態を初期化（使用前に必須）
- `Shutdown()` - スレッドローカル状態をクリーンアップ
- `CreateScheduler()` - 新しいスケジューラを作成し、IDを返す
- `DestroyScheduler(id)` - スケジューラを削除
- `RegisterScheduler(id)` - スケジューラを現在として設定するRAIIガードを返す
- `GetScheduler(id)` - IDでスケジューラを取得
- `GetCurrentScheduler()` - 現在アクティブなスケジューラを取得

### ユーティリティ関数

#### `DelayFrame(int frames)`

指定フレーム数だけ実行を中断します。

```cpp
co_await DelayFrame(5);  // 5フレーム待機
```

#### `WaitFor(duration)`

指定時間が経過するまで実行を中断します。

```cpp
co_await WaitFor(2s);         // 2秒待機
co_await WaitFor(500ms);      // 500ミリ秒待機
```

#### `WaitUntil(timepoint)`

特定の時点まで実行を中断します。

```cpp
auto target = std::chrono::steady_clock::now() + 5s;
co_await WaitUntil(target);
```

#### `WhenAll(tasks...)`

複数のタスクの完了を待機し、結果のタプルを返します。

```cpp
auto [result1, result2] = co_await WhenAll(Task1(), Task2());
```

#### `GetCompletedTask()`

即座に完了したタスクを返します（条件分岐に便利）。

```cpp
co_await (condition ? ActualTask() : GetCompletedTask());
```

### タスクメソッド

#### `.Forget()`

自動クリーンアップを伴うファイア・アンド・フォーゲット実行をマークします。

```cpp
MyBackgroundTask().Forget();
```

> **重要**: `.Forget()`なしでは、タスクは保存またはawaitする必要があります。そうしないと早期破棄されます。

#### `.IsDone()`

タスクが実行完了したかを確認します。

```cpp
auto task = MyTask();
if (task.IsDone()) { /* ... */ }
```

#### `.IsReady()`

タスクの結果が取得可能かを確認します。

```cpp
if (task.IsReady()) {
    auto result = co_await task;
}
```

---

## ビルドとテスト

### ビルドオプション

```bash
# デフォルトオプションで構成
cmake -B build -DCMAKE_BUILD_TYPE=Release

# ビルド
cmake --build build

# カスタムビルド
cmake -B build \
  -DBUILD_SAMPLES=OFF \  # サンプルをビルドしない
  -DBUILD_TESTS=OFF \    # テストをビルドしない
  -DUSE_GTEST=ON         # Google Testを使用（デフォルト）
```

### テストの実行

```bash
# CTestですべてのテストを実行
ctest --test-dir build

# テスト実行ファイルを直接実行
./build/bin/tests/TaskKit_tests

# Google Testフィルタで特定のテストを実行
./build/bin/tests/TaskKit_tests --gtest_filter=TaskKitTest.DelayFrame*
```

### サンプルの実行

```bash
./build/bin/samples/QuickStart
```

---

## ライセンス

このプロジェクトはMITライセンスの下でライセンスされています。詳細は[LICENSE](LICENSE)ファイルを参照してください。
