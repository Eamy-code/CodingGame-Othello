# Othello ローカル対戦環境構築ガイド

## 1. 目的

本書は、Windows上で新旧Othello AIのローカル対戦BATを実行するために、Python、MSYS2、GCCを導入する手順を説明します。

対戦環境の動作仕様は[Othello ローカル対戦テスト仕様書](Othello_Local_Match_Test_Specification.md)を参照してください。

## 2. 前提環境

| 項目 | 要件 |
|---|---|
| OS | Windows 10またはWindows 11 |
| ターミナル | Windows PowerShellまたはPowerShell 7 |
| Python | Python 3 |
| C++コンパイラ | C++17対応の`g++` |
| 推奨C++環境 | MSYS2 UCRT64版GCC |

コマンドは、原則としてリポジトリルートで実行します。

```text
C:\Users\ユーザー名\...\CodingGame-Othello
```

## 3. Pythonの確認と導入

### 3.1 インストール確認

PowerShellで次を実行します。

```powershell
python --version
```

Python 3のバージョンが表示されれば追加作業は不要です。仮想環境やConda環境のPythonも使用できます。

### 3.2 Pythonが見つからない場合

`winget`で利用可能なPythonを確認します。

```powershell
winget search --id Python.Python
```

表示された安定版のIDを指定してインストールします。次はPython 3.13を導入する例です。

```powershell
winget install -e --id Python.Python.3.13
```

インストール後はPowerShellを開き直し、`python --version`を再実行します。

## 4. MSYS2の導入

### 4.1 MSYS2のインストール

PowerShellで次を実行します。

```powershell
winget install -e --id MSYS2.MSYS2
```

確認画面が表示された場合は、内容を確認して続行します。標準のインストール先は`C:\msys64`です。

### 4.2 MSYS2 UCRT64の起動

Windowsのスタートメニューから`MSYS2 UCRT64`を起動します。

`MSYS2 UCRT64`はPowerShellへ入力するコマンド名ではありません。PowerShellへそのまま入力すると、コマンドが見つからないエラーになります。

### 4.3 パッケージ情報と基本パッケージの更新

起動したMSYS2 UCRT64ターミナルで次を実行します。

```bash
pacman -Syu
```

更新途中でターミナルを閉じるよう案内された場合は、画面の指示に従って閉じます。その後、スタートメニューからMSYS2 UCRT64を再度起動し、もう一度更新します。

```bash
pacman -Syu
```

### 4.4 UCRT64版GCCのインストール

MSYS2 UCRT64ターミナルで次を実行します。

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc
```

インストール確認が表示された場合は、内容を確認して続行します。

## 5. GCCの確認

PowerShellから、GCC本体が存在することを確認します。

```powershell
Test-Path "C:\msys64\ucrt64\bin\g++.exe"
```

`True`が表示されたら、バージョンを確認します。

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" --version
```

対戦BATは標準パスにあるGCCを自動検出するため、通常はWindowsの環境変数`PATH`を変更する必要はありません。

## 6. PATHを手動設定する場合

MSYS2を標準パス以外へ導入した場合や、PowerShellから直接`g++`を使用したい場合は、現在のPowerShellセッションでMSYS2のパスを先頭へ追加します。

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

末尾ではなく先頭へ追加してください。Condaや別の開発環境が有効な状態で末尾へ追加すると、別環境のDLLやツールが優先され、`g++ --version`は成功しても実コンパイルに失敗する場合があります。

ユーザー環境変数へ永続的に追加した場合、変更は新しく開いたPowerShellから有効になります。既に開いているPowerShellには自動反映されません。

## 7. ローカル対戦の実行

### 7.1 BATの起動

PowerShellでリポジトリルートへ移動し、BATを起動します。

```powershell
cd "C:\Users\ユーザー名\...\CodingGame-Othello"
.\test\run_othello_matches.bat
```

### 7.2 新旧ファイルの入力

表示されたプロンプトへ、リポジトリルートからの相対パスを入力します。

```text
old_cpp_file: src\cpp\Othello_world_cup_ver_1.cpp
new_cpp_file: src\cpp\Othello_world_cup_ver_4.cpp
Change_Details: Compare ver1 and current ver4
Games: 100
```

