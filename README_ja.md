# TaskKit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

C++20コルーチン向けの直感的で軽量なタスクシステムを、フレームベーススケジューリングで提供します。

<!-- LANG_LINKS_START -->
**Languages: [English](README.md) | [日本語](README_ja.md)**
<!-- LANG_LINKS_END -->

* **ヘッダーオンリーライブラリ** - 簡単に統合でき、インクルードするだけで使えます
* **C++20コルーチン** - `co_await`によるモダンなasync/await構文
* **フレームベーススケジューリング** - ゲームループやリアルタイムアプリケーションに最適
* **時間ベースの遅延** - フレーム遅延（`DelayFrame`）と時間ベースの待機（`WaitFor`）の両方をサポート
* **タスク合成** - `WhenAll`と`WhenAny`で複数のタスクを組み合わせ可能
* **スレッドプール対応** - `SwitchToThreadPool`と`RunOnThreadPool`による組み込みスレッドプール
* **効率的なメモリ確保** - プールアロケータでコルーチンフレームのヒープオーバーヘッドを削減
* **カスタマイズ可能なアロケータ** - 独自のアロケータで細かく制御可能
* **依存関係ゼロ** - C++20標準ライブラリのみが必要

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
  - [スレッドプール操作](#スレッドプール操作)
  - [キャンセル可能なタスク](#キャンセル可能なタスク)
  - [カスタムアロケータ](#カスタムアロケータ)
- [高度な機能](#高度な機能)
  - [カスタムAwaitable型](#カスタムawaitable型)
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

C++20コルーチンをサポートするコンパイラが必要です：

- **GCC 11以降**（推奨） - `-std=c++20`でコルーチンがデフォルトで有効化されます
  - GCC 10は実験的サポートですが、`-fcoroutines`フラグが必要で既知のバグがあります
- **Clang 14以降**（推奨） - 完全なC++20コルーチンサポート
  - Clang 8-13は部分的サポートですが、本番環境では推奨されません
- **MSVC 19.28以降**（Visual Studio 2019 16.8以降） - 機能完全なC++20コルーチン
  - `/std:c++20`または`/std:c++latest`を使用してください
- **AppleClang 12以降** - 完全なコルーチンサポート

CMake 3.15以降

### インストール

#### ヘッダーオンリーライブラリとして

1. `include`ディレクトリをプロジェクトにコピーします
2. コンパイラ設定にインクルードパスを追加します
3. コード内で`TaskKit.h`をインクルードします

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
    // TaskSystemを初期化（スレッドプールも自動的に作成）
    TaskSystem::Initialize();

    {
        // スケジューラを作成してアクティブ化
        auto id = TaskSystem::CreateScheduler();
        auto activation = TaskSystem::ActivateScheduler(id);

        // タスクを開始（ファイア・アンド・フォーゲット）
        ExampleTask().Forget();

        // 毎フレームスケジューラを更新
        while (TaskSystem::GetPendingTaskCount(id) > 0)
        {
            TaskSystem::UpdateActivatedScheduler();
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

TaskKitのタスクは、シンプルなライフサイクルに従います：

1. **作成** - タスクコルーチンは即座に開始されます（エントリ時点では中断されません）
2. **中断** - `co_yield {}`またはユーティリティ関数のawaitにより、次フレームでの再開がスケジュールされます
3. **完了** - `co_return`が結果を設定し、継続処理を再開します
4. **忘れられたタスク** - `.Forget()`により、完了時に自動的に破棄されます（ファイア・アンド・フォーゲット）

> **注意**: タスクはムーブオンリーで、`[[nodiscard]]`でマークされており、意図しないリソースリークを防ぎます。

### TaskSystemとScheduler

**TaskSystem**は、スケジューラと組み込みスレッドプールを管理します。使用前に初期化が必要です。

**主な特徴:**
- アプリケーション起動時に`TaskSystem::Initialize()`を一度呼び出します
- スケジューラはスレッドIDを含む一意のIDで識別されます
- `SchedulerActivation` RAIIガードが現在のスケジューラコンテキストを管理します
- 重い計算をオフロードするための組み込みスレッドプール

**典型的なパターン:**

```cpp
// TaskSystemを初期化（スレッドプールも作成）
TaskSystem::Initialize();

// メインスレッド用のスケジューラを作成
auto id = TaskSystem::CreateScheduler();

// 現在のスケジューラとしてアクティブ化（RAIIガード）
{
    auto activation = TaskSystem::ActivateScheduler(id);

    // ここで作成されたタスクはこのスケジューラを使用します
    MyTask().Forget();

    // 更新ループ
    while (TaskSystem::GetPendingTaskCount(id) > 0)
    {
        TaskSystem::UpdateActivatedScheduler();
    }

} // 自動的に非アクティブ化されます

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
Task<> WhenAllExample()
{
    auto task1 = []() -> Task<int> {
        co_await WaitFor(2s);
        co_return 1;
    };

    auto task2 = []() -> Task<int> {
        co_await WaitFor(3s);
        co_return 2;
    };

    // すべてのタスクが完了するまで待機
    auto [result1, result2] = co_await WhenAll(task1(), task2());
    std::printf("結果: %d, %d\n", result1, result2);
}

Task<> WhenAnyExample()
{
    auto task1 = []() -> Task<int> {
        co_await WaitFor(2s);
        co_return 1;
    };

    auto task2 = []() -> Task<int> {
        co_await WaitFor(1s);
        co_return 2;
    };

    // 最初に完了したタスクを待機
    auto result = co_await WhenAny(task1(), task2());
    // resultはstd::variant<int, int>
    std::printf("最初に完了したインデックス: %zu\n", result.index());
}
```

### スレッドプール操作

```cpp
Task<> ThreadPoolExample(TaskSchedulerId mainSchedulerId)
{
    // 重い計算のためにスレッドプールに切り替え
    co_await SwitchToThreadPool();

    // ワーカースレッドで実行中
    auto result = HeavyComputation();

    // メインスケジューラに戻る
    co_await SwitchToSelectedScheduler(mainSchedulerId);

    // メインスレッドに戻った
    UpdateUI(result);
}

Task<> RunOnThreadPoolExample()
{
    // RunOnThreadPoolは自動的に元のスケジューラに戻る
    int result = co_await RunOnThreadPool([]() {
        return HeavyComputation();
    });

    // 自動的に元のスレッドに戻っている
    UpdateUI(result);
}

Task<> RunOnThreadPoolWithTaskExample()
{
    // Task<T>を返す関数にも対応
    auto data = co_await RunOnThreadPool([]() -> Task<std::vector<int>> {
        co_await SomeAsyncOperation();
        co_return ProcessData();
    });
}
```

### キャンセル可能なタスク

```cpp
Task<> CancellableTask(std::stop_token stopToken)
{
    for (int i = 0; i < 10; ++i)
    {
        // キャンセル確認
        ThrowIfStopRequested(stopToken);

        co_await DelayFrame(1);
        std::printf("反復 %d\n", i);
    }
}
```

### カスタムアロケータ

```cpp
// コルーチンフレーム用のカスタムアロケータを定義
struct MyAllocatorContext {
    // アロケータの状態
};

MyAllocatorContext myContext;

TaskAllocator myAllocator{
    &myContext,
    [](void* ctx, std::size_t size) -> void* {
        // カスタム確保
        auto* context = static_cast<MyAllocatorContext*>(ctx);
        return myCustomAlloc(context, size);
    },
    [](void* ctx, void* ptr, std::size_t size) {
        // カスタム解放
        auto* context = static_cast<MyAllocatorContext*>(ctx);
        myCustomFree(context, ptr, size);
    }
};

// カスタム設定でTaskSystemを初期化
auto config = TaskSystemConfiguration::Builder()
    .WithCustomAllocator(myAllocator)
    .WithThreadPoolSize(4)        // ワーカースレッド数
    .WithReservedTaskCount(200)   // スケジューラごとの予約タスク数
    .Build();

TaskSystem::Initialize(config);
```

> **注意**: デフォルトでは、TaskKitはヒープ確保のオーバーヘッドを削減する効率的なプールアロケータを使用します。スレッドプールサイズのデフォルトは`std::thread::hardware_concurrency()`です。

---

## 高度な機能

### カスタムAwaitable型

TaskKitは、`AwaitTransformer`を使用して独自の型をTaskコルーチン内でawait可能にする拡張メカニズムを提供します。

**仕組み:**

`AwaitTransformer`テンプレートを特殊化することで、独自の型に対する変換ロジックを定義できます。Task内で`co_await yourCustomType`を使用すると、`AwaitTransformer<YourType>::Transform()`が呼び出され、awaitable型に変換されます。

**例:**

```cpp
// 独自の型
struct MyCustomEvent
{
    int eventId;
    std::string eventData;
};

// 独自の型に対してAwaitTransformerを特殊化
namespace TKit
{
    template<>
    class AwaitTransformer<MyCustomEvent>
    {
    public:
        static auto Transform(MyCustomEvent&& event)
        {
            // この型を処理するawaiterを返す
            struct Awaiter
            {
                MyCustomEvent event;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    // 独自の中断処理
                    // 例: イベントハンドラの登録、コールバックのスケジューリングなど
                }

                MyCustomEvent await_resume()
                {
                    return std::move(event);
                }
            };

            return Awaiter{ std::move(event) };
        }
    };
}

// これでTask内で使用できます
Task<> ProcessEvent()
{
    MyCustomEvent event{ 42, "data" };

    // 独自の型がawait可能になりました
    auto result = co_await event;

    std::printf("Processed event: %d\n", result.eventId);
}
```

**要件:**

1. `TKit`名前空間内で`AwaitTransformer<T>`を特殊化します
2. その型を受け取る静的な`Transform()`メソッドを提供します
3. 標準的なawaiterインターフェースを持つオブジェクトを返します:
   - `await_ready()` - 結果が即座に利用可能な場合にtrueを返します
   - `await_suspend(handle)` - コルーチンが中断されるときに呼び出されます
   - `await_resume()` - コルーチンが再開されるときに結果を返します

**ユースケース:**

- イベントシステムとの統合（UIイベント、ネットワークイベント）
- コールバックベースのAPIをコルーチン対応にラップ
- 独自の同期プリミティブ
- ドメイン固有の非同期操作

> **注意**: `Awaitable`コンセプトは、有効な`AwaitTransformer`特殊化を持つ型を自動的に検出します。

---

## APIリファレンス

### コア型

#### `Task<T>`

非同期操作を表すメインのコルーチン型です。

- **テンプレートパラメータ**: `T` - 戻り値の型（voidの場合は`Task<>`を使用します）
- **ムーブオンリー**: コピーはできず、ムーブのみ可能です
- **即座実行**: 作成時に即座に開始されます

#### `TaskSystem`

スケジューラとスレッドプールを管理する静的クラスです。

- `Initialize(config)` - オプションの設定でTaskSystemを初期化します
- `Shutdown()` - クリーンアップしてスレッドプールを破棄します
- `IsInitialized()` - TaskSystemが初期化されているかチェックします
- `CreateScheduler(threadId, reservedCount)` - 新しいスケジューラを作成し、IDを返します
- `ActivateScheduler(id)` - スケジューラをアクティブ化するRAIIガードを返します
- `UpdateActivatedScheduler()` - アクティブなスケジューラの保留中のタスクを処理します
- `GetPendingTaskCount(id)` - 保留中のタスク数を取得します
- `GetActivatedSchedulerId()` - 現在アクティブなスケジューラIDを取得します
- `Schedule(id, handle)` - コルーチンハンドルを特定のスケジューラにスケジュールします

#### `TaskSystemConfiguration::Builder`

TaskSystem設定のビルダーです。

- `WithCustomAllocator(allocator)` - カスタムメモリアロケータを設定します
- `WithThreadPoolSize(size)` - ワーカースレッド数を設定します（0 = hardware_concurrency）
- `WithReservedTaskCount(count)` - スケジューラごとの予約タスクスロット数を設定します
- `Build()` - 設定オブジェクトを作成します

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

#### `WhenAny(tasks...)`

最初に完了したタスクを待機し、結果のvariantを返します。

```cpp
auto result = co_await WhenAny(Task1(), Task2());
// result.index()で最初に完了したタスクを判別
```

#### `SwitchToThreadPool()`

コルーチンの実行をスレッドプールに切り替えます。

```cpp
co_await SwitchToThreadPool();
// ワーカースレッドで実行中
```

#### `SwitchToSelectedScheduler(id)`

コルーチンの実行を指定されたスケジューラに切り替えます。

```cpp
co_await SwitchToSelectedScheduler(mainSchedulerId);
// メインスレッドで実行中
```

#### `RunOnThreadPool(func)`

スレッドプールで関数を実行し、自動的に元のスケジューラに戻ります。

```cpp
// 通常の関数の場合
int result = co_await RunOnThreadPool([]() { return compute(); });

// Taskを返す関数の場合
auto data = co_await RunOnThreadPool([]() -> Task<Data> {
    co_return co_await AsyncCompute();
});
```

#### `GetCompletedTask()`

即座に完了したタスクを返します（条件分岐に便利です）。

```cpp
co_await (condition ? ActualTask() : GetCompletedTask());
```

### タスクメソッド

#### `.Forget()`

自動クリーンアップを伴うファイア・アンド・フォーゲット実行をマークします。

```cpp
MyBackgroundTask().Forget();
```

> **重要**: `.Forget()`を使わない場合、タスクは変数に保存するか、awaitする必要があります。そうしないと、タスクが想定より早く破棄されてしまいます。

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
