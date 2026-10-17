# 注音 AI（ZhuyinLLM）Windows 版

Windows 10／11 的注音輸入法，打字時由本機 LLM（Qwen3-0.6B）即時修正錯字，也修英文拼字。按鍵照「微軟注音」，另外加上 AI 標記與跳轉。

> **目前狀態**：引擎（`core/`、`engine/`）與 Mac 版同一套演算法，在 macOS 上跑過完整測試（含模型），準確率與 Mac 版一致；整套 Windows 程式可用 MinGW 交叉編譯，GitHub Actions 會在 Windows 上用 MSVC 建置、跑測試、實際啟動引擎伺服器走具名管道，並用 regsvr32 註冊／移除文字服務。**還沒有在實體 Windows 電腦的 App 裡手動打字測試過**，第一次安裝請留意問題並回報。

## 架構

```
每個 App 的行程                                   每位使用者一個行程
┌──────────────────────────────┐   具名管道    ┌──────────────────────────────────┐
│ ZhuyinLLM-x64.dll / -x86.dll  │ ◄──────────► │ zhuyinllm-server.exe              │
│ TSF 文字服務（薄客戶端）       │ \\.\pipe\    │ 詞庫、lattice、英文修正、          │
│ 組字、標記、選字窗、提示框、    │ ZhuyinLLM-   │ llama.cpp（CPU, AVX2）+ Qwen3-0.6B │
│ 中／英圖示、英文改字           │ <使用者 SID>  │ 每個連線一個 Session（按鍵狀態機） │
└──────────────────────────────┘              └──────────────────────────────────┘
```

- 文字服務會被載入每個 App，所以它只負責轉送按鍵與顯示；詞庫和模型只在伺服器載入一次（約 400 MB 記憶體）。
- 32 位元 App 用 `ZhuyinLLM-x86.dll`，同樣連到 64 位元的伺服器。
- 管道只允許你自己、SYSTEM 與 App Container（Microsoft Store App）連線。
- 中／英、全形／半形是**同一個全域模式**：在一個視窗切換後，所有 App、所有視窗都一樣，重開機也保留。

## 安裝

1. 從 GitHub Actions 的 **ZhuyinLLM-windows** 成品（Artifacts）下載並解壓縮，或自己建置（見下方）。
2. 在解壓縮的資料夾執行（會要求系統管理員權限）：
   ```powershell
   powershell -ExecutionPolicy Bypass -File install.ps1
   ```
   - 檔案裝到 `C:\Program Files\ZhuyinLLM`（Store App 也讀得到）。
   - 下載模型（約 380 MB，固定版本並核對 SHA-256）。加 `-SkipModel` 只用詞庫、不用 AI。
   - 用 regsvr32 註冊 64 位元與 32 位元文字服務；登入時自動啟動引擎（HKCU Run）。
3. 到「設定 > 時間與語言 > 語言與地區 > 中文(台灣) > 語言選項 > 鍵盤 > 新增鍵盤」加入「注音 AI」（或安裝時加 `-AddKeyboard` 自動加入），再用 Win+Space 切換。

移除：`powershell -ExecutionPolicy Bypass -File "C:\Program Files\ZhuyinLLM\uninstall.ps1"`（加 `-Purge` 連設定與記住的英文字一起刪）。

## 按鍵

### 組字（同微軟注音）

| 按鍵 | 行為 |
|---|---|
| 注音鍵、6 3 4 7、空白 | 組字；聲調鍵或空白（一聲）完成一個音節 |
| Enter | 送出 |
| Esc | 清掉正在打的音節，再按一次清掉整段 |
| Backspace／Delete | 刪除 |
| ← → Home End | 在組字區移動游標（游標所在的字會反白，並顯示讀音提示框） |
| ↓ | 打開選字窗（游標前的字） |
| ↑（選字窗外） | 不開窗，直接把游標前的字換成下一個候選；連按繼續換 |
| 空白（已轉換後） | 打開選字窗 |
| Shift（單按） | 切換中／英（所有視窗共用） |
| Shift+Space | 切換全形／半形 |
| Shift+英文字母 | 輸入大寫英文 |
| Caps Lock 開啟時 | 字母直接輸入英文（大寫；加 Shift 為小寫） |
| Shift+， ． ／ ； 1 9 0 [ ] | ， 。 ？ ： ！ （ ） 『 』 |
| [ ] \ | 「 」 、 |
| Ctrl+符號鍵 | 對應的全形符號（例如 Ctrl+, → ，；可在選單關閉） |
| ` + 符號鍵 | 對應的全形符號 |
| ` U + 十六進位 + 空白 | 輸入該 Unicode 字元（例如 `U4e00 → 一） |

### 選字窗（同微軟注音）

| 按鍵 | 行為 |
|---|---|
| ↑ ↓ | 移動選取，組字區跟著預覽 |
| 空白、PageDown、→、= ] | 下一頁 |
| PageUp、←、- [ | 上一頁 |
| 1–9、Enter、滑鼠點選 | 選定 |
| Esc | 關閉並恢復原本的字（AI 標記保留） |

### AI 功能（本輸入法新增）

| 按鍵 | 行為 |
|---|---|
| Tab／Shift+Tab | 跳到下一個／上一個 AI 標記的字並打開選字窗：第一個是現在的字，接著是 AI 的建議，再來是詞庫候選。選字窗中按 Tab 跳到下一個標記 |
| Shift+Backspace | 把 AI 改過的字退回你實際打的注音，之後不再改 |
| Backspace（英文模式，緊接在自動修正後） | 還原成你打的字，並記住它以後不再改 |

標記的樣子：AI 改過的字是**橘色粗底線**，AI 覺得可能有錯的字是**紅色波浪線**，游標所在的字**反白**。不同 App 對顯示屬性的支援不同：大多數 App 會畫底線，有些 App 只畫單色底線。

和微軟注音不同的地方：Tab 用來跳 AI 標記（微軟注音的 Tab 是斷詞／展開選字表）；沒有聯想詞與表格式選字窗；` B（Big5 碼）輸入不支援。