`Change_Details` には新版だけに加えた変更内容、`Games` には対局数を入力します。入力ファイルがC++の場合は自動的にコンパイルされ、`.py` の場合はコンパイルせずにそのまま起動します。

```text
Compiling OLD bot with local metrics...
Compiling NEW bot with local metrics...
```

コンパイル後、指定した対局数の対戦と最終集計が自動的に実行されます。対局数が偶数なら、旧版と新版は黒と白をそれぞれ同数担当します。実行後はコンソール表示に加えて、`test\result\` 配下へMarkdownレポートも保存されます。

## 8. 手動コンパイルによる確認

BATでコンパイルエラーが発生した場合は、リポジトリルートで次のように直接コンパイルして診断を確認できます。

```powershell
New-Item -ItemType Directory -Force test\build | Out-Null
& "C:\msys64\ucrt64\bin\g++.exe" `
  -std=c++17 `
  -O2 `
  -DOTHELLO_ENABLE_METRICS `
  src\cpp\Othello_world_cup_ver_1.cpp `
  -o test\build\manual_test_bot.exe
```

終了コードは次で確認できます。

```powershell
$LASTEXITCODE
```

`0`ならコンパイル成功です。

## 9. トラブルシューティング

### 9.1 `pacman`が認識されない

`pacman`は通常のPowerShellコマンドではありません。スタートメニューからMSYS2 UCRT64を起動し、そのターミナル内で実行します。

### 9.2 `MSYS2 UCRT64`が認識されない

`MSYS2 UCRT64`はスタートメニューのショートカット名です。PowerShellへ文字列を入力して起動するものではありません。

### 9.3 `g++`が認識されない

次の直接指定で動作を確認します。

```powershell
& "C:\msys64\ucrt64\bin\g++.exe" --version
```

直接指定では動作する場合、PATHが現在のPowerShellへ反映されていません。ただし、標準パスにインストールされていれば対戦BATは直接検出できます。

### 9.4 `Compiling OLD bot...`の後に失敗する

次を確認します。

1. 入力した旧版ファイルが存在すること。
2. 拡張子が`.cpp`または`.py`であること。
3. `C:\msys64\ucrt64\bin\g++.exe`が存在すること。
4. 手動コンパイルで表示されるエラー内容を確認すること。

Conda環境を使用している場合でも、BATはMSYS2の標準パスをPATH先頭へ設定します。

### 9.5 `OLD source file not found`または`NEW source file not found`

入力パスは、現在のPowerShellの場所ではなくリポジトリルートを基準とする相対パスです。

```text
src\cpp\ファイル名.cpp
```

ファイル一覧は次で確認できます。

```powershell
Get-ChildItem src\cpp -File
```

### 9.6 `python was not found in PATH`

次を実行してPythonを確認します。

```powershell
python --version
Get-Command python
```

Pythonをインストールした直後の場合は、PowerShellを開き直します。

### 9.7 `Reason: TIMEOUT`が表示される

AIがCodinGame相当の制限時間内に着手を返せなかった状態です。最初の行動は2秒、2回目以降は150ミリ秒です。PCの負荷を下げて再実行し、繰り返し発生する場合はAIの探索時間設定を確認します。

### 9.8 `BookLastMove` や `MaxDepth` が `N/A` になる

そのAIがローカル計測用の標準エラー出力を出していない状態です。現行のC++版は対応していますが、旧版や独自Python版では `N/A` になる場合があります。勝敗集計自体には影響しません。

## 10. 環境構築完了条件

次をすべて満たせば環境構築は完了です。

1. `python --version`でPython 3が表示される。
2. `C:\msys64\ucrt64\bin\g++.exe`が存在する。
3. `g++.exe --version`でGCCのバージョンが表示される。
4. `run_othello_matches.bat`で旧版と新版を入力できる。
5. 両AIのコンパイルが成功する。
6. 指定した局数分の結果と`Final Summary`が表示される。
7. `test\result\` 配下へMarkdownレポートが保存される。