### 工作列的「中／英」圖示

左鍵點一下切換中／英；右鍵選單可以切換英文模式、全形、AI 即時糾錯、注音打錯鍵容錯、單按 Shift 切換、Ctrl+符號鍵，並顯示 AI 模型是否已載入。

## 建置

需要 CMake 3.21 以上。引擎會用 FetchContent 下載並編譯 llama.cpp `v0.7.0`（與 Mac 版相同版本）。

**Windows（Visual Studio 2022）**

```powershell
cmake -S windows -B build-x64 -A x64
cmake --build build-x64 --config Release
cmake -S windows -B build-x86 -A Win32 -DZL_BUILD_ENGINE=OFF   # 32 位元文字服務
cmake --build build-x86 --config Release
```

**macOS／Linux 交叉編譯（MinGW-w64）**

```bash
brew install mingw-w64 cmake
cmake -S windows -B build-x64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 -j
cmake -S windows -B build-x86 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DZL_BUILD_ENGINE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86 -j
```

產出：`ZhuyinLLM-x64.dll`、`ZhuyinLLM-x86.dll`、`zhuyinllm-server.exe`（另有 `imecli`、`session_test`、`pipe_test`）。安裝資料夾還需要 `data\`（tsi.csv、en_freq.txt、en_known_words.txt、en_misspellings.txt）與 `installer\` 的兩個腳本。

**在 macOS 上測引擎**（用 Homebrew 的 llama.cpp，Metal）

```bash
cmake -S windows -B build-mac -DCMAKE_PREFIX_PATH=/opt/homebrew && cmake --build build-mac -j
build-mac/session_test data ~/path/to/model.gguf          # 按鍵／協定的端到端測試
build-mac/imecli eval data/tsi.csv model.gguf bench/zh_candidates.json --calibrate
build-mac/imecli en-eval data/tsi.csv model.gguf data/en_freq.txt bench/en_eval.json
```

`bench/en_eval.json` 由 `bench/scripts/fetch_en_data.sh` 與 `make_en_eval.py` 產生（來源資料不附在這個 repo 裡）。

## 協定（文字服務 ↔ 引擎）

每則訊息是 4 位元組長度（little endian）加 UTF-8 JSON。請求帶 `op` 與 `id`，回應原樣帶回 `id`：

- `key`：`{code, shift, ctrl, alt, caps, ctx?}` → `{eat, commit?, seq?, view?, actions?, flash?, mode, idle}`
  - `view`：`comp`（組字文字、游標、各段樣式：0 一般、1 AI 改過、2 可能有錯，+3 為游標所在）、`cand`（候選、選取、焦點位置）、`bubble`（提示框）。
  - `actions`：英文模式的 `fix`（立刻改字）、`watch`（答案還沒到，先追蹤這個字）、`revert`（還原）。
  - 上一個回應的 `idle` 為真時，文字服務會附上游標前最多 400 字（`ctx`）給 AI 當上文。
- `commit`、`cancel`、`select`、`lang`、`width`、`set`、`state`、`userword`、`hello`。

引擎主動推送：`{"ev":"view"}`（AI 重新排序後的組字）、`{"ev":"fix"}`（晚到的英文修正）、`{"ev":"mode"}`（任何 App 切換了中／英）。

## 檔案與紀錄

- 安裝：`C:\Program Files\ZhuyinLLM\`
- 設定與記住的英文字：`%LOCALAPPDATA%\ZhuyinLLM\settings.json`、`userwords.txt`
- 紀錄：`%LOCALAPPDATA%\ZhuyinLLM\server.log`（只有事件與延遲數字，不記錄打的文字）
