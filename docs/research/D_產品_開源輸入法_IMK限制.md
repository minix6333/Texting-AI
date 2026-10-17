# Topic D：既有產品、開源輸入法、macOS InputMethodKit (IMK) 工程限制

> 調查日期：2026-10-17（agent D）。所有連結皆為實際開啟或於搜尋結果中看到者；未能驗證者標「未能確認」。
> 「廠商宣稱」與「我的評估」分開標示。
> Card schema：name | link | 是什麼 | 跟我們的關係 | 關鍵數字/做法 | 侷限

---

## 進度紀錄（incremental）與索引

- [x] 建立筆記檔
- [x] 讀碼（shallow clone 於 `scratchpad/agentD_src/`）：azooKey-Desktop、AzooKeyKanaKanjiConverter、vChewing、McBopomofo、libchewing、Squirrel、hallelujahIM、fcitx5-macos、Mozc（sparse）、TabType、iTerm2（3 檔）、Chromium（3 檔）、AOSP LatinIME（1 檔）、多個 LLM-IME 專案（`agentD_src/llm_ime/`）
- [x] 一手 API 文件：macOS 27.0 SDK 的 `IMKInputSession.h`、`IMKInputController.h`、`IMKServer.h`
- [x] GitHub issues（API 讀全文）：azooKey #342/#347/#357/#359/#374/#379/#380；Squirrel #1040/#1140/#1154/#1162；vChewing #88/#599/#602/#616/#618/#620

**卡片索引**：論文卡 P1 Zenzai、P2 Pixel 端側文法｜商用 C1 Apple autocorrect、C2 Gboard、C3 Writing Tools、C4 Cotypist、C5 ATOK、C6 Apple 中文 IME、C7 Live Conversion、C8 Grammarly/LanguageTool（含端側 GEC）、C9 中國大陸 IME、C10 Windows/SwiftKey｜開源 O1 azooKey/Zenzai、O2 Squirrel、O3 hallelujahIM、O4 fcitx5-macos、O5 Mozc、O6 TabType、O7 AOSP LatinIME、O8 vChewing、O9 McBopomofo、O10 libchewing、O11+ LLM-IME 專案群｜章節：IMK 限制與對策、UX 模式整理、神經網路/LLM 輸入法前例與實測延遲、設計啟示

---

## 論文卡 P1：ニューラルかな漢字変換システム Zenzai（ANLP 2025）【關鍵前例】

- **出處**：三輪敬太（東京大學／Turing）、高橋直希（早稻田／CoeFont），言語処理学会 第31回年次大会（NLP2025）P1-19，2025-03，CC BY 4.0。
- **連結**：https://www.anlp.jp/proceedings/annual_meeting/2025/pdf_dir/P1-19.pdf （已下載並以 PDFKit 抽出全文閱讀）
- **問題**：Transformer 類神經かな漢字變換精度高但慢；IME 需要「本機、即時」。如何兼顧精度與速度、且保證輸出忠於使用者輸入（讀音）？
- **核心想法**：
  1. 把變換建模為條件式 LM（GPT-2 decoder-only）：序列 `[<boc>, 左文脈…, <boi>, 讀音…, <boo>, 輸出…, <eoo>]`（左文脈格式同 PinyinGPT-Concat）。貪婪解碼。
  2. **以傳統統計式變換器（AzooKeyKanaKanjiConverter，n-gram + 辭典 lattice）當 speculative decoding 的 draft model**：draft 一次提出整句候選 → target LM 一次 forward 平行驗證每個 token 是否為 argmax；若某 token 不是 argmax，則把「到該位置為止的前綴 + LM 的 argmax token」當作**制約 C** 回饋給 draft model 重新搜尋；直到全數通過（Algorithm 1）。
  3. 因為所有修正都必須由 draft（有辭典）實現 → 輸出一定對應到辭典條目，**可偵測並拒絕 LM 不忠於讀音的生成**（防幻覺）。
  4. 迴圈次數可設上限 L（推論回數制約），以精度換速度。
- **結果**（Apple M2 Pro Mac mini, macOS 14.6.1, 32GB；llama.cpp（自改 fork）用內建 GPU；全條件用 KV cache；評估集：日文 Wikipedia 入力誤り資料集中「かな漢字變換由來」錯誤 200 筆）：
  - 模型：GPT-2 **medium 310M / small 91M / xsmall 26M**；訓練資料約 **190M 對**（llm-jp-corpus-v3 + Wikipedia，MeCab+NEologd 推讀音）；字元級 BPE（byte-fallback），詞彙 6000；llm.c 單張 H100 訓練。
  - Table 1 Acc@1 / CER：Google 日本語入力 54.0 / 6.7；draft model（AzooKey 統計式）44.5 / 7.6；GPT-4o 56.0 / 7.7；GPT-4o mini 19.0 / 21.8；**Zenzai xsmall 66.5 / 4.6、small 80.0 / 2.6、medium 86.5 / 1.7**。
  - Table 2（**每輸入 1 字元平均 ms**）：
    - xsmall：無 SD 3.5ms（Acc 66.5）；+SD L=∞ 6.1ms；L=1 3.7ms（61.5）。→ 模型夠小時 SD 反而變慢（draft 本身約 1.3 ms/字）。
    - small：無 SD 9.8ms（80.5）；+SD L=∞ 6.0ms（80.0）；L=1 4.2ms（69.5）。
    - medium：無 SD **26.4ms**（86.5）；+SD L=∞ **6.9ms**（86.5）；L=3 5.8ms（83.0）；L=1 4.8ms（70.5）。
  - L=1 時 small 與 medium 差距很小 → draft 的輸出中「≤1 次修正即可正確」者最多約 70%；draft 越好，越小的 L 就夠。
  - 失敗型：draft 辭典缺詞（例「夜駆け」）時，SD 會拒絕 LM 的正確答案（過度忠實）。
- **為什麼重要**：這是目前最接近「小型本機 LM 即時介入 IME 主路徑」且有公開數字的前例；證明 26M–310M 級 GPT-2 在 Apple Silicon GPU 上可做到每字元數 ms 級。
- **侷限**：日文かな漢字變換 ≠ 錯字更正（輸入讀音本身被視為正確）；時間是「每輸入字元平均」，不是 per-keystroke P99；只在 M2 Pro 測；評估集僅 200 筆；GPT-4o 為 zero-shot 比較。
- **對我們輸入法的啟示**：
  - 注音→漢字本質上同構（讀音→漢字），可直接移植「**傳統 lattice（vChewing Megrez/McBopomofo Gramambular）當 draft + 小 LM 驗證/制約**」架構；LM 只負責「挑選與驗證」，辭典負責「保證忠實」，天然抑制過度更正。
  - 錯字（注音打錯鍵）情境需擴充：讓 draft 側產生「讀音鄰近變體」（鍵盤鄰鍵、聲韻混淆）作為候選格，LM 只在其中選，維持「可解釋」與「可拒絕」。
  - 設 L（推論次數上限）作為延遲預算旋鈕；模型 ≤100M 時考慮直接 greedy（不 SD）。

---

## 卡片 C1：Apple 鍵盤內建 transformer 自動校正 / inline predictive text（iOS 17 / macOS Sonoma 起）

- **name**：Apple autocorrect + inline predictive text（AppleSpell 內的 "unilm" 模型）
- **link**：Jack Cook 逆向分析 https://jackcook.com/2023/09/08/predictive-text.html ；MacStories iOS 17 review https://macstories.net/stories/ios-and-ipados-17-the-macstories-review/9 ；新聞（WWDC23）https://www.npr.org/2023/06/07/1180791069/apple-autocorrect-ducking
- **是什麼**：Apple 2023 起以「transformer language model」做自動校正與 inline 預測（英/法/西文鍵盤先上）；Apple 宣稱全部 on-device、會個人化（**廠商宣稱**）。
- **跟我們的關係**：同為「極小型本機 LM 在打字迴圈中即時運作」的系統級前例，且是使用者最熟悉的 UX 基準（我們的 IME 會與它並存/競爭）。
- **關鍵數字/做法**：
  - Jack Cook 逆向（第三方、非官方）：模型位於 `/System/Library/LinguisticData/RequiredAssets_en.bundle/AssetData/en.lm/unilm.bundle`；GPT-2 風格 **6 層 decoder、hidden 512、約 34M 參數、詞彙約 15,000（含 100 個 emoji）**；以 Espresso（CoreML 內部推論引擎）執行，存在 `unilm_joint_cpu` 與 `unilm_joint_ane` 兩組 shape 檔 → 暗示 CPU 與 Neural Engine 兩條路徑（**ANE 實際使用未能確認**）；由 `AppleSpell` 服務（XPC）提供。多數只預測一個詞，偶爾 2 詞。
  - UX（MacStories）：自動校正後的詞**短暫以淡藍底線標示**，點一下可看到原字並還原或選其他建議；**使用者還原後，下次同樣打法不再自動改**，但改以紅底線標示為疑似錯字（= 從 revert 學習）。inline 預測以灰字顯示，**按空白鍵接受**。
  - 限制：自訂 text view（如以 web 技術構建的 Obsidian）用不到新系統（reviewer 稱這類 app "feel broken"）→ 顯示「系統級 inline 功能高度依賴 native text view」。
- **侷限**：模型細節為第三方逆向推測；Apple 未公布延遲數字；中文（注音）無此 transformer 自動校正的公開證據（未能確認）。

## 卡片 C2：Gboard（Proofread / 自動校正還原 / 注音）

- **name**：Gboard Proofread、Autocorrect revert、Gboard 注音
- **link**：Proofread 論文 https://arxiv.org/abs/2406.04523 （ACL 2024 demo：https://aclanthology.org/2024.acl-demos.27 ）；還原功能報導 https://helentech.jp/news-75816/ ；Gboard 注音上線報導 https://www.inside.com.tw/article/12149-google-gboard
- **是什麼**：(a) Proofread：一鍵句子/段落級修正，**伺服器端** tuned PaLM2-XS，Pixel 8 上線，TPU v5 服務；(b) 2025-10 報導：自動修正過的字會被 **highlight**，點一下候選列出現原字、一鍵還原（逐字還原，不必 backspace 刪掉周圍正確文字）；(c) 2018 年起 Android 支援注音，Google 稱以 ML 模型做預測與自動校正。
- **跟我們的關係**：(a) 證明「整句 LLM 校正」目前主流是**按需（one tap）而非每鍵自動**，且當時仍需雲端；(b) highlight + 一鍵還原是我們「自動替換後可撤銷」的直接 UX 範本；(c) 注音自動校正已有大廠先例，但細節不公開。
- **關鍵數字/做法**：Proofread：合成資料管線 + SFT（先 rewrite 再 proofread 的序列式微調最好）+ RL（global/direct reward）；golden set **good ratio 85.56%**；延遲優化：quantization、bucket inference、text segmentation、speculative decoding（論文頁未給延遲絕對值；有二手摘要稱 median latency 降 39.4%，**未能於原文確認**）。
- **侷限**：Proofread 為雲端；Gboard 注音校正準確率/演算法無公開資料（未能確認）；還原功能報導中作者自己在日文/英文鍵盤未能重現（漸進推出）。

## 卡片 C3：Apple Writing Tools – Proofread（Apple Intelligence）

- **name**：Writing Tools（Proofread / Rewrite）
- **link**：MacRumors 指南 https://www.macrumors.com/guide/apple-intelligence-writing-tools/ （已開啟；2024-09，macOS 15.1 beta 時代：Proofread 回傳整段改寫版，可複製/取代/分享，稱為「an all-or-nothing correction」，長文難以看出改了哪裡；系統級「anywhere there's text」；當時 macOS 上不如 iOS 穩定）；Intego 評測 https://www.intego.com/mac-security-blog/how-to-use-apple-intelligence-writing-tools/amp/ （僅搜尋結果摘要）
- **是什麼**：選取文字後由系統 LLM 校對/改寫；macOS 15.1 時代評測指出 Proofread **只能整段接受或拒絕，不能逐項接受**（評測者抱怨）。
- **跟我們的關係**：屬「事後、按需、整段」的校對模式；與我們「打字當下、逐字」的模式互補而非重疊。可借鏡：顯示差異、可撤銷。
- **關鍵數字/做法**：無公開延遲數字；是否全部 on-device 或用 Private Cloud Compute：**未能確認**。
- **侷限**：資料多為評測文章；macOS 27 是否改為逐項接受：未能確認。

## 補充：Apple Foundation Models 在「背景程序」的 rate limit（對 IME 很重要）

- 連結：https://developer.apple.com/forums/thread/789788 ；文件 https://developer.apple.com/documentation/foundationmodels/languagemodelerror/ratelimited(_:).md
- Apple Frameworks Engineer 原話（節錄）："Rate limiting applies when you device is on battery AND when your process is running in the background."；並建議背景使用時不要 streaming、改用 `respond`。
- **我的評估**：IME 程序是 LSBackgroundOnly 的背景程序，MacBook Air 常用電池 → 若想用系統 Foundation Models（第三方文章稱約 3B 參數，Apple 文件未在本次確認）做逐鍵校正，**極可能被 rate limit**；且 3B 模型也太大、無法控制延遲。→ 逐鍵路徑應使用自帶的小模型（llama.cpp / MLX / Core ML），Foundation Models 最多用於低頻、按需的整句潤飾。（此推論未實測）

## 卡片 C4：Cotypist（本機 LLM 系統級 inline 補全，Accessibility 路線）

- **name**：Cotypist
- **link**：官網 https://cotypist.app/ ；相容性表 https://cotypist.app/compatibility ；Daring Fireball 短評 https://daringfireball.net/linked/2026/06/18/cotypist
- **是什麼**：macOS 上「任何 app 都有 ghost-text 自動補全」的工具；不是輸入法，而是透過 **macOS Accessibility API** 讀游標附近文字並在宿主 app 內以「目前字型」畫出灰色建議；Tab 接受（可逐字），繼續打字即放棄。官網亦稱 typo 修正以 inline 方式呈現（"no pop-ups, no red squiggles"）。
- **跟我們的關係**：與我們最接近的「系統級、本機模型、即時」商業產品，但走 AX 路線而非 IMK；它的相容性表是**「AX 路線在哪些 app 會壞」的最佳實測清單**，可直接對照 IMK 路線的優劣。
- **關鍵數字/做法**（官網，**廠商宣稱**）：Apple Silicon、macOS 14+；建議 M1 Pro / M2 以上 + **16GB**；執行時約 **1–2.5GB** 記憶體；「almost instantly」「without perceptible delay」（無量化數字）；macOS 會擋它讀密碼欄位。模型：官網未公開；第三方說法分歧（Qwen2.5 1.5B / ~3GB Gemma via llama.cpp）→ **未能確認**。價格：免費每天 100 個完成字；Plus/Pro（Gruber：US$6 / $9 每月）。
- **相容性（官網）**：可用：Safari/Chrome/Brave/Edge/Firefox、Mail、Outlook、Word、TextEdit、Pages、Notes、Notion、Obsidian、Messages、Teams、WhatsApp、Terminal.app、iTerm（含 Claude Code 等 CLI prompt）。需設定：Google Docs（要開「螢幕閱讀器支援」）、Arc/Dia（需 `chrome://accessibility` 開 Text Metrics 或以 `--force-renderer-accessibility=complete` 啟動，否則只能用「mirror window」浮窗）。部分：Slack。只在側欄 chat：VS Code/Cursor/Windsurf（主編輯器需另開選項）。不支援：Zed、Sublime Text、Ghostty、Warp、Kitty、OneNote、Google Slides（canvas）、VM/遠端桌面。原因：這些 app 不把文字欄位暴露給 AX。
- **侷限**：AX 路線依賴宿主 app 的 accessibility 實作品質；Chromium 系需特殊旗標；無法在 canvas/自繪文字 app 運作；Gruber 本人覺得「a never-ending distraction」→ **主動建議的打擾成本是真實 UX 風險**。
- **我的評估**：IMK 路線在「我們自己就是組字的那一方」時（注音 marked text 階段）不受宿主 AX 品質影響；但若要「改已經送出（commit）的文字」，IMK 也得依賴宿主對 `replacementRange` / `attributedSubstring` 的支援（見 IMK 限制章節），兩條路線在「改舊字」上都脆弱。

## 卡片 O1：azooKey on macOS（azooKey-Desktop）+ AzooKeyKanaKanjiConverter / Zenzai 實作【最重要前例】

- **name**：azooKey on macOS（日文 IMK 輸入法）＋ AzooKeyKanaKanjiConverter（變換引擎，含 Zenzai）
- **link**：https://github.com/azooKey/azooKey-Desktop （HEAD bd90b7b，2026-10 讀碼）；https://github.com/azooKey/AzooKeyKanaKanjiConverter （HEAD d59a28e）；作者解說 https://zenn.dev/azookey/articles/ea15bacf81521e ；IPA 未踏 2024 https://www.ipa.go.jp/jinzai/mitou/koubo/career/2024/2024-supercreator-10.html
- **是什麼**：Swift 實作的 macOS 日文輸入法，傳統 n-gram 辭典 lattice + GPT-2 系「zenz」小模型（llama.cpp, Metal GPU）做神經かな漢字變換；預設開啟 Live Conversion；MIT 授權（code）；tokenizer 來自 ku-nlp（CC BY-SA 4.0）；zenz 模型授權：repo 中未找到（未能確認）。
- **跟我們的關係**：幾乎就是我們要做的系統的「日文版」——IMK + 小 LM + 即時 + 本機 + 個人化，且已實際上線，原始碼完整可讀。
- **關鍵數字/做法（皆讀碼確認）**：
  1. **演算法**（`Sources/KanaKanjiConverterModule/ConversionAlgorithms/Zenzai/zenzai.swift`、`ZenzCandidateEvaluator.swift`）：lattice 產生 N-best（無制約 N=2）→ 取最佳候選 → 把 prompt+候選 token **一次 `llama_decode`** 驗證；逐 token 比對 **argmax（無機率門檻）**；不符則回傳「前綴制約 = 已通過 token + LM argmax token」（UTF-8 byte prefix）→ lattice 在制約下重搜；同制約重覆兩次則忽略學習/使用者辭典重試一次再放棄；**LM 自己生成的文字永遠不直接輸出**，最終結果必來自辭典 lattice。使用者學習過的 token 允許 `logit + log(learningPriority) > maxLogit` 時保留（個人化讓步）。
  2. **推論次數上限** `ZenzaiInferenceLimit` 預設 **5**（UI 1–50）；文件 `Docs/zenzai.md`：「通常 1 就夠，求精度約 5」；記憶體約 **150MB**；建議 M1 以上、用 GPU。
  3. **Prompt（zenz v3）**：`[辞書:語(よみ)…][ profile 末25字][ topic][ style][ preference][ 左文脈末40字][ 右文脈前40字]<片假名讀音><候選>`（以 Private Use Area 字元當特殊標記；空白→U+3000、移除換行）。
  4. **跨鍵復用**：`ZenzaiCache` 以上一鍵被 LM 接受的候選中「讀音仍為前綴」的部分當下一鍵的起始制約；llama.cpp 端找與前次的**共同 token 前綴**，`llama_kv_cache_seq_rm` 刪尾巴只 decode 新增部分（KV cache 複用）；羅馬字尾未定（如尾端 "k"）時**延後評估**；多層 LRU 快取（evaluation 256 / resolved 64 / draft 128 / prompt tokens 128）。
  5. **llama.cpp 設定**（`ZenzContext.swift`）：`n_ctx=512`、`n_batch=512`、`n_ubatch=64`、`flash_attn=true`、`use_mmap=true`、threads = P-core 數；以預建 xcframework 連結並**刻意釘在 b4846**：`Package.swift` 註解記載 b9637 在 M2 Pro CPU 路徑可把穩態延遲從 **約 10.8 → 7.2 ms/request**，但多配置 **63.77 MiB** CPU_REPACK buffer、iOS 退化、且關閉時觸發 Metal residency-set assertion；`Zenz.swift` 註記 KV cache 約 **18 MiB**。桌面版載入 `ggml-model-Q5_K_M.gguf`（**Q5_K_M**），模型為 zenz-v3.x-small（HF `Miwa-Keita/zenz-v3.2-small-gguf` 子模組；tokenizer 設定 12 層/768 維/詞彙 6000）。
  6. **程序架構**：**推論不在 IME 程序內**——`Core/Sources/ConverterServer/main.swift` 是獨立執行檔，以 **XPC Mach service `dev.ensan.inputmethod.azooKeyMac.ConverterServer`** 對外，由 postinstall 寫入 `/Library/LaunchAgents/…plist` 並 `launchctl bootstrap gui/<uid>`（KeepAlive/RunAtLoad）。IME 本體 **App Sandbox 開啟**，entitlements 用 `temporary-exception.mach-register.global-name = <bundleID>_Connection`（IMK 連線名）與 `mach-lookup` 指向 ConverterServer；ConverterServer 僅有 app-group（不在 sandbox）。（拆程序的官方理由：repo 文件中未找到，**未能確認**；我推測是隔離 Metal/llama.cpp 記憶體與崩潰、避免拖垮 IMK 連線。）
  7. **鍵盤事件流**：`handle(_:client:)` 只處理 keyDown；Command 組合鍵放行；**若仍有未回覆事件則一律吞鍵並排隊**（保序）；否則非同步送 server 並立即回 `true`；回覆到達後才 `insertText(…, replacementRange: NSRange(NSNotFound,0))` / `setMarkedText`。`OrderedAsyncCommandQueue`：一次只一個 in-flight、`commandTimeout = 1s`、key 事件重試不丟棄、重連 backoff 0.2s；server 端以 event ID 去重、拒絕亂序；**不取消、不合併過期請求**（每鍵都序列轉換）。
  8. **讀周邊文字**：`client.string(from:actualRange:)`，以 `markedRange()`→`selectedRange()` 為錨點取左右文脈，傳輸上限 200 字、變換只用左側最後一行 30 字、AI 建議用 100 字；右文脈用 `client.length()` 界定。
  9. **Chromium 死結避讓**（Chromium issue 503787240）：Chromium 系 app 在 JS 編譯中被 activate 時，同步呼叫 `client.attributes(forCharacterIndex:…)` 會 **deadlock** → 初始化與 `activateServer` 期間不向 client 查詢任何東西、先把候選窗關掉。選取文字替換：先試 `insertText(_:replacementRange:)`，失敗則退回**剪貼簿 + 合成 Cmd+V**。
  10. **候選窗**：不用 IMKCandidates；自繪 borderless `NSWindow`（level `.popUpMenu`）+ `NSTableView`，位置用 `attributes(forCharacterIndex:0, lineHeightRectangle:)`。
  11. **錯字更正**：`ZenzaiTypoCandidateGenerator.swift`（實驗）＝對輸入鍵序做 **beam search**，分數 = LM 對讀音的 log-prob − 通道成本；通道成本：替換 α=2.0×**Mac QWERTY 鍵距**（1.65 鍵內視為鄰鍵）、漏鍵 β=3.0、相鄰對調 γ=2.0；beam 32 / topK 64 / nBest 5；**原輸入永遠當 baseline 計分**；每鍵從 checkpoint 續算。桌面版實際出貨：**僅 debug 開關、在按 backspace 時觸發**，用下載的 5-gram LM（非 zenz），beam 16/topK 32/nBest 3，**以「預測」形式呈現、按 Tab 才接受**（不自動替換）。
  12. **個人化**：每 token logit 加 `alpha*(log p_personal − log p_base)`，兩個 5-gram Kneser-Ney（d=0.75）；`ZenzaiPersonalizationLevel` off/soft/normal/hard = α 0/0.5/1.0/1.5；個人模型存 `<AppGroup>/Library/Application Support/p13n_v1/lm_*.marisa`；`ZenzaiProfile` 自由文字。
  13. **雲端/系統 LLM 只做按需功能**：「いい感じ変換」可選 OpenAI 相容端點（預設 gpt-4o-mini、60s timeout、key 存 Keychain）或 Apple Foundation Models（macOS 26+）→ 回 3–5 個候選；**不在逐鍵路徑**。
- **侷限**：作者自評仍 alpha；早期 zenn 文章稱約 40ms/操作在 M2 Pro「稍重」，主觀 20–30ms 才算順、40ms 是 live conversion 上限；過度忠實於辭典（缺詞時拒絕 LM 正解）；每鍵不取消過期請求→快打時可能排隊；錯字更正尚未正式出貨。

### O1 補充：azooKey「把變換搬到 XPC 程序」後踩到的真實坑（2026 年 GitHub issues/PR，經 GitHub API 讀取全文）

> 這組 issue 是**本調查最有價值的工程教訓**：同樣是 M 系列 Mac、同樣是 llama.cpp 小模型，把推論移出 IME 程序後，問題從「算得慢」變成「IMK 同步語意被破壞」。

- **#357（2026-09-01，open）終端機第一個字重複**：https://github.com/azooKey/azooKey-Desktop/issues/357
  - 機制：`handle` 回 `true`（我吃了這個鍵），但 marked text 要等 ConverterServer **30–110ms** 後才設定；iTerm2 在 `handle` 回來的當下檢查「是否已有 marked text」，沒有就把原始鍵送給 shell → 出現 `bb`。NSTextView 系不會（macOS 自己處理）。v0.1.4（同程序同步）不會發生。
  - 教訓：**對 iTerm2 / Ghostty 等終端機，必須在 `handle` 回傳之前同步放好 marked text 或 insertText**。
- **#379（2026-10-04，merged）修正**：https://github.com/azooKey/azooKey-Desktop/pull/379 — 在送 server 前，先把按下的字元當「暫定 marked text」**同步**設定，回覆到時再替換；無回覆則清掉。
- **#374（2026-09-06，open PR）把 XPC 鍵處理改回同步**：https://github.com/azooKey/azooKey-Desktop/pull/374 — 等 server 回覆、反映 commit/marked text 後才回傳 `handle` 的消費判定；同步 RPC 逾時 30 秒、非同步 1 秒；失敗或 >200ms 的鍵處理記 log（不含文字內容）；明說「Server 停止時無法繼續日文輸入」。
- **#347（2026-08-03，open）ConverterServer 不釋放 session → 模型累積 → 輸入卡死**：https://github.com/azooKey/azooKey-Desktop/issues/347 — 每個 session 都載入一份 GGUF（70.3MB），3 天 23 小時後同一檔案被 mmap **224 次（≈15.7GB）**，大量 swap，每次變換都 page-in → 數秒卡頓；閒置時每分鐘仍新增 6 個模型。測試機 M3 Pro、macOS 26.5.2。
  - 教訓：**模型/上下文必須是全程序單例**，session 只能持有輕量狀態；LaunchAgent `KeepAlive=true` 會讓洩漏長期累積。
- **#380（2026-10-10，open）輸入中 IME 凍結數秒**：https://github.com/azooKey/azooKey-Desktop/issues/380 — 原因 (1) 每鍵重讀自訂輸入表；(2) 變換 server 存取 IME 的 **App Group 共享資料未被 macOS 授權，開學習辭典時等權限約 6 秒後逾時**（修正 PR #382：兩個程序都嵌入 provisioning profile）；(3) 向宿主 app 查詢預測候選位置（`attributes(forCharacterIndex:…)` 類）**偶發 2–3 秒無回應**（原因未明）。回報者用 **MacBook Air M4** 實測兩個 PR 後凍結明顯減少。
- 其他：#342 server 讀不到 resource bundle 崩潰；#359 分離後自訂輸入方式設定失效。
- **對我們的直接結論**：(1) IMK 程序內必須保留一條**不依賴模型的同步快速路徑**（注音組字、marked text 更新），LLM 結果只能「事後增強」；(2) 若拆 helper/XPC，要處理 App Group 簽章/provisioning、session 生命週期、單例模型、逾時與降級；(3) 對宿主的同步查詢（位置、文脈）要有逾時心態，避免在 activate 期間呼叫。

---

## 卡片 O2：Squirrel 鼠鬚管（Rime 的 macOS 前端）

- **name**：Squirrel（rime/squirrel，HEAD a685441，GPLv3）
- **link**：https://github.com/rime/squirrel
- **是什麼**：Rime（librime，C++）引擎的 macOS IMK 前端（現為 Swift）；支援 librime-lua / octagram / predict 外掛。
- **跟我們的關係**：成熟 IMK 前端的「各 app 相容性設定」與安裝/簽章流程範本；Lua 外掛 + `_refresh_ui` 是「非同步結果回填 UI」的現成機制。
- **關鍵數字/做法（讀碼）**：
  - `setMarkedText(attr, selectionRange: caret, replacementRange: NSRange(NSNotFound,0))`；commit 用 `insertText(_, replacementRange: .empty)`；樣式 `kTSMHiliteConvertedText` / `kTSMHiliteSelectedRawText`。
  - **非 inline 模式**把 marked text 設成單一全形空白 U+3000：註解「避免 iTerm2 回顯原始 preedit；半形佔位會讓中文組字基線不穩」。
  - `force_marked_text_for_direct_commit`：註解「有些 NSTextInputClient 實作要求 insertText 前先有一次 setMarkedText」（預設給 Alacritty，issue #741）。
  - 預設 `app_options`（squirrel.yaml）：Spotlight/Alfred/Xcode/VS Code/Sublime 等預設英文；**Terminal、iTerm2、Emacs：ascii_mode + no_inline**；MacVim 加 vim_mode；**Chrome、Edge、Telegram 強制 inline**（changelog：「force inline in Chrome to work around bksp」）。
  - **macOS 26：別的程序用 `TISSelectInputSource` 切換輸入法時，系統不再呼叫 `deactivateServer`（issue #1140）** → Squirrel 自己監聽 `kTISNotifySelectedKeyboardInputSourceChanged` 補呼叫。
  - 候選窗：自繪 `NSPanel`（nonactivating、`CGShieldingWindowLevel()`、TextKit 2；macOS 26 用 `NSGlassEffectView`），位置用 `attributes(forCharacterIndex:0, lineHeightRectangle:)`。
  - Info.plist：`InputMethodConnectionName = Squirrel_Connection`、`InputMethodServerControllerClass`/`InputMethodServerDelegateClass`、`LSUIElement=true`、`TICapsLockLanguageSwitchCapable=true`、`tsInputModeListKey`（Hans/Hant）。
  - 安裝：`pkgbuild --install-location "/Library/Input Methods"`，postinstall：killall → `TISRegisterInputSource` → 預建資料 → `TISEnableInputSource` → `TISSelectInputSource`；安裝後要求登出。
  - 簽章：entitlements `app-sandbox=false`、`cs.disable-library-validation=true`（載入外掛 dylib）、`network.client=true`；`codesign --options runtime --timestamp` → `productsign` → `notarytool submit --wait` → `stapler staple`。
  - 崩潰保護：上次啟動 <2 秒內又啟動則跳過 Rime 初始化（防設定錯誤造成 crash loop）。
  - librime 1.16 changelog：拼寫規則新增 `derive/X/Y/correction`（容錯規則）。
- **侷限**：GPL；Rime 無內建 LLM；deploy 在主執行緒同步執行（大詞庫時會卡）。

## 卡片 O3：Hallelujah IM（macOS 英文輸入法 + 拼字建議）

- **name**：hallelujahIM（dongyuwei，HEAD 5740287，GPLv3，Objective-C++）
- **link**：https://github.com/dongyuwei/hallelujahIM
- **是什麼**：把英文也當成「組字」的 IMK 輸入法：打字時單字保持 marked text（`kTSMHiliteSelectedRawText` 底線），空白鍵 commit（+空格）、Enter commit 不加空白、數字選候選、標點 commit+標點、Esc 清空。
- **跟我們的關係**：**英文以 marked text 呈現的現成先例**——證明可在 IMK 內對英文做「commit 前修正」，不需動到已送出的文字。
- **關鍵數字/做法**：候選順序：text expander → SQLite 前綴補全（依 Google 1-gram 頻率，LIMIT 50）→ 無前綴命中才用 `NSSpellChecker guessesForWordRange`（≤4）+ Phonex 語音碼（edit distance ≤3，≤4）→ 拼音轉英文（cedict）；**原始輸入永遠排第一，永不自動更正**。詞庫 SQLite 23.6MB（140,402 詞）。候選窗自繪 NSPanel；`attributes(forCharacterIndex:0…)` 回 `NSZeroRect` 時退回滑鼠位置。macOS 26 註記：`imklaunchagent` 會快取 `InputMethodConnectionName`，安裝腳本需 kill `imklaunchagent`/`TextInputSwitcher`/`TextInputMenuAgent`；未簽章未公證，README 教使用者 `xattr -d com.apple.quarantine`。
- **侷限**：英文永遠在 marked text 會改變使用者習慣（每字都有底線、需 commit 鍵）；不做上下文 LM；GPL。

## 卡片 O4：fcitx5-macos

- **name**：fcitx5-macos（fcitx-contrib，HEAD a5cb40f；repo GPLv3、fcitx5 core LGPL-2.1+）
- **link**：https://github.com/fcitx-contrib/fcitx5-macos
- **是什麼**：把 Linux 的 fcitx5 核心（C++）整個跑在 IMK 程序內（獨立 `std::thread` 事件迴圈），Swift ↔ C++ interop；外掛含 chewing、zhuyin、rime、lua、hallelujah；候選窗為 WKWebView（fcitx5-webview）。
- **跟我們的關係**：記錄了**最多「各 app 對 IMK 的怪癖」註解**的開源專案，是我們相容性測試清單的來源。
- **關鍵數字/做法（讀碼，原文註解）**：
  - 「無法保證 app 對視覺上取得焦點的 client 呼叫 activateServer… IMK being IMK」→ 每次 `handle` 都重設 controller；Chrome 某情境 `activateServer` 永遠不被呼叫。
  - **Dummy preedit hack**（預設 U+200B）：「沒有 client preedit 時，Terminal 的 Backspace 會繞過 IM；iTerm、JetBrains、VSCode 終端機每個鍵既被 IM 處理又被送到 client；Chrome 網址列/VSCode 編輯器 Backspace 被處理兩次；Spotlight 的 ↓ 被吞」。SwiftUI 欄位與 JetBrains 改用一般空白（JetBrains 會把 ZWSP 畫成 "ZWSP"）；MacVim、Mail、密碼類 app 跳過；切換 IM 時不設（會清掉 LibreOffice 儲存格）。
  - Word：「不用 dummy preedit 取代選取文字時，Esc 會繞過 IM」；VSCode：在 `commitComposition` 移除 preedit 會讓「下一個 Backspace 在 VSCode 送出 BS 字元」。
  - 游標位置：`attributes(forCharacterIndex: n)` 傳 n 會得到 (0,0)，需退一字再加字寬；「Terminal 回報正確字型，其他多半只回 Helvetica 12pt」；零矩形時沿用上次座標（避免跳到原點）。
  - 周邊文字讀取限 ±1024 UTF-16 單位（Terminal 讀太多很慢）。
  - Secure input：`IsSecureEventInputEnabled()` + IORegistry `kCGSSessionSecureInputPID` 判斷是哪個程序開著 secure input（忽略殘留者）。
  - 安裝：`/Library/Input Methods/Fcitx5.app`，首次安裝需登出；更新腳本 ad-hoc 簽章（`codesign --sign -`）並移除 quarantine。
- **侷限**：GPL；WKWebView 候選窗較重；ad-hoc 簽章路線不適合一般使用者散佈。

## 卡片 O5：Mozc（Google 日本語入力 OSS）— typing correction 與 macOS 架構

- **name**：Mozc
- **link**：https://github.com/google/mozc ；mac 設計文件 https://github.com/google/mozc/blob/master/docs/design_doc/mac_internal.md
- **是什麼**：Google 日文輸入法開源版；converter 以獨立 server 程序執行（mac 文件只描述 IME 端）。
- **跟我們的關係**：(1) **輸入層 typing correction** 是大廠在 lattice IME 內做「打錯鍵」更正的實作；(2) 早期 macOS IMK 怪癖紀錄。
- **關鍵數字/做法（讀碼，sparse clone）**：`src/composer/query.h` 定義 `TypeCorrectedQuery`：類型 `CORRECTION`（一般打錯更正）/`COMPLETION`/`KANA_MODIFIER_INSENTIVE_ONLY`，帶 `score`（相對「原樣輸入」的分數差，用於**決定是否觸發**）與 `bias`（用於計算更正候選的懲罰/加分）；`src/prediction/dictionary_predictor.cc` 以 `request.config().use_typing_correction()` 開關，並把結果標上 `Attribute::TYPING_CORRECTION`（候選可被 UI 標示為「更正」）。mac 文件：Firefox 的 `attributedSubstringFromRange` 不回東西、無 IME 端解法（2009 年紀錄）；login window（`com.apple.securityagent`）中**不應觸發網路**；位置用 `attributesForCharacterIndex:lineHeightRectangle:`。
- **侷限**：typing correction 模型細節（訓練/鍵距）未在 sparse clone 內追完（未能確認）；主要用於行動版。

## 卡片 C5：ATOK ディープコレクト（深度學習打錯鍵修復）

- **name**：ATOK（JustSystems）入力支援機能／ATOK ディープコレクト
- **link**：PC Watch 2017-12 https://pc.watch.impress.co.jp/docs/news/1095152.html ；Mac 版報導 https://www.macotakara.jp/software/entry-35123.html
- **是什麼**：在**輸入（羅馬字）階段**以深度學習偵測「日文中不自然的鍵序」（顛倒、多打子音、鄰鍵）並自動重組，例如 shourahia →「将来は」。
- **跟我們的關係**：最直接的商業「IME 內自動修正打錯鍵」UX 前例。
- **關鍵數字/做法（廠商宣稱）**：修復率較前版 **+35%**；**自動修復，Shift+BackSpace 取消修復**；**若使用者確定了取消後的原樣輸入，之後不再對該輸入自動修復**（從 revert 學習）；限制：以既有日文訓練，對怪讀音外來語與新詞無效。Mac 版 2018-06（Tech Ver.31）同樣宣稱導入。
- **侷限**：模型/延遲未公開；是否所有修復都自動或有門檻：未能確認。

## 卡片 C6：macOS 內建中文輸入法（注音 vs 拼音的「自動校正」落差）

- **name**：Apple Chinese Input Method（Zhuyin – Traditional / Pinyin）
- **link**：https://support.apple.com/guide/chinese-input-method/cim21aa5fa50/mac
- **是什麼**：macOS 內建中文輸入來源設定。
- **跟我們的關係**：直接界定**市場缺口**：Apple 文件中 **Pinyin（簡/繁）有「Correct spelling：Automatically correct spelling errors as you type」與「Correct fuzzy pinyin」**；**Zhuyin – Traditional / Zhuyin Eten 只有 Adjust word frequency、Show simplified and rare characters、Specialized vocabularies、Candidate window、Show predictive completions —— 沒有任何自動校正/模糊選項**。
- **關鍵數字/做法**：預測補全「出現在候選窗第 2 或第 3 個位置」（搜尋摘要所見 Apple 說明）→ Apple 對中文的 AI 介入是「放進候選」而非 inline 自動替換。
- **侷限**：Apple 內部演算法未公開；macOS 27 是否新增注音校正：官方頁面未見（未能確認）。

## 卡片 C7：macOS 日文 Live Conversion（ライブ変換）— 「自動轉換、不需選字」的 UX 前例

- **name**：Apple Japanese Input Method – Live Conversion
- **link**：Apple 說明 https://support.apple.com/guide/japanese-input-method/jpim10265/mac （搜尋結果摘要）；mynavi 報導 https://news.mynavi.jp/techplus/article/20210722-1911971/3 （搜尋結果）
- **是什麼**：OS X El Capitan（2015）起，打平假名時即時轉成漢字混合文，不需每個詞按空白；Return 確定；Escape（新版）/←（舊版）取回原始平假名；可關閉回到傳統「空白鍵變換」。
- **跟我們的關係**：我們「錯字自動修正」若在 marked text 內即時改寫，本質上就是 Live Conversion 的變體：**修正在 commit 前、在組字區內發生，使用者以 Return 確定、Esc 回原文**。這比改已送出的文字安全得多（不依賴 replacementRange）。
- **關鍵數字/做法**：Apple 提醒若字元顯示異常，確認 app 字型與候選窗字型一致，或關閉 Live Conversion（暗示 marked text 頻繁重寫在某些 app 有顯示問題）。Catalina 起 Apple 稱以新的神經語言模型考慮句子前半做預測（iphone-mania 報導，廠商宣稱）。Apple 社群有「第一個字被自動確定」的回報（未能確認原因）。
- **侷限**：azooKey 作者指出 live conversion 中同音字會使「前面已轉好的字」跳動、並增加推論量（zenn 文章）；一般使用者對此的抱怨程度未量化（未能確認）；我們若做 inline 改寫，需控制「改動範圍只在最近 N 個字、且同一處不反覆跳動」。

## 卡片 C8：Grammarly for Mac / LanguageTool for Desktop（AX 路線的校對工具）

- **name**：Grammarly desktop（Mac）、LanguageTool for Desktop（Mac）
- **link**：Grammarly 說明 https://support.grammarly.com/hc/en-us/articles/360059770451-Why-does-Grammarly-need-access-to-accessibility-features ；LanguageTool Mac https://languagetool.org/insights/post/product-mac-app （搜尋結果）；Apple 論壇「如何做 Grammarly 式浮動 UI」https://developer.apple.com/forums/thread/720686 （搜尋結果）
- **是什麼**：系統級文法/拼字檢查，皆**需要 macOS Accessibility 權限**（Grammarly：沒有此權限「our product will not be able to provide writing assistance」）；LanguageTool 可逐 app 開關、per-app picky mode（搜尋摘要）。
- **跟我們的關係**：代表「**底線提示 + 點選才替換**」的保守 UX；與我們「輸入法內即時」不同層，可並存（甚至互相干擾：兩者都改同一段字）。
- **關鍵數字/做法**：Grammarly 開發者整合指南（Superhuman 說明中心，403 無法開啟，僅見搜尋摘要）稱 Mac 端 app 需把文字暴露給 accessibility（NSTextView 開箱即用；自訂 view 需支援 selected-text-range 與變更通知）→ 證實 AX 路線依賴宿主實作。第三方回報：Safari 需另裝擴充、PowerPoint 更新曾破壞整合（搜尋摘要）。延遲數字：**未找到**。
- **侷限**：皆為雲端或混合（Grammarly 雲端；LanguageTool 可自架）；細節非官方。

## 卡片 C9：中國大陸輸入法的 AI 糾錯（搜狗 / 訊飛 / 百度 / 微信輸入法）

- **name**：搜狗輸入法、訊飛輸入法、百度輸入法、微信輸入法（WeType）
- **link**：訊飛 14.0（端側輸入大模型）https://www.ithome.com/0/805/308.htm ；訊飛 15.0 https://m.tech.china.com/hea/articles/20251108/202511081761389.html ；百度 AI 助聊（2021-06-14）https://www.pingwest.com/w/244215 （已開啟：「在用户输入过程中就能迅速标注出来」並給修改建議；召回率/準確率「远超其他同类产品」但無數字）；搜狗 20.0（2026-01）https://www.bbtnews.com.cn/2026/0127/583256.shtml （搜尋結果）；微信輸入法評測 https://sspai.com/post/75527 （搜尋結果）
- **是什麼 / 關鍵做法（皆廠商宣稱或媒體報導）**：
  - **訊飛 14.0（2024-10）**：「訊飛星火端側輸入大模型」需在設定中下載啟用，**離線也能做 AI 增強聯想、整句預測**；未公布參數量/延遲；未說明糾錯是否端側。**15.0（2025-11）**：宣稱端側輸入模型「功耗降低 90%」、覆蓋用戶 +382%；「AI 鍵」做句子補完與語音後語義修正。
  - **百度（2021-06 AI 助聊）**：「AI 校正」在輸入過程**標出錯字並提示更正**，鍵盤右上給修改提示、點擊即改；宣稱召回率/準確率「遠超同類」但無數字（無法核實）。
  - **搜狗**：2024 起接入騰訊混元（AI 帮写等）、2025 接 DeepSeek R1；2026-01 20.0 版宣稱 AI 用戶破億；有「智能糾錯」開關與敏感度（僅見不可靠問答頁，未能確認）；AI 功能主要依託雲端（報導推論）。
  - **微信輸入法**：2022 評測稱有拼音糾錯（不支援雙拼糾錯）、拼寫檢查能改錯字但不管「的得地」；「問 AI」以 `=` 觸發、雲端混元/DeepSeek。
- **跟我們的關係**：中文 IME 大廠的「AI 糾錯」普遍是 **(1) 拼音層容錯（候選中給更正）+ (2) 句子層「標示錯字、點選才改」+ (3) LLM 能力集中在按需功能（帮写/問AI），且多走雲端**；**端側大模型目前公開用途是聯想/預測**，不是逐鍵自動改字。
- **侷限**：皆無技術文件與延遲數字；多為行銷口徑；注音（台灣）市場的對應功能幾乎無公開資料（未能確認）。

## 論文/技術卡 P2：Gboard「Grammar Correction as You Type, on Pixel 6」（Google Research 2021-10-27）【端側延遲數字前例】

- **出處/連結**：https://research.google/blog/grammar-correction-as-you-type-on-pixel-6/ （已開啟）
- **問題**：在手機鍵盤內、完全端側、對「尚未打完的句子」即時做文法更正，受記憶體與延遲限制。
- **核心想法**：seq2seq（**Transformer encoder + LSTM decoder**）、encoder/decoder 共享權重、factorized embedding、**int8 post-training quantization**；**hard distillation**：數億句網頁英文由雲端大型文法模型更正後當訓練對；另建「前綴」資料集（用 LM 補完前綴、再用文法模型判斷），讓模型能在使用者打完前就標錯。
- **結果**：模型 **20MB**；**60 個輸入字元推論 < 22ms（Pixel 6 CPU）**；使用者打超過 **3 個詞**才送請求；輸出以 Levenshtein 對齊轉成 edit，錯處**底線標示、點選才替換（不自動套用）**；僅英文；**WebView 中不可用**。
- **為什麼重要**：證明「小型專用 seq2seq / 蒸餾模型」可在 CPU 上做到每次 ~20ms 的句級更正；且大廠在**句級文法**仍選擇「底線 + 點選」而非自動替換。
- **侷限**：2021 年架構；未公布 precision / 誤報率；英文。
- **對我們輸入法的啟示**：(1) 用雲端/大模型離線「蒸餾」出注音錯字更正的小模型是可行路徑；(2) 句級（非讀音層）更正採「標示+確認」，讀音層（還在組字區）才考慮自動；(3) 觸發條件要有最小上下文（≥3 詞）避免過早誤報。

## 卡片 C10：Microsoft SwiftKey / Windows 硬體鍵盤 text suggestions（簡記）

- **link**：Thurrott Windows 11 Field Guide https://www.thurrott.com/books/windows-11-field-guide/277060/keyboards-mice-and-touchpads （搜尋結果）
- **重點**：Windows 11「Show text suggestions when typing on the physical keyboard」預設關閉；以方向鍵選、Enter 接受；「Autocorrect misspelled words / Highlight misspelled words」只作用於觸控鍵盤，**不作用於實體鍵盤**；可多語建議；離線可用（第三方指南稱）。SwiftKey 與其關係：搜尋結果未證實（未能確認）。
- **啟示**：桌面作業系統對**實體鍵盤自動更正**一向保守（預設關、只給建議），我們的預設值也應保守。

---

## IMK 限制與對策

### A. 官方 API 語意（一手來源：macOS 27.0 SDK 標頭檔，本機 `/Library/Developer/CommandLineTools/SDKs/MacOSX27.0.sdk`）

- **IMKServer**（`InputMethodKit.framework/Headers/IMKServer.h`）：每個輸入法「只建立一個」IMKServer；它建立一個 **NSConnection**（名稱 = Info.plist 的 `InputMethodConnectionName`）供 client 連線，並為每個 client session 建立一個 `IMKInputController`。`initWithName:bundleIdentifier:` 會從 Info.plist 讀 controller/delegate 類別名。
- **三種收鍵方式（擇一）**（`IMKInputController.h`）：(1) keybinding：`inputText:client:` + `didCommandBySelector:client:`；(2) `inputText:key:modifiers:client:`；(3) `handleEvent:client:`（Swift 為 `handle(_:client:)`）。回傳 YES = 已處理，NO 則事件交給 client。
- **生命週期**：`initWithServer:delegate:client:` → `activateServer:` / `deactivateServer:`（IMKStateSetting）→ `commitComposition:`（client 要求立即結束組字時呼叫，「之後應視該 session 結束」）→ `inputControllerWillClose`。`recognizedEvents:` 預設只收 keyDown；若只回 keyDown，IMK 預設在使用者點擊組字區外時送 `commitComposition:`。`updateComposition` 會呼叫 `composedString:` 再 `setMarkedText`；`cancelComposition` 以 `originalString:` 取代 marked text。
- **IMKTextInput（client 代理）**（`HIToolbox.framework/Headers/IMKInputSession.h`）原文要點：
  - `insertText:replacementRange:`：replacementRange 以文件開頭為基準；要插在目前選取處就給 `{NSNotFound, NSNotFound}`；**「If the client does not support the TSMDocumentAccess protocol the replacementRange string will be ignored.」**
  - `setMarkedText:selectionRange:replacementRange:`：selectionRange 相對於 marked 字串；replacementRange 相對於文件；同樣「client 不支援 TSMDocumentAccess 時 replacementRange 被忽略」；傳 NSString 會得到預設底線。
  - **「selectedRange、attributedSubstringFromRange、length 都依賴 client 支援 TSMDocumentAccess；Many applications do not support these so input methods should be prepared for these methods to return nil or NSNotFound.」**
  - `length`：「determining how long a text document is can be a very expensive operation… avoid frequently calling this method.」
  - `stringFromRange:actualRange:`（10.7+）：Apple 稱「clients that do not support TSMDocumentAccess are extremely rare. Certainly the text system for Cocoa does support this」；回傳長度可能因 surrogate pair 與要求不同。
  - `attributesForCharacterIndex:lineHeightRectangle:`：給候選窗定位用；index 相對組字區，無組字時傳 0 取目前選取位置；回傳含 CTFont 與 `IMKTextOrientationKey`（直排/橫排）。另有 `firstRectForCharacterRange:actualRange:`（10.7+）、`characterIndexForPoint:…`、`validAttributesForMarkedText`、`overrideKeyboardWithKeyboardNamed:`、`selectInputMode:`、`bundleIdentifier`、`windowLevel`（自繪候選窗應用「client 視窗層級 +1」）、`supportsProperty:`、`uniqueClientIdentifierString`。
  - 標記樣式：`markForStyle:atRange:` 用 `kTSMHiliteSelectedRawText` / `kTSMHiliteConvertedText` / `kTSMHiliteSelectedConvertedText`。

### B. 各 app 的實際支援度（證據彙整；✔=有實作、△=部分/有坑、✘=不支援；皆附來源）

| 宿主類型 | marked text | replacementRange（改已送出文字） | 讀周邊文字 | 位置查詢 | 證據 |
|---|---|---|---|---|---|
| Cocoa NSTextView（TextEdit、Mail、Notes…） | ✔ | ✔（Cocoa text system 支援 TSMDocumentAccess） | ✔ | ✔ | SDK 標頭註解；fcitx5/iTerm2 對照；#357「NSTextView 系由 macOS 處理延遲 marked text」 |
| WebKit / Safari | ✔ | △（現況未實測、未能確認；2011 年 WebKit2 初期 bug 57649：UI process 快取導致 IME 拿到過時資料、`attributedSubstringFromRange` 一度未實作；其他同步查詢逾時細節未逐一確認） | △ | △ | WebKit bug 討論（搜尋結果）https://bugs.webkit.org/show_bug.cgi?id=57649 |
| Chrome/Chromium 及 Electron（VS Code、Slack、Notion…） | ✔ | △ | △（VS Code 編輯器以分頁 textarea/EditContext 呈現，IME 可見文字有限——部分證據） | **△ 危險**：`firstRectForCharacterRange` 走 `TextInputClientMac::SyncRequest`，**在 Chrome UI 執行緒同步等 renderer，逾時預設 1500ms**（Chromium 150 `kTextInputClientIPCTimeout`）；頁面載入/JS 編譯中可整個瀏覽器卡住；甚至 **deadlock**（Chromium issue 503787240） | Chromium 原始碼 https://raw.githubusercontent.com/chromium/chromium/150.0.7871.49/content/common/features.cc ；Squirrel PR #1154 https://github.com/rime/squirrel/pull/1154 ；azooKey 原始碼註解 |
| Chrome 網址列 / VSCode 編輯器 | △ Backspace 可能被處理兩次（需 dummy preedit）；Brave/Helium 網址列 Backspace 一次清空整個浮動組字區（vChewing #599） | △ | △ | △ | fcitx5-macos 註解；Squirrel 對 Chrome 強制 inline（changelog「work around bksp」）；vChewing #599 |
| Terminal.app / iTerm2 / Ghostty / Alacritty | △（需在 `handle` 回傳前同步放 marked text，否則原始鍵漏到 shell；Squirrel 非 inline 用 U+3000 佔位） | **✘ 實質不可能**：已送出字元已進 pty；iTerm2 `setMarkedText` 原始碼註解「`// TODO: Respect replacementRange`」 | △ iTerm2 `attributedSubstringForProposedRange` 回傳**螢幕緩衝內容**（含提示字元/輸出）；Terminal 讀太多很慢（fcitx5 限 ±1024） | ✔（Terminal 回報正確字型；其他多回 Helvetica 12pt） | iTerm2 原始碼 https://github.com/gnachman/iTerm2/blob/master/sources/TerminalView/PTYTextView.m ；azooKey #357/#379；fcitx5 註解 |
| Microsoft Word / Office | △ Esc 可能繞過 IM（取代選取文字時）；Squirrel changelog：「按 Cmd 會清掉選取」舊 bug | **✘/危險**：vChewing 原始碼註解「replacementRange 不要亂填，否則會在 Microsoft Office 等軟體內出現故障」 | 未能確認 | 未能確認 | fcitx5-macos 註解；Squirrel changelog；vChewing `InputSession_HandleStates.swift` |
| JetBrains IDE（JBR） | △ ZWSP 佔位會被畫成 "ZWSP"，需改用一般空白；每鍵可能「既被 IM 處理又被送到 client」（終端機分頁） | 未能確認 | 未能確認 | 未能確認 | fcitx5-macos 註解 |
| Firefox（2009 紀錄） | ✔ | — | ✘ `attributedSubstringFromRange` 不回東西 | — | Mozc mac 文件 |
| 自繪/canvas（Google Slides、Zed、部分遊戲） | 取決於 app | ✘ | ✘ | △ | Cotypist 相容性表（AX 路線，作為旁證） |

**我的評估**：「在組字區（marked text）內修正」幾乎所有 app 都可行；「回頭改已 commit 的文字」只有 Cocoa/WebKit 類較可靠、Chromium 系不穩、終端機不可能 → **核心更正必須發生在 commit 之前**。

### C. 已知怪癖（macOS 14–27）

1. **macOS 26：`TISSelectInputSource` 程序化切走時不呼叫 `deactivateServer`**（候選窗殘留、組字不上屏；Squirrel #1140，已以監聽 `kTISNotifySelectedKeyboardInputSourceChanged` 自救）；**程序化切入時 controller 常未啟用、按鍵直通 ASCII、輸入選單變空**（#1162，open，非必現）。https://github.com/rime/squirrel/issues/1140 、https://github.com/rime/squirrel/issues/1162
2. **macOS 26 beta：Squirrel 頻繁崩潰並語音提示「launched with bad configuration」**（#1040）→ Squirrel 有「2 秒內重啟則跳過初始化」的 crash-loop 保護。
3. **`imklaunchagent` 會快取 `InputMethodConnectionName`**（macOS 26，hallelujahIM 安裝腳本註解）→ 更新/改名後需 kill `imklaunchagent`、`TextInputSwitcher`、`TextInputMenuAgent` 或登出。
4. **activateServer 不保證被呼叫**（Chrome 某情境；fcitx5「IMK being IMK」）→ 每次 `handle` 都要能自我初始化。
5. **連線名**：近期 macOS 預期 `InputMethodConnectionName = $(PRODUCT_BUNDLE_IDENTIFIER)_Connection`（vChewing commit 說明；與 2006 年 WWDC 建議不同）。
6. **Secure Event Input**：TN2150 https://developer.apple.com/library/archive/technotes/tn2150/_index.html — 任一程序啟用後（不論前景背景）鍵盤事件不再交給 intercept 程序；app 應只在密碼欄位聚焦時開啟；殘留的 secure input 會讓第三方輸入法無法切換/使用（vChewing 字串、Chromium `secure_password_input.mm` 會在密碼欄切到 ASCII-capable 輸入源）。fcitx5 用 `IsSecureEventInputEnabled()` + IORegistry `kCGSSessionSecureInputPID` 找出元兇。**對策**：偵測到 secure input 或密碼欄時，停用所有學習/記錄與 LLM 推論。
7. **login window / 安全代理**：Mozc 文件：`com.apple.securityagent` 中不應觸發網路。→ 我們應在此 bundle ID 及密碼欄停用模型與日誌。
8. **App Sandbox**：IME 可 sandbox，但需 `com.apple.security.temporary-exception.mach-register.global-name = $(PRODUCT_BUNDLE_IDENTIFIER)_Connection`（vChewing、azooKey、Apple 範例社群做法）；Developer ID 簽章下 mach-register 曾被拒的論壇回報 https://developer.apple.com/forums/thread/808940 （搜尋結果）。Squirrel 為載入外掛關閉 sandbox 並開 `cs.disable-library-validation`。
9. **安裝/散佈**：`/Library/Input Methods`（Squirrel、azooKey、fcitx5 皆是）或 `~/Library/Input Methods`；Mac App Store 曾以「背景程式、無 UI」拒絕輸入法上架（2016 論壇；2021 回覆仍稱 Apple 說 no）https://developer.apple.com/forums/thread/43817 → 實務上以 **Developer ID + notarization + pkg** 散佈（Squirrel：`codesign --options runtime --timestamp` → `productsign` → `notarytool submit --wait` → `stapler staple`；postinstall 以 `TISRegisterInputSource`/`TISEnableInputSource`/`TISSelectInputSource` 註冊；需以 console user 的 GUI session 執行，hallelujah 用 `launchctl asuser`）。首次安裝多半需登出。
10. **macOS 26 外觀**：Squirrel 在 macOS 26 用 `NSGlassEffectView` 畫候選窗（Liquid Glass）；Apple 提醒 Live Conversion 字型與候選窗字型不一致時顯示可能異常。

### D. 架構對策（Do / Don't）

**Do**
1. **同步快速路徑**：`handle(_:client:)` 內完成注音組字與 `setMarkedText`（或 `insertText`）**再回傳**；終端機類 app 尤其必要（azooKey #357/#379）。
2. **LLM 永遠非同步、可丟棄**：以 generation token 丟棄過期結果（Squirrel #1154 的作法）；結果只回填到「仍在組字中的同一段」marked text；若使用者已 commit/移動游標/切 app，直接丟棄。
3. **模型單例 + 預熱**：整個程序只載一份模型與 context（azooKey #347 的反例）；KV cache 以共同前綴複用（azooKey `llama_kv_cache_seq_rm`）。
4. **限制對宿主的同步查詢**：只在必要時呼叫 `attributes(forCharacterIndex:…)`/`firstRect…`；Chrome 系同一次組字只查一次位置並快取；**activateServer 期間不查詢**（Chromium deadlock）；周邊文字讀取設上限（azooKey 200 字、fcitx5 ±1024）並只讀左側最後一行。
5. **per-app 策略表**：參考 Squirrel `app_options` 與 fcitx5 dummy-preedit 例外清單（Terminal/iTerm2/JetBrains/VSCode/Chrome/Word/LibreOffice/MacVim/Spotlight）。
6. **自繪候選/提示窗**：不用 IMKCandidates（vChewing 以 bug 多與跨平台阻礙為由棄用；hallelujah、Squirrel、azooKey 皆自繪）；用 `windowLevel()+1`；位置為零矩形時沿用上次座標或滑鼠位置。
7. **監聽 `kTISNotifySelectedKeyboardInputSourceChanged`** 補救 macOS 26 不呼叫 deactivate 的問題。
8. **隱私/安全**：secure input、密碼欄、`com.apple.securityagent` 一律關閉推論與學習；log 不含使用者文字（azooKey #374 只記錄 >200ms 的鍵處理、不含內容）。
9. **若拆 XPC/LaunchAgent**：處理 App Group 簽章（provisioning profile；azooKey #380 曾因權限等待 ~6 秒）、session 回收、逾時降級（server 掛掉時仍能打字）。

**Don't**
1. 不要讓 IME 主執行緒等待模型（任何超過 ~10ms 的工作都會變成打字延遲）。
2. 不要依賴 `replacementRange` 去改已 commit 的文字作為主要功能（終端機不可能、Chromium 不穩、TSMDocumentAccess 不支援時被忽略）。
3. 不要頻繁呼叫 `length()`（Apple 明說可能很貴）或讀整份文件。
4. 不要在每鍵重讀設定檔/辭典（azooKey #380 根因之一）。
5. 不要假設 `activateServer`/`deactivateServer` 一定成對被呼叫。

### E. 重運算放哪裡？（in-process vs XPC/LaunchAgent）與 GPU/ANE

- **證據**：azooKey 早期（2024）在 IME 程序內直接以 llama.cpp + Metal 推論（zenn 文章）；2026 年（#335, v0.1.5-beta.1）移到 LaunchAgent + XPC 的 ConverterServer，**隨即引入終端機漏字、凍結、session 洩漏等一串回歸**（#347/#357/#374/#379/#380）。Mozc 長期採 converter server 獨立程序。Apple 自家 autocorrect 模型由 `AppleSpell` 服務（XPC）提供，並有 ANE 版本的模型 shape 檔（第三方逆向）。
- **GPU/ANE 限制**：未找到 Apple 文件禁止 IME/背景程序使用 Metal 或 Core ML/ANE（**未能確認有任何明文限制**）；azooKey 的 IME 程序本身是 sandboxed 且曾在程序內用 Metal。**但** Apple Foundation Models 在「電池 + 背景程序」會被 rate limit（Apple 工程師論壇回覆），IME 屬背景程序 → 不適合逐鍵使用。
- **我的建議**：MVP 採 **in-process 推論於獨立 serial queue/actor**（避免 XPC 同步語意問題），但以「看門狗 + 熔斷」保護：推論逾時（例如 >80ms）即放棄該次結果；記憶體上限監控；模型崩潰風險高時（例如換 runtime）再拆 XPC helper，並沿用 azooKey 已踩過的對策（同步暫定 marked text、單例模型、App Group profile、逾時降級）。

## 卡片 O6：TabType（開源、Cotypist 式 AX ghost-text 補全；本機 llama.cpp）

- **name**：TabType（nilava/TabType，MIT；HEAD 讀碼 2026-10）
- **link**：https://github.com/nilava/TabType ；release 說明 https://newreleases.io/project/github/nilava/TabType/release/v2.0.0
- **是什麼**：menu-bar app，以 **Accessibility API + CGEventTap** 在任何 app 畫灰色續寫建議，Tab 逐詞接受；本機 **llama.cpp（b11490 XCFramework, Metal）**，16GB 機預設 **Qwen3-4B base（2.5GB）**、8GB 用 Qwen3-1.7B（1.1GB, Q4_K_M）；v1 用 MLX 後移除（MLX、swift-transformers、Apple Intelligence 引擎皆移除）。未公證（自簽 identity），不在 sandbox。
- **跟我們的關係**：**AX 路線的完整開源參考實作**，且有公開 eval 與延遲數字；它踩過的 AX 坑正是「不做 IME 會遇到的成本」。
- **關鍵數字/做法（讀碼 + eval/README.md）**：
  - **延遲（p50，eval 硬體未註明）**：Qwen3-4B base **104ms**、Qwen3-1.7B **44ms**、Qwen3-0.6B **23ms**、Gemma 4 E2B 76ms、E4B 117ms；預設 9-wide beam 的 4B 為 p50 147ms / p95 232ms；v1（MLX）p50/p95 296/478ms；啟動後第一個建議從 ~2.2s 降到 ~240ms。
  - **品質**（自建 seed set 192 例）：4B base recall 56.2% / precision 70.6% / wrong-show 23.4%；0.6B recall 38.0%。作者私有 867 例：下一詞 recall 約 31–34%；**詞中補完正確率 53–65% vs 詞界 21–32%**（詞中容易、詞界難）。螢幕 OCR 上下文把 recall 從 27.5% 提到 31.0%。
  - **信心門檻**：只在模型夠確定時顯示：首 token p≥0.02 取 4 候選、beam 9×3、每多一詞需 p≥0.05；顯示門檻詞中 0.08、詞界 0.15（`LlamaEngine.swift`）。
  - **AX 讀取**：`kAXFocusedUIElementAttribute`（每元素 0.05s 逾時、系統層 0.25s）、`kAXValueAttribute`、`kAXSelectedTextRangeAttribute`、`kAXBoundsForRangeParameterizedAttribute`、WebKit/Chromium 的 `AXSelectedTextMarkerRange`/`AXBoundsForTextMarkerRange`、`kAXAttributedStringForRangeParameterizedAttribute`（取字型/顏色）；對 Chromium/Electron 寫入 `AXManualAccessibility`/`AXEnhancedUserInterface` 以開啟無障礙樹。
  - **插入**：合成 `CGEvent`（`keyboardSetUnicodeString`，每 16 UTF-16 單位一塊、加 userData 標記避免自己攔截）；多行或 ≥80 字改用**剪貼簿 + ⌘V，150ms 後還原剪貼簿**；Slack/Chrome/VS Code/Notion/Obsidian 一律貼上；**從不使用 AX setValue**。
  - **AX 怪癖（原始碼註解）**：TextEdit 回零長度且離游標很遠的矩形；網頁 CSS 行盒偏大；**Electron 游標座標落後 0.3–0.5s**；Chromium 貼上後重新宣告焦點；WebKit 先更新 index 後更新 bounds；Monaco/CodeMirror 的隱藏 textarea；Google Docs 為 canvas（需開無障礙模式）。為了畫得像宿主字型，甚至**截圖 + CoreText 比對字型（約 50ms/次）**、繪製前做像素佔用檢查。
  - **取消/復用**：`GenerationGate` 計數器在每個 decode step 檢查 → 過期請求在一步內停止；新輸入若只是延伸進行中的生成則讓它跑完再拼接；KV 以最長共同 token 前綴復用；prompt 由穩定到變動排列以利 cache；結果快取 30s/200 筆。
  - **typo**：LLM 不負責；詞界用 **SymSpell（edit distance 2）**自動改，以 Backspace×n + 重打實作；若目前詞（≥4 字母、小寫、上文沒出現過）無法成為字典詞就不生成；豁免短字、大寫字、螢幕上出現過的字（如 "kubectl"）。
  - **安全**：跳過 `AXSecureTextField` 與疑似帳密/OTP/卡號欄；每 10s 輪詢 `IsSecureEventInputEnabled()` 並以 `kCGSSessionSecureInputPID` 指出元兇；`SecretSanitizer` 從上下文刪除金鑰/JWT/卡號/IBAN。
  - **省電**：Low Power Mode 暫停截圖；電池時可切「按需/短建議/小模型」；15 分鐘閒置或記憶體壓力時卸載模型。
  - README 宣稱實測「Cotypist 自己的模型檔（Gemma 4 E2B）」可在 TabType 執行（第三方說法，**未能獨立確認** Cotypist 用何模型）。
- **侷限**：AX 路線需大量 per-app hack；Secure Input 時 event tap 失明；未公證、需手動授權；eval 硬體與 RSS 未公開。
- **對我們的啟示**：IMK 路線天生拿到「正確的游標位置、宿主字型（`attributesForCharacterIndex`）與可靠插入」，省掉 TabType 一半以上的工程；但其**信心門檻設計、GenerationGate 取消、KV 前綴復用、字典閘門（模型前先用字典判斷是否需要動）、豁免規則**可直接移植。

## 卡片 O7：AOSP LatinIME（Android 開源鍵盤）— revert-on-backspace 的標準實作

- **name**：AOSP LatinIME `InputLogic.java`
- **link**：https://android.googlesource.com/platform/packages/inputmethods/LatinIME/+/refs/heads/main/java/src/com/android/inputmethod/latin/inputlogic/InputLogic.java （以 `?format=TEXT` 下載並讀碼）
- **是什麼**：Android 原生鍵盤的輸入邏輯（Gboard 前身之開源版本）。
- **跟我們的關係**：「自動更正後**立刻按 Backspace = 還原原字**」這個慣例的原始碼級證據。
- **關鍵做法（讀碼）**：`revertCommit()` 註解「Reverts a previous commit with auto-correction. This is triggered upon pressing backspace just after a commit with auto-correction.」；條件 `mLastComposedWord.canRevertCommit()`；還原時把原字連同 `SuggestionSpan`（其他建議）一起 commit 回去；統計 `StatsUtils.onRevertAutoCorrect()`；**對無空格語言（如中文）**：還原後直接回到組字狀態（`setComposingWord` + `setComposingTextInternal`），讓使用者繼續編輯；註解提到還原後重新啟動建議是為了讓「最後修正的字」能被學習。
- **侷限**：Android InputConnection 語意與 macOS IMK 不同；在 IMK 上「還原」若字已 commit，需 `replacementRange` 或送 Backspace，受宿主支援度影響 → **我們應在 commit 前提供還原**（例如組字區內 Backspace 第一下先還原 LLM 修正、第二下才刪字）。

---

## UX 模式整理（自動替換 vs 底線建議、還原、門檻、個人化）

| 產品 | 介入層級 | 呈現 | 還原方式 | 從還原學習 | 來源 |
|---|---|---|---|---|---|
| Apple iOS 17+/macOS 鍵盤 autocorrect | 詞級自動替換 | 替換後**短暫淡藍底線** | 點底線看原字/其他建議 | ✔ 還原後同打法不再自動改（改紅底線） | MacStories |
| Gboard autocorrect（2025-10） | 詞級自動替換 | **highlight** 被改的字 | 點一下→候選列出現原字→一鍵還原（逐字） | 未能確認 | helentech（引 Android Authority） |
| AOSP LatinIME | 詞級自動替換 | 底線 composing | **緊接 Backspace 還原** | 還原後重啟建議以學習 | 原始碼 |
| ATOK ディープコレクト | 鍵序（讀音）層自動修復 | 直接出現修復結果 | **Shift+BackSpace** 取消 | ✔ 確定原樣後不再修復同輸入 | PC Watch |
| Gboard 端側文法（Pixel 6） | 句級 | **底線，不自動套用** | 不需（點選才改） | — | Google Research |
| 百度 AI 校正 | 句級 | 標出錯字、鍵盤右上提示 | 點擊才改 | 未能確認 | pingwest（搜尋結果） |
| Grammarly / LanguageTool | 句級 | 底線 + 浮窗 | 點選才改 | 個人辭典 | 官方說明（部分） |
| Apple Writing Tools Proofread | 段落級、按需 | 直接改寫整段 | 只能整段接受/拒絕（15.1 時代） | — | 評測 |
| azooKey 錯字（debug） | 鍵序層 | 以「預測候選」呈現 | Tab 才接受 | — | 原始碼 |
| hallelujahIM | 詞級 | 候選窗（原輸入永遠第 1） | 不需（永不自動改） | — | 原始碼 |
| TabType / Cotypist | 補全（＋SymSpell 詞界自動改） | 灰色 ghost text | 繼續打字即消失 | 個人化 suffix index | 原始碼 / 官網 |
| Windows 實體鍵盤 | 建議 | 建議列（預設關） | 方向鍵 + Enter 才接受 | — | Thurrott |

**歸納**（我的評估）：
1. **自動替換只在「詞/讀音層、高信心、可一鍵還原」時被大廠採用**；句級/語意層一律「標示 + 確認」。
2. **還原必須零成本**：Backspace（LatinIME）、Shift+Backspace（ATOK）、點一下（Apple/Gboard）；且**還原 = 強負回饋**（Apple、ATOK、LatinIME 都會學）。
3. **信心門檻要依情境分開**：TabType 詞中 0.08 / 詞界 0.15；Mozc `TypeCorrectedQuery.score`（相對原樣輸入的分數差）決定是否觸發；azooKey 錯字 beam 永遠把原輸入當 baseline。
4. **豁免規則**：短字、大寫、上文/螢幕出現過的字、專有名詞、程式碼/終端機情境（TabType、Squirrel 預設終端機英文模式）。
5. **打擾成本真實存在**：Gruber 對 Cotypist「a never-ending distraction」；Apple 社群抱怨正確字也被藍底線、影響游標點選 → 底線/高亮要短暫且可關。

### 補充：vChewing（唯音/威注音）issue tracker 近期真實問題（GitHub API 讀取）

- **#620（2026-10，macOS 26.7.1，MacBook Air M1）漸退記憶（POM，使用者學習）資料累積後「每個字都慢」**，關閉學習或清除資料即恢復；POM 檔 235KB + journal 28KB；重啟後記憶體約 200MB。https://github.com/vChewing/vChewing-macOS/issues/620
  → 教訓：**個人化資料的查詢/寫入不可在逐鍵熱路徑上隨資料量線性變慢**；要有上限、壓縮與背景整理。
- **#616（2026-09）加詞與 POM 背景存檔鎖順序相反 → 死鎖，輸入法永久無回應，而且「當下打字的 app（Terminal、LINE）也跟著『沒有回應』」**；POM 2,218 筆時一天發生四次。https://github.com/vChewing/vChewing-macOS/issues/616
  → 教訓：**IME 卡住會連帶凍結宿主 app**（IMK 呼叫是同步的）；背景存檔/模型推論與主執行緒之間的鎖要極度保守，或用 actor/單一序列佇列。
- **#599（2026-07）Chromium 系網址列（Brave、Helium）中按 Backspace 會一次清掉整個浮動組字區**（Firefox 正常）。https://github.com/vChewing/vChewing-macOS/issues/599
- **#618（2026-10，open）切換 App 後收不到 `flagsChanged`，右 Shift 無法切英數**（Caps Lock 後恢復）。https://github.com/vChewing/vChewing-macOS/issues/618
- **#602（2026-08）企業微信/新版微信無法切換到唯音；曾觸發 `TUINSCursorUIController CFRelease(NULL)` 崩潰**（系統游標 UI 元件）。https://github.com/vChewing/vChewing-macOS/issues/602
- **#88（2022）OpenVanilla 曾對 Chrome 延遲 1/20 秒（50ms）才更新組字區**（引 Chromium bug 86460、580808，亦影響 Google 日本語入力與 Apple 拼音）；vChewing 2022 年在 macOS 12.4 無法重現而未引入。https://github.com/vChewing/vChewing-macOS/issues/88

### 補充：librime 容錯
- librime 早有「spelling correction」功能（CHANGELOG 記 #228）與拼寫運算 `fuzz`；Squirrel 1.1.0（2026-01-11）隨附 **librime 1.16.0：「拼寫運算增設容錯規則 `derive/X/Y/correction`」「修復糾錯候選排序權重以及與造句的策略衝突」**（Squirrel CHANGELOG 讀碼確認）。https://github.com/rime/librime/blob/master/CHANGELOG.md
- 對我們：Rime 的「容錯」是**規則式拼寫變體 + 排序懲罰**，是 LLM 之前的必要底層（先產生候選，再讓模型挑）。

### IMK 怪癖補充（安裝/啟用）
- Apple 論壇（未獲回覆）：近期所有 macOS 版本，在 Input Methods 資料夾新增或修改輸入來源後，需**登出再登入**系統設定與選單列才會更新。https://developer.apple.com/forums/thread/775526 （搜尋結果摘要）
- vChewing commit 說明：**macOS 12 起，註冊/啟用第三方輸入法時會跳出允許提示**，因此將啟用流程移到安裝器。（搜尋結果摘要，testgitea 鏡像）
- PTT 討論（2026-06）：macOS 27 將不再支援 Intel 架構 App（舊輸入法經 Rosetta 執行者受影響）——**未能確認 Apple 官方對輸入法的說明**；我們的 IME 應為 arm64 原生。https://disp.cc/ptt/MAC/1gGm2erM （搜尋結果）
- macOS 27 對 IMK 的 API 變更：**搜尋未找到任何官方說明（未能確認）**；本機 MacOSX27.0.sdk 的 IMK 標頭仍只有 10.7 年代的 availability 標記，無新增 API（讀標頭確認）。

### O1 再補充：zenz 模型檔
- HF `Miwa-Keita/zenz-v3.2-small-gguf`：**Apache-2.0**、**95.1M 參數**、GPT-2 架構、唯一量化 **Q5_K_M = 73.9MB**（與 azooKey #347 中 vmmap 看到的 70.3M 映射一致）；model card README 為空（訓練資料未載明）。https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf
- 估算（我的推論）：~95M/Q5 的模型 + 18MiB KV + ~150MB 程序總記憶體（Docs/zenzai.md）→ 這個量級在 16GB MacBook Air 上幾乎無感。

### C8 補充：Grammarly 端側 GEC（Grammarly 工程部落格，2025-03-27 更新）【商業端側延遲前例】
- 連結：https://www.grammarly.com/blog/engineering/on-device-models-scale/ （已開啟）
- **廠商宣稱**：文法錯誤更正（GEC）模型已在端側出貨給「數百萬」桌面用戶（Mac、Windows、Chrome 擴充）；核心為 **T5**；原雲端模型需近 **4GB**，最終端側 **<300MB**；量化比較後 **BFLOAT16** 最佳（記憶體約減半、精度影響小，INT4 較差）；**Rust SDK** 跨平台、Apple 上用 **Metal**，multi-head attention 換成 **MLX** 的最佳化函式；目標「建議 **<100ms**」、整條 GEC 管線 **100+ tokens/s**；T5 吞吐 **70 → 297 tokens/s**；記憶體與延遲降 50%+。
- **我的評估**：這是「商業級、Mac 上、端側、文法更正」唯一有公開數字的案例：**<100ms、~300MB、BF16、seq2seq**。對我們：逐鍵（<16–30ms）預算遠比 Grammarly 嚴，代表我們不能每鍵跑 seq2seq 整句改寫，應「讀音層小模型逐鍵 + 句級模型在停頓/標點時才跑」。

---

## 神經網路/LLM 輸入法前例與實測延遲

> 標註：【論文】=同儕/學會發表；【讀碼】=我們讀原始碼/設定確認；【issue】=GitHub issue 實測紀錄；【廠商】=廠商宣稱；【第三方】=第三方逆向/評測。數字一律照原文，不換算成同一指標（各自定義不同）。

| # | 系統 | 任務 | 模型 / 大小 | 執行 | 延遲數字（原文定義） | 硬體 | 來源性質 |
|---|---|---|---|---|---|---|---|
| 1 | Zenzai（NLP2025 論文） | かな漢字變換（讀音→文字） | GPT-2 xsmall 26M / small 91M / medium 310M | llama.cpp（自改 fork）GPU + KV cache；統計式 draft + SD | **每輸入字元平均**：xsmall 3.5ms（無 SD）；small 6.0ms（SD）；medium 6.9ms（SD）/ 26.4ms（無 SD）；L=1 時 3.7–4.8ms | M2 Pro Mac mini 32GB | 【論文】 |
| 2 | Zenzai（zenn 解說，zenz-v1） | 同上 | ~90M 字元級 GPT-2，Q8_0 | llama.cpp from Swift | 每 query：統計式 26.2ms、Zenzai 59.8ms、rerank 120.6ms；優化後約 **40ms/操作**；作者：**20–30ms 才順、40ms 是 live conversion 上限** | M2 Pro；M3 MBA 相近 | 【作者】 |
| 3 | azooKey on macOS（現行碼） | 同上 + 個人化 | zenz-v3.2-small 95.1M，Q5_K_M 73.9MB；KV 18MiB；程序約 150MB | llama.cpp b4846（Metal）；`n_ctx=512` | CPU 路徑 b9637：**10.8 → 7.2 ms/request**（但多 63.77MiB）；inferenceLimit 預設 5（1 通常足夠） | M2 Pro | 【讀碼】 |
| 4 | azooKey ConverterServer（XPC 拆分後） | 同上 | 同上 | LaunchAgent + XPC | 鍵→marked text **30–110ms** 才回來（造成終端機漏字）；>200ms 的鍵處理記 log；宿主位置查詢偶發 **2–3 秒**卡住；App Group 權限等待 **~6 秒** | M3 Pro / MBA M4 | 【issue】 |
| 5 | Apple autocorrect / inline prediction | 詞級更正、預測 | GPT-2 風格 ~34M、6 層、hidden 512、詞彙 ~15k | Espresso（CPU/ANE 版本 shape 檔） | 無數字（「almost instantly」） | — | 【第三方逆向】 |
| 6 | Gboard 端側文法（Pixel 6） | 句級 GEC | Transformer enc + LSTM dec，int8，**20MB** | 手機 CPU | **60 字元 < 22ms** | Pixel 6 CPU | 【廠商技術部落格】 |
| 7 | Grammarly 端側 GEC | 句級 GEC | T5，**<300MB**，BF16 | Rust SDK + Metal + MLX attention | 目標建議 **<100ms**；**70→297 tokens/s** | Mac/Win/Chromebook | 【廠商】 |
| 8 | TabType | 續寫補全（beam） | Qwen3 0.6B / 1.7B / 4B base（Q4_K_M 等） | llama.cpp b11490 Metal | p50：**23 / 44 / 104ms**；4B 預設 beam p50/p95 147/232ms；v1 MLX 296/478ms | 未註明 | 【讀碼/eval】 |
| 9 | Cotypist | 續寫 + typo | 未公開（第三方稱 Gemma 系/Qwen 系，未能確認） | 本機 | 「almost instantly」；RAM 1–2.5GB | 建議 M1 Pro/M2+16GB | 【廠商】 |
| 10 | Gboard Proofread | 句/段級一鍵改寫 | PaLM2-XS（tuned） | **雲端** TPU v5 | 未給絕對值（量化/bucket/分段/SD 降延遲） | — | 【論文】 |
| 11 | 訊飛 14/15 端側輸入大模型 | 聯想/整句預測 | 未公開 | 手機端 | 無；宣稱功耗 -90% | — | 【廠商】 |
| 12 | ATOK ディープコレクト | 鍵序修復 | 未公開 | 本機 | 無；修復率 +35% | — | 【廠商】 |
| 13 | rime-llm-translator | 熱鍵觸發整句轉換/糾錯 | 雲端 API / CLI | HTTP / CLI | DeepSeek Flash 無思考「幾乎即時」；claude CLI ~3s；codex 等 7–10s；預設逾時 60s | — | 【README】 |
| 14 | 宿主端：Chromium `TextInputClientMac` | （IME 查詢游標位置） | — | Chrome UI 執行緒同步等 renderer | 逾時預設 **1500ms** | — | 【讀碼】 |

**解讀（我的評估）**
1. **能「每鍵同步」跑的只有 ≲100M 級、專用任務的小模型**（Zenzai 26–95M：每字數 ms；Pixel 端側 GEC 20MB：60 字 22ms）。通用 0.6B–4B LLM 的單次延遲是 23–104ms（TabType p50），只適合**非同步、可丟棄**的增強或停頓時才跑。
2. **「受限/驗證式」用法比「自由生成」快又安全**：Zenzai 讓 LM 只做一次 forward 驗證 draft（≈1 次 decode），且最終輸出必來自辭典 → 防幻覺、防過度更正；TabType 以信心門檻抑制錯誤顯示（wrong-show 仍 ~23%）。
3. **真正的延遲殺手常在 LM 以外**：XPC 往返（30–110ms）、宿主同步查詢（Chromium 1.5s、azooKey 2–3s）、個人化資料鎖與 journal 重放（vChewing #616/#620）、權限等待（6s）。設計預算要把這些尾端延遲算進去。
4. **大廠的端側模型都經過「蒸餾 + 量化 + 專用架構」**（Google hard distillation + int8；Grammarly T5 4GB→300MB BF16；Apple 34M）→ 我們的注音錯字模型也應走「大模型離線產生訓練對 → 蒸餾小模型」。
5. **雲端 LLM 在 IME 只能是熱鍵/按需功能**（rime-llm-translator 秒級；azooKey「いい感じ変換」60s 逾時；Gboard Proofread one-tap）。

---

## 卡片 O8：vChewing 唯音（原威注音）輸入法【注音 IMK 工程的最佳在地參考】

- **name**：vChewing-macOS（v4.9.1，commit 81cbe2e，2026-10；README 已稱「唯音」）
- **link**：https://github.com/vChewing/vChewing-macOS ；Gitee 鏡像 https://gitee.com/vChewing/vChewing-macOS/
- **是什麼**：Swift（+少量 ObjC）實作的 macOS 注音輸入法；組句引擎 2026-04 下旬起由 **Megrez 遷移到 Homa**（DAG 動態規劃求最大分數路徑，O(N+E)，log 分數空間，不用拓撲排序；見 `algorithm.md`）；注音組字 **Tekkon**；使用者學習 **LXPerceptor**（POM）。授權：主程式 MulanPSL-2.0；Tekkon、Homa 為 LGPL-3.0+（Swift 例外）；**詞庫 MulanPSL-2.0，但詞頻資料「由 NAER 授權用於非商業用途」**（README 第 125 行）→ 商用需另找詞頻來源。
- **跟我們的關係**：注音 + IMK + Swift，且對 IMK 怪癖記錄最詳盡；**Tekkon 的「自動糾正讀音組合」就是讀音層的規則式錯字更正**，可當我們的第一層（規則）＋ LLM 第二層。
- **關鍵數字/做法（讀碼確認）**：
  1. **Tekkon 自動糾正讀音組合**（`Tekkon_SyllableComposer.swift`，偏好 `AutoCorrectReadingCombination` 預設開；中英混打模式時停用）規則例：ㄅㄆㄇㄈ+ㄨ+ㄛ/ㄥ → 去 ㄨ（**ㄅㄨㄥ→ㄅㄥ**）；ㄋㄌ+ㄨ+ㄟ → 去 ㄨ；ㄧ/ㄩ 後的 ㄜ → ㄝ；ㄨ+ㄝ → ㄩㄝ；ㄩ+ㄛ → ㄨㄛ；ㄓㄔㄕㄗㄘㄙ 後接 ㄧ 則去 ㄧ、接 ㄩ 則 ㄓ/ㄗ→ㄐ、ㄔ/ㄘ→ㄑ、ㄕ/ㄙ→ㄒ。其他容錯：允許先打聲調、自動切音節（`Tekkon_PhonabetAutoChop.swift`）、無聲調連打（「Furious Zhuyin」）、**中英混打回退**（按鍵無法依序組成合法讀音或 ≥2 鍵覆寫前鍵時視為英文，保護 "tod"、"film"、"hell" 等）。
  2. **IMK 呼叫全部在 MainActor**；`DevLab/InputMethodKitPhuquingRetarded.txt`（作者筆記）：macOS 10.12 起 CapsLock 中英切換其實是「切換輸入法」，每次切換都建立新 controller 並 `activateServer` → 高頻切換造成**主執行緒塞車與 ARC 壓力**，macOS 26 的 ARC 效率問題使其更糟。建議：`deactivateServer` 不要寫 client；`overrideKeyboard()` 要非同步呼叫（否則輸入法自己的視窗可能卡死數十秒）；**IMKInputController 子類別不要擁有任何物件**，業務邏輯放 singleton。vChewing 實作上只保留兩個 session、以 raw address 傳遞避免 retain client，並（可關閉地）用私有 API 清掉孤兒 controller（macOS 15 把私有 client wrapper 類別拆成兩個）。
  3. **marked text 規則（原始碼註解）**：要顯示游標則所有底線粗細必須相等；selectionRange 長度須為 0；**macOS 14 起相鄰等粗底線會連成一條**；**「replacementRange 不要亂填，否則會在 Microsoft Office 等軟體內出現故障」**；空組字時放佔位字元，避免 IMK 判定組字結束把鍵直通給 app；insertText 前先清 marked text（微信相容）；QQNT 不支援粗底線；**macOS 26 上寫入暫定 marked text 會讓 client 多跑一次 deactivate/reactivate**。
  4. **per-app 緩解**：預設「IMK 不相容」清單 `com.valvesoftware.steam`、`jp.naver.line.mac`、`com.openai.chat` → 改用浮動組字窗 + 佔位；**Electron app 預設浮動組字窗**（偵測 Info.plist 含 electron 或 Frameworks 內有 electron/mswebview/slimcorewebview）；Discord、`org.mozilla.*` 用 "_" 佔位；`com.apple.SecurityAgent` 靜默模式；Electron 會送重複的 Shift/CapsLock 事件。**不讀周邊文字**（proxy 存在但未使用）。
  5. **Secure Input**：每 60 秒讀 IORegistry `kCGSSessionSecureInputPID`；**非前景卻持有 secure input 的程序被視為濫用，vChewing 會呼叫 `DisableSecureEventInput()` 並警告**（預設開）。
  6. **記憶體自我保護**：每次 activate 檢查；改用 `task_vm_info.internal` 而非 `phys_footprint`（註解：Apple Silicon/macOS 26 上後者含 WindowServer/GPU surface）；連續兩次（間隔 0.25s）≥ **1536 MiB** 即通知並 `NSApp.terminate`，由 IMK 按需重啟。README 稱雙模式約 115MB。
  7. **候選窗**：棄用 IMKCandidates（理由：macOS 專屬、需 bridging header 用內部 API、「IMKCandidates is buggy」、Apple 範例也不用）；自製「田所（TDK）」NSPanel，level = `max(CGShieldingWindowLevel(), kCGPopUpMenuWindowLevel)+2`；定位用 line-height rect（原點為 (0,0) 時遞減 index 重試）。
  8. **Info.plist**：`InputMethodConnectionName = $(PRODUCT_BUNDLE_IDENTIFIER)_Connection`、`InputMethodServerControllerClass/DelegateClass/DataSourceClass`、`TISInputSourceID = org.atelierInmu.inputmethod.vChewing`（模式 .IMECHT / .IMECHS）、`tsInputModeScriptKey = smUnicode`、`TICapsLockLanguageSwitchCapable = true`、`TISIconIsTemplate = true`、`TISParticipatesInTouchBar = false`、`LSUIElement = true`、`NSSupportsSuddenTermination = false`。
  9. **Sandbox/散佈**：`ENABLE_APP_SANDBOX=YES` + hardened runtime；entitlements 含 `mach-register.global-name`（`…_Connection`）等 temporary exceptions；安裝到 **`~/Library/Input Methods/vChewing.app`**；安裝器 kill 舊程序、移除 quarantine、kill TextInputMenuAgent、`TISRegisterInputSource`；`notarytool submit` → `stapler staple`。
  10. **使用者學習**：LRU 500 筆；年齡因子 `(max(0,1−age/8天))²`；頻率因子 `0.5√p + 0.5·log1p(count)/ln10`；低於 −13 丟棄；崩潰旗標時隔離學習檔。（相關 issue：#616 鎖順序死鎖、#620 資料累積拖慢每鍵——見前節。）
- **侷限**：詞頻資料非商用授權；主程式 MulanPSL-2.0（與 GPL 相容性需評估）；大量私有 API 介入（可關）；無 LM/神經模型。

## 卡片 O9：McBopomofo 小麥注音

- **name**：McBopomofo（openvanilla，commit be6564a）
- **link**：https://github.com/openvanilla/McBopomofo
- **是什麼**：Swift controller + ObjC++ KeyHandler + C++ 引擎：**Gramambular2 `ReadingGrid`**（DAG 最短路徑、拓撲排序、log-prob unigram、最大跨度 8）；`ParselessLM` 以 mmap 的排序文字檔 + 二分搜尋；`UserOverrideModel`（容量 500、半衰期 5400 秒、指數衰減）。
- **跟我們的關係**：**MIT 授權**的注音引擎，最適合當 Zenzai 式架構的「draft lattice」底座（授權乾淨度優於 vChewing 詞頻）。
- **關鍵數字/做法**：資料 `BPMFMappings.txt` 源自 libtabe `tsi.src`（BSD）；詞頻來自私有語料（`textpool.rc`，未公開）；bpmfvs 讀音資料 Apache-2.0；程式碼 MIT。Info.plist：`InputMethodConnectionName = McBopomofo_1_Connection`、`TISDoubleSpaceSubstitution = 。`、`TICapsLockLanguageSwitchCapable = true`；候選窗自繪 NSPanel（level `kCGPopUpMenuWindowLevel+1`，依 `IMKTextOrientation` 判斷直排）。workaround：nil event（FB11472618）時 commit；deactivate 時不 `setMarkedText`（#346）；replacementRange 一律 NSNotFound。安裝到 `~/Library/Input Methods/`；**README：macOS 可能限制同一次登入 session 內 kill 同一輸入法程序的次數，裝太多次會失效，需登出再登入**。錯字/模糊：**未找到**（僅拼音 "v" 容忍）。
- **侷限**：無錯字更正；詞頻語料不公開；無個人化以外的 LM。

## 卡片 O10：libchewing 新酷音（C → Rust）

- **name**：libchewing（commit 3c4a93a；`Cargo.toml` 0.12.0-alpha.3，Rust edition 2024，LGPL-2.1-or-later）
- **link**：https://github.com/chewing/libchewing
- **是什麼**：老牌注音引擎；**函式庫邏輯已全部 Rust（約 19.9k 行）**，C 只剩 `chewing.h` 標頭與 C 測試；NEWS：0.7.0 首個 Rust beta、0.8 起預設 Rust、1.0 將移除 C；附 SwiftPM 包（plugin 以 cargo build 靜態庫）。
- **跟我們的關係**：跨平台、可嵌入 Swift 的注音引擎；**`FUZZY_CHEWING` 引擎**提供前綴/部分注音/無聲調查詢，可當「容錯候選產生器」。
- **關鍵數字/做法**：conversion engine `SIMPLE 0 / CHEWING 1 / FUZZY_CHEWING 2`；Fuzzy 用 `LookupStrategy::FuzzyPartialPrefix`（字典音節以輸入音節為前綴即匹配）；CHEWING 引擎分數 `ln(freq/1e9)` + 長度先驗，DAG 上用改良 **Yen's algorithm 取 10 條最佳路徑**（天然 N-best，適合給 LM 重排/驗證）；字典為 DER/ASN.1 trie（"CHEW" magic），SQLite 為選配（0.11 起預設關）；學習 `chewing_set_autoLearn`、刪除詞進 `chewing-deleted.dat`。**讀音組合自動糾正：未找到**。資料授權：子模組未取回（未能確認）。
- **侷限**：LGPL；fuzzy 是「前綴/省略」容錯，不是鄰鍵錯字更正。

### IMK 補充（vChewing / McBopomofo 讀碼後新增的限制）
- **所有 IMK API 都在主執行緒（MainActor）**；CapsLock 中英切換 = 切換輸入法 = 每次建立新 controller + activate → 主執行緒塞車（vChewing DevLab 筆記；macOS 26 更糟）。→ 我們的 controller 必須「零擁有」、activate 極輕量，LLM 狀態放 singleton/actor。
- **Electron app 的 inline 組字不可靠**：vChewing 預設對 Electron 改用「浮動組字窗 + 佔位字元」；Steam、LINE、ChatGPT 桌面版被列為「IMK 不相容」。→ 我們的「組字區內 LLM 修正」在這些 app 應改在浮動窗內呈現。
- **macOS 14 起相鄰等粗底線會連成一條、要顯示游標需所有底線等粗** → 想用「不同底線」標示被 LLM 改過的片段，視覺上可能看不出來；Chromium `validAttributesForMarkedText` 只接受 `NSUnderlineStyle`、`NSUnderlineColor`、`NSMarkedClauseSegment`、`NSTextInputReplacementRange`（Chromium 150 原始碼，註解「copied from WebKit」）→ **可用 `NSUnderlineColorAttributeName` 改底線顏色來標示修正處**（是否在所有 app 生效：未能確認，需實測）。
- **macOS 26：寫入暫定 marked text 會讓 client 多跑一次 deactivate/reactivate**（vChewing 註解）——與 azooKey #379「同步暫定 marked text」的作法可能互相牽動，需實測。
- **IME 程序可被系統按需重啟**（vChewing 超過 1536MiB 自殺、IMK 再拉起）；但 **McBopomofo README：同一登入 session 內 kill 輸入法程序的次數可能被 macOS 限制**，裝/殺太多次會失效需登出。→ 不要把「自殺重啟」當常規降級手段。
- **Chromium 50ms 延遲 hack 的歷史**：OpenVanilla 曾對 Chrome 延遲 1/20 秒更新組字（Chromium bug 86460、580808）；vChewing 2022 年無法重現而未採用（#88）。

### IMK 補充：Chromium（含所有 Electron app）NSTextInputClient 實作細節（讀 Chromium 150.0.7871.49 原始碼）
- 檔案：https://github.com/chromium/chromium/blob/150.0.7871.49/content/app_shim_remote_cocoa/render_widget_host_view_cocoa.mm ；`content/renderer/render_frame_impl.cc`
- **周邊文字只有「選取點前後各 100 字」的快取**：`kExtraCharsBeforeAndAfterSelection = 100`；`attributedSubstringForProposedRange` 從瀏覽器端快取 `_availableText` 回答，**由 Blink 非同步更新**；原始碼註解直言「IMEs read `_availableText` preceding the insertion point as the context for predictive completion. Unfortunately by the moment IME reads the text, Blink likely hasn't finished the commit so the IME will read a wrong context」與「Mac's IME API is synchronous and it plays badly with async APIs between the browser and the renderer」（crbug 40165347）。→ **在 Chrome/Electron 讀到的左文脈最多約 100 字、且剛 commit 的字可能還沒進去**。
- **`setMarkedText` 的 replacementRange 只部分支援**：註解「TODO(suzhe): It's hard for us to support replacementRange without accessing the full web content.」（只用來更新 markedRange 記帳）。
- **`insertText` 的 replacementRange 有傳給 renderer**（`ImeCommitText(text, replacementRange)`），但在 keyDown 處理中且 range 為 NSNotFound 時會先緩衝；另有一段專為 **Apple Writing Tools** 的 workaround（FB16872510）。→ 用 insertText+replacementRange 改「最近 commit 的字」在 Chromium **理論上可行但未經我們實測**（未能確認穩定度）。
- `validAttributesForMarkedText` = `NSUnderlineStyle`、`NSUnderlineColor`、`NSMarkedClauseSegment`、`NSTextInputReplacementRange`（註解：copied from WebKit）。

---

## 設計啟示

> 以下為「我的評估」，每條附主要依據（卡片編號）。

### 1. 走 IMK、而且「修正發生在 commit 之前」
- **選 IMK 而非 AX**：AX 路線（Cotypist/TabType/Grammarly）要靠宿主暴露文字、游標座標落後（Electron 0.3–0.5s）、要截圖比對字型、Chromium 需開無障礙旗標、終端機/Zed/canvas 不支援（C4、O6、C8）。IMK 由我們自己掌握組字區，能拿到 client 的字型與行矩形（SDK 標頭）。
- **核心原則：只在 marked text（組字區）內改**。改已 commit 的字依賴 TSMDocumentAccess／replacementRange：終端機不可能（iTerm2 TODO）、Office 會壞（vChewing 註解）、Chromium 部分支援（O 補充）。→ 「commit 後修正」只作為**Cocoa/WebKit 白名單內、使用者主動觸發**的進階功能。
- 中文：注音組字本來就在 marked text 內，**LLM 修正 = 改寫組字區內容**（類 Live Conversion，C7）。英文：仿 hallelujahIM 讓「當前單字」留在 marked text、空白/標點時 commit（O3），在 commit 那一刻套用高信心更正；**緊接 Backspace 還原**（LatinIME O7、ATOK C5）——由於還原時字已 commit，需在支援 replacementRange 的 app 才啟用自動更正，其他 app 降級為「只提示不自動改」。

### 2. 演算法：Zenzai 式「辭典 lattice 當 draft，小 LM 只驗證/挑選」
- 以 **McBopomofo Gramambular2（MIT）或 libchewing（LGPL，Rust，Yen's 10-best）** 當 draft 產生器；vChewing 的 Tekkon 規則（ㄅㄨㄥ→ㄅㄥ 等）+ libchewing `FUZZY_CHEWING` + librime `derive/…/correction` 式規則 + **鍵盤鄰鍵通道模型**（azooKey `ZenzaiTypoCandidateGenerator`：鄰鍵 α=2.0×鍵距、漏鍵 β=3.0、對調 γ=2.0；Mozc `TypeCorrectedQuery.score` 相對原樣輸入計分）產生「可能的正確讀音」候選格（O1、O5、O8–O10）。
- 小 LM（**~30–100M 字元級 decoder**，參考 zenz 95M/Q5 73.9MB、Apple 34M）對 draft 做**一次 forward 驗證**；不符則給前綴制約讓 lattice 重搜；**推論次數上限 L=1–2**（Zenzai：L=1 已接近，medium 4.8ms/字）；**原輸入永遠是 baseline**，只有分數差超過門檻才換（azooKey typo、Mozc score）。
- **LM 不得輸出辭典外的字**（Zenzai 的防幻覺性質）→ 天然抑制過度更正；缺詞時寧可不改（接受 Zenzai 的「過度忠實」代價）。
- 句級（標點/停頓時）才考慮較大的模型做「底線提示」，不自動改（Google Pixel GEC、Grammarly、百度 AI 校正：P2、C8、C9）。

### 3. 執行架構與延遲預算
- **同步路徑（<5ms）**：注音組字、Tekkon 規則、lattice 首選 → 立刻 `setMarkedText`，在 `handle` 回傳前完成（azooKey #357/#379）。
- **非同步路徑（目標 p95 < 30ms，硬上限 ~80ms 後丟棄）**：LM 驗證/重排；以 generation token 丟棄過期結果、只回填同一組字段（Squirrel #1154、TabType GenerationGate）；KV 以共同前綴復用（azooKey、TabType）。作者經驗：20–30ms 才順、40ms 是 live conversion 上限（O1）。
- **MVP 先 in-process**（獨立 actor/serial queue + 看門狗），模型單例、預熱、閒置卸載（TabType 15 分鐘）；**XPC/LaunchAgent 拆分留待第二階段**，屆時照抄 azooKey 的補丁清單（暫定 marked text、同步 RPC、session 回收、App Group provisioning、逾時降級）（O1 補充）。
- **GPU/ANE**：llama.cpp Metal 在 IME 程序內已有先例（azooKey 早期）；Core ML/ANE 在 IME 的實際延遲**無公開數據**（待實測）；**不要用 Apple Foundation Models 做逐鍵**（背景+電池 rate limit）。
- **把非 LM 尾延遲算進預算**：Chromium 位置查詢最長 1.5s（同一組字只查一次並快取）、個人化資料鎖（vChewing #616/#620）、權限等待（azooKey #380）。

### 4. UX 規則
- **兩級介入**：(a) 讀音/鍵序層、高信心 → 直接改寫組字區（使用者在 commit 前看得到），以底線顏色或候選窗小字「原：ㄅㄨㄥ」提示（Chromium/WebKit 支援 `NSUnderlineColor`，其他 app 待測；macOS 14 等粗底線會連成一條——vChewing）；(b) 句級/語意層 → 只標示＋候選，不自動改。
- **還原零成本**：組字區內 Esc 或第一次 Backspace = 撤銷 LLM 修正（回到原輸入），第二次才刪字；commit 後緊接 Backspace 還原（僅在可靠 app）。
- **從還原學習**：同一輸入被還原 ≥1 次即停止自動修正（Apple、ATOK、LatinIME）；使用者辭典優先於 LM（Zenzai 的 learningPriority 讓步）。
- **信心門檻分情境**：詞中/詞界、中文/英文、app 類型各自門檻（TabType 0.08/0.15 的思路）；豁免：短字、全大寫、上文出現過的字、程式碼/終端機情境（TabType、Squirrel `app_options`）。
- **預設保守**：Windows 實體鍵盤建議預設關、Gruber 嫌 Cotypist 打擾（C4、C10）→ 首次啟用時只開「讀音層自動 + 句級提示」，句級自動改寫預設關。

### 5. 相容性與安全清單（上線前必測）
- 測試矩陣：TextEdit/Notes（Cocoa）、Safari、Chrome/Arc/Brave（網址列與網頁 textarea）、VS Code（編輯器 + 整合終端機）、Slack/Notion/Discord（Electron）、LINE/微信/ChatGPT 桌面版（vChewing 不相容清單）、Terminal/iTerm2/Ghostty、Word/PowerPoint、JetBrains、Spotlight/Raycast、密碼欄與 `com.apple.SecurityAgent`。參考 VS Code IME Test wiki（含注音 `su3cl3` → 你好）https://github.com/Microsoft/vscode/wiki/IME-Test 。
- 安全：secure input / `AXSecureTextField` 類欄位 / SecurityAgent 時停用模型與學習（O4、O6、O8）；日誌不含文字（azooKey #374）。
- 散佈：arm64、Developer ID 簽章 + notarization + pkg 安裝到 `~/Library/Input Methods` 或 `/Library/Input Methods`、`TISRegisterInputSource`/`TISEnableInputSource`、提示登出；注意 `imklaunchagent` 快取與 kill 次數限制（O3、O8、O9）。
- 授權：zenz 權重 Apache-2.0；McBopomofo MIT；vChewing 詞頻非商用；libchewing LGPL；Squirrel/hallelujah/fcitx5 GPL → 商用產品宜以 McBopomofo/自建詞頻為底。

### O1 再補充：IPA 未踏頁面（已開啟）
- https://www.ipa.go.jp/jinzai/mitou/koubo/career/2024/2024-supercreator-10.html ：「投機的デコーディングなどの工夫により、**GPU前提ながらM1程度の端末でも快適に動作する**」；**Tuner 會收集畫面上的文字建立使用者專屬 LM**（即時反映個人詞彙）；PM 評語提到以 Marisa-Trie 有效率地建 5-gram LM。無延遲/精度數字。
- 對我們：「收集畫面文字做個人化」有隱私與權限（螢幕錄製/AX）成本；我們可只用「使用者在本 IME 內 commit 的文字」做個人化，避免額外權限。

### C1 補充：macOS Sonoma 的實際行為（MacStories macOS Sonoma review，已開啟）
- https://www.macstories.net/stories/macos-sonoma-the-macstories-review/11/
- **inline 預測只在「標準 macOS 文字欄位」可用；Safari、Safari 系 web app 與 Electron app（Obsidian、Notion、Discord、Slack）不支援**；接受鍵：Space、Tab 或 Return。
- **autocorrect**：英/法/西文鍵盤用新 transformer 模型；被改的字「temporarily underlined in the app's accent color」；**點該字跳出 popup 可還原原字**。
- 對我們：連 Apple 自己的系統級 inline 功能都避開 Safari/Electron → 我們在這些 app 應準備「浮動組字窗/候選窗內呈現」的降級路徑（與 vChewing 對 Electron 的預設一致）。

### IMK 補充：安裝位置與 Info.plist 的一手依據（macOS 27.0 SDK `HIToolbox/TextInputSources.h`）
- `TISRegisterInputSource()` 文件：只能註冊「**`/Library/Input Methods/` 或 `~/Library/Input Methods/`** 內的輸入法 bundle（分別給所有使用者/目前使用者）」以及 Keyboard Layouts；輸入法 bundle 可內含私有、不可選的鍵盤配置供 `TISSetInputMethodKeyboardLayoutOverride` 使用，會隨輸入法自動註冊；「Any code that calls TISRegisterInputSource is part of an application or service that has already been validated in some way (e.g. by the user).」
- 輸入法一律以 bundle 形式存在（Leopard 起為 `<domain>/Library/Input Methods/` 中的 application bundle）；Info.plist 頂層新增 `TISInputSourceID`（通常等於 BundleID，省略則用 BundleID）與 `TISIntendedLanguage`。
- 各專案實際使用的 Info.plist 鍵（讀碼彙整）：`InputMethodConnectionName`（建議 `$(PRODUCT_BUNDLE_IDENTIFIER)_Connection`）、`InputMethodServerControllerClass`、`InputMethodServerDelegateClass`、（vChewing）`InputMethodServerDataSourceClass`、`ComponentInputModeDict`/`tsInputModeListKey`（多模式，如繁/簡）、`tsInputModeScriptKey`、`tsInputModePrimaryInScriptKey`、`TISIntendedLanguage`、`TICapsLockLanguageSwitchCapable`、`TISIconIsTemplate`、`TISParticipatesInTouchBar`、`TISDoubleSpaceSubstitution`（McBopomofo：`。`）、`LSUIElement` / `LSBackgroundOnly`、`NSPrincipalClass`（azooKey/fcitx5 用自訂 `NSManualApplication`）。

### C3 補充：macOS 26 的 Writing Tools
- Pixel Envy 連結文（WWDC 示範，作者未實測）https://pxlnv.com/linklog/macos-tahoe-writing-tools/ （搜尋結果）：新版不再用 popover，但 Rewrite 仍就地改寫、看不出差異。**Proofread 是否支援逐項接受/拒絕：未能確認**。

---

## 開源「LLM 輸入法」專案群（2025–2026；GitHub API 搜尋 + README/讀碼；子代理整理、關鍵數字我已抽查）

> 搜尋：GitHub repositories 搜「LLM input method」「llm ime」「AI输入法」「rime llm」「fcitx5 llm」「ibus llm」。clone 於 `scratchpad/agentD_src/llm_ime/`。

### 卡片 O11：樹懶注音 sloth-zhuyin-linux【最接近我們的注音前例】
- **link**：https://github.com/vieenrose/sloth-zhuyin-linux ；模型 https://huggingface.co/Luigi/sloth-ime-models （README 連結）
- **是什麼**：fcitx5/IBus（經本機 daemon）、Android NDK、WASM 的注音輸入法；**每個按鍵都對整句注音做免選字轉換**（新注音式），commit 後預測下一詞；中英自動切分。
- **關鍵做法/數字（README 與 docs 抽查確認）**：**12M 三值（ternary, TQ2_0）雙向 encoder、9.7MB GGUF、自寫 ggml forward**；每個位置**只在該讀音的合法字中選**（phonetic-legality mask；早期版用 GBNF 文法限制在 libchewing 候選內）；**錯字修復：不可能的音節由模型修正**；訓練噪聲從「任意編輯距離 1」改為只取**大千排列 QWERTY 8 鄰鍵**（每音節模擬錯誤 21.8 → 8.9 種）（`docs/RELATED-WORK.md`）。**延遲：BOOX 平板（Snapdragon 662）每 6 音節一次 forward 9.3ms（4 執行緒）/ 15.8ms（2 執行緒），預算 ≤20ms**；25M 版 18.5ms。**準確度**：500 句 held-out（c4-zh-TW）整句全對 76%（25M）；230 句裝置實測 84%（12M）；有聲調同音難句逐字 86%（libchewing 71%）。作者自陳：天花板是微軟新注音／自然輸入法、樓板是 libchewing；（子代理摘錄）通用小 LLM 小測 Qwen2.5-0.5B 67%、1.5B 83%、LFM2.5-230M 95%，但 230M 在真實測試集首選準確率仍輸 libchewing，才改做專用 encoder；曾發現測試集洩漏到訓練資料。
- **跟我們的關係**：證明「**專用、極小（12M）、受讀音合法性約束的模型**」就能在**低階手機 CPU 上 <20ms/鍵**做注音整句轉換與錯字修復——在 M4 上只會更快。是比通用 LLM 更正確的方向。
- **侷限**：無授權檔（**不可直接重用**）；3★、v0.2.0；Linux/Android 為主，非 macOS IMK；英文錯字未處理。

### 卡片 O12：zhanghaozhecn/rime-llm-rerank & rime-llm-ime（Windows 小狼毫）
- **link**：https://github.com/zhanghaozhecn/rime-llm-rerank ；https://github.com/zhanghaozhecn/rime-llm-ime
- **做法**：每鍵用 **Qwen3.5-0.8B Q4_K_M（508MB，llama.cpp CPU）** 對 Rime 候選**計分重排、從不生成**；所有候選共用左文脈 → 最多 3 次 decode；左文脈由 **UIA TextPattern / WPS COM**（或 TSF 事件）讀取。
- **數字（README，硬體未註明）**：上一次 commit 時已預處理文脈約 **43ms/鍵**、未預處理約 80ms、逐候選天真做法 274ms；RAM ~1GB；**正確字排第一 93.4%（2 萬樣本），模型上限約 94.3%；換 2B 只 +0.3 分卻 3 倍延遲**；**GPU 版因 CUDA 重編、電源狀態抖動與 hang 被放棄**。
- **啟示**：「打分而非生成」「commit 時預先處理文脈」「更大模型邊際效益極低」。

### 卡片 O13：macOS IMK 上的 LLM 輸入法實作經驗（ds-input、AllInOneIME、FuJiK0）
- **ds-input**（https://github.com/madeye/ds-input ，MIT，macOS IMK + Windows TSF，Rust core）：無聲調拼音整句交給雲端 LLM（預設 deepseek），debounce 100ms、逾時 8000ms、SSE 串流；本機 bigram 先給即時猜測；一次只一個請求 + generation counter 丟棄過期結果。**其 CLAUDE.md「Things that bite」（我已讀原文）**：**bundle ID 與每個 `TISInputSourceID` 都必須含 `.inputmethod.` 片段**（如 `io.github.madeye.inputmethod.dsinput`），否則 `TISRegisterInputSource()` 回 `noErr` 但輸入來源**永遠不出現在設定中**（不論簽章、公證、位置）；bundle 必須 **Developer ID 簽章**（ad-hoc 可啟動但不會出現）；`tsInputMethodIconFileKey` 指到的圖示檔必須存在；新輸入來源**需登出再登入**才會被掃描到。
- **AllInOneIME**（https://github.com/MicroZ-LLC/AllInOneIME ，FSL-1.1-ALv2，macOS IMK + librime/rime-ice，已公證）：`@improve/@translate/@question` 等指令按 ⏎ 才送雲端（Bedrock Claude Haiku 4.5 等）或 Apple Foundation Models，逾時 15s。**原始碼註解（我已讀）**：用 `insertText` + `replacementRange` 回頭改字「NSTextView, WebKit, Chromium and Electron do」honor；**只在「游標位置可回報且就在已打文字末端、且 `attributedSubstring` 讀回與打出的一致」的欄位才做，每一步前都驗證；終端機永不做；不遵守替換的 app 會被記住（TypoMemory）不再嘗試**；並說明「以合成 ⌫ 鍵代替需要 Accessibility 權限、且可能在下一個字之後才到達 app」故不採用。子代理另摘：Apple 端側模型上下文約 4K tokens（macOS 27 為 8K）、rate limit/並發時回 busy、會把拼音誤判為印尼文/越南文（**未逐字核對**）。
- **FuJiK0/rime-llm-reranker**（https://github.com/FuJiK0/rime-llm-reranker ，MIT）：Squirrel + librime-lua 呼叫 oMLX 伺服器（Qwen3.5-0.8B-MLX-bf16）重排前 5 候選；README 宣稱 ~1ms（P99<5ms），但子代理讀碼發現以 Lua `os.clock()`（CPU 時間）量測阻塞 HTTP → **實際延遲未能確認**。唯一的 Apple Silicon/MLX 例子。

### 卡片 O14：其他 LLM-IME（簡表）
| 專案 | 平台 | LLM 角色 | 模型/執行 | 約束 | 延遲/逾時 | 教訓 |
|---|---|---|---|---|---|---|
| xushengfeng/lime（GPL-3.0，892★）https://github.com/xushengfeng/lime | Rime-Lua + 本機 Deno HTTP | 逐 token 產生拼音候選、預測 | Qwen3-0.6B IQ4_XS（node-llama-cpp，CPU） | **logit 層同音遮罩**（只留拼音相符 token） | 無數字；Lua 每鍵同步 curl | README 自承長句弱、打太快會掉字、重啟失憶；多字 token 扭曲機率比較 |
| scukeqi/Wisdom-Weasel（GPL-3.0，372★）https://github.com/scukeqi/Wisdom-Weasel | Windows 小狼毫 | commit 後預測下一詞 | OpenAI 相容 API / llama.cpp / HF（Qwen3-4B-Base beam 10） | HF 版用拼音首字母過濾 logits | HTTP 逾時 10s；使用者稱 4B ≤200ms（硬體未知） | 背景執行緒 + 請求序號 |
| Shan-HIT/HuoziIME（GPL-3.0）https://github.com/Shan-HIT/HuoziIME ；論文 https://arxiv.org/abs/2604.14159 （ACL 2026 Demo，子代理開啟） | Android（YuyanIME fork） | 個人化片語補全（非更正） | Qwen3-0.6B 系 Q4_0 485MB，llama.cpp JNI | 無 | Dimensity 9000：prefill >260 tok/s、生成 24–25 tok/s、冷啟首候選 800–1700ms、峰值 RAM ~1.12GB | 前綴快取宣稱打字中「近零」但無消融 |
| fukuyori/rakukan（MIT）https://github.com/fukuyori/rakukan | Windows TSF（Rust）+ 獨立引擎程序 | 日文 live conversion + 變換 | karukan「jinen」28–88MB，llama.cpp | 自由生成 + 辭典/歷史優先排序 | live debounce 80ms；inline 等待由 3–15s 砍到 250ms | **引擎 DLL 載入宿主程序曾讓 Zoom/Dropbox/Explorer 崩潰 → 拆程序**；Enter 曾 commit 使用者沒看到的結果 → 只 commit 已顯示內容；同步重載 keymap 造成 931ms 卡頓 |
| BonoJovi/Bonolith（MIT）https://github.com/BonoJovi/Bonolith | Linux IBus/Fcitx5（Rust） | 背景重排辭典候選 | Qwen2.5-1.5B Q4_K_M（llama-server CPU） | 文法強制逐字輸出候選並讀 log-prob | 程式註解「20-40ms」；逾時 500/1500ms、失敗冷卻 30s | **程序內 llama.cpp segfault → 改 server**；冷啟 server 讓空白鍵卡數秒；一次 503 曾讓整個 session 停用 LLM |
| ximeiorg/Xime（GPL-3.0，1225★）https://github.com/ximeiorg/Xime | Android/Rime | commit 後下一詞預測 | 自訓 10M/23M Transformer（ONNX INT8） | — | 未能確認 | 首選準確 29.78%/39.02% |
| gaboolic/moqi-im-windows（MIT）https://github.com/gaboolic/moqi-im-windows | TSF + Go | 熱鍵送雲端 | 雲端 chat API | 只靠 prompt 要求忠於拼音 | 同步呼叫、20s 逾時 | — |

**歸納（子代理 + 我的評估）**：(1) 能進「每鍵路徑」的都把 LLM 限制在**辭典/讀音合法候選內「打分」**（lime、sloth、zhanghaozhecn、Bonolith），自由生成者（ds-input、rakukan、moqi）都要額外補 parse/降級/「只 commit 已顯示」規則；(2) **專用小模型（sloth 12M、rakukan 28–88MB、Zenzai 26–95M）勝過通用 0.5–2B LLM**；(3) **程序內 llama.cpp/引擎 DLL 的穩定性問題**被多個專案回報（Bonolith segfault、rakukan 崩宿主）→ 支持「IME 程序要能承受推論端崩潰」；(4) **沒有任何開源專案修正「已 commit 的文字」**（AllInOneIME 的 verify-then-replace 是最接近的工程樣板）；(5) **沒有 Apple M 系列上的可信逐鍵延遲數字**（FuJiK0 的 1ms 不可信）→ 需要我們自己量。

### 延遲表補充（開源 LLM-IME，接續上方「神經網路/LLM 輸入法前例與實測延遲」表）
| # | 系統 | 任務 | 模型 / 大小 | 執行 | 延遲數字 | 硬體 | 來源性質 |
|---|---|---|---|---|---|---|---|
| 15 | 樹懶注音 sloth | **注音整句轉換 + 錯字修復（每鍵）** | 12M ternary encoder，9.7MB | 自寫 ggml forward，CPU | **6 音節一次 9.3ms（4 執行緒）/ 15.8ms（2 執行緒）**；25M 版 18.5ms；預算 ≤20ms | Snapdragon 662（BOOX 平板） | 【README 實測】 |
| 16 | rime-llm-rerank | 候選重排（每鍵） | Qwen3.5-0.8B Q4_K_M 508MB | llama.cpp CPU | **~43ms/鍵**（文脈已預處理）、~80ms（未預處理）、274ms（天真做法） | 未註明 | 【README】 |
| 17 | HuoziIME | 個人化補全 | Qwen3-0.6B 系 Q4_0 485MB | llama.cpp（Android） | prefill >260 tok/s、生成 24–25 tok/s、**冷啟 800–1700ms** | Dimensity 9000 | 【論文/README】 |
| 18 | Bonolith | 背景重排 | Qwen2.5-1.5B Q4_K_M | llama-server CPU | 「20-40ms」（程式註解） | 未註明 | 【讀碼，未證實】 |
| 19 | ds-input | 雲端整句轉換 | deepseek（雲端） | HTTP SSE | debounce 100ms、逾時 8s | — | 【讀碼】 |
| 20 | rakukan | 日文 live conversion | 28–88MB | llama.cpp，獨立程序 | debounce 80ms；inline 等待上限 250ms | — | 【README】 |

**補充解讀**：sloth（12M、手機 CPU 9.3ms）與 Zenzai（26–95M、M2 Pro GPU 數 ms/字）兩個獨立案例都指向：**專用小模型 + 讀音約束 → 每鍵 <20ms 可達**；通用 0.6–1.5B 模型每鍵 40–80ms 起跳、冷啟可達秒級（HuoziIME），只適合非同步/背景重排。

### IMK 限制補充（來自 LLM-IME 專案）
- **輸入來源 ID 規則**：bundle ID 與每個 `TISInputSourceID` 須含 `.inputmethod.` 片段，否則 `TISRegisterInputSource()` 回 `noErr` 但永遠不出現；**ad-hoc 簽章可啟動但不會出現在設定**，需 Developer ID；icon 檔必須存在；新來源需登出登入（ds-input CLAUDE.md，已讀原文）。→ 與 Squirrel（`im.rime.inputmethod.Squirrel`）、vChewing（`org.atelierInmu.inputmethod.vChewing`）、azooKey（`dev.ensan.inputmethod.azooKeyMac`）命名一致。
- **改已 commit 文字的安全樣板（AllInOneIME）**：`insertText(_:replacementRange:)` 前先確認「client 回報的游標就在剛打字的末端、`attributedSubstring` 讀回內容一致」，每步驗證；**終端機禁用**；替換失敗的 app 記入黑名單；不用合成 ⌫（需 AX 權限且有時序問題）。
- **Apple Foundation Models 實務限制**（AllInOneIME 經驗，子代理摘錄、未逐字核對）：上下文約 4K tokens（macOS 27 為 8K）、rate limit 或並發時回 busy。

### C7 補充：Apple 日文輸入法也有「Correct spelling」
- Apple 日文輸入法設定中有「Correct spelling」選項：自動更正日文拼字錯誤或在輸入時建議候選（Apple 支援頁，搜尋結果摘要）https://support.apple.com/guide/japanese-input-method/jpim662a12b9/mac 。→ Apple 對日文/拼音都有輸入層容錯，**唯獨注音沒有**（C6）。

### 設計啟示補充（整合 LLM-IME 專案群 O11–O14 後）
6. **模型選型**：逐鍵路徑用**專用小模型（12M–100M）**——sloth 12M ternary 在手機 CPU 9.3ms、Zenzai 26–95M 數 ms/字；**不要**把 0.6–4B 通用 LLM 放進逐鍵同步路徑（40–100ms 起跳、冷啟秒級）；通用 LLM 最多做背景重排（rime-llm-rerank 證明 2B 比 0.8B 只多 0.3 分）或按需整句潤飾。
7. **CPU 優先值得實測**：≤30M 模型在 M4 CPU 上可能就 <5ms（推論：sloth 在 SD662 上 9.3ms），可避開 Metal 相關問題（azooKey 記錄 llama.cpp b9637 Metal shutdown assertion；rime-llm-rerank 因 GPU 電源狀態抖動/hang 放棄 GPU 版——但那是 Windows/CUDA）。Apple GPU/ANE 喚醒延遲在 IME 情境的數據：**未找到**，列為必測項。
8. **錯字通道模型**：訓練噪聲與候選生成都限定在**實際鍵盤鄰鍵**（sloth：大千 8 鄰鍵，21.8→8.9 候選/音節；azooKey：Mac QWERTY 鍵距 ≤1.65），再加 Tekkon 規則與聲調/韻母混淆表；所有候選都要通過「讀音合法 + 辭典存在」閘門。
9. **只 commit 使用者看過的內容**（rakukan 教訓）：非同步 LLM 結果若在 Enter/空白前一刻才到，不得直接 commit；必須先顯示（至少一個 frame）或延到下一次。
10. **commit 時預處理文脈**（rime-llm-rerank：預處理後 43ms vs 80ms）＋ KV 前綴快取（azooKey、TabType）。
11. **推論端崩潰隔離**：多個專案回報程序內 llama.cpp/DLL 崩潰（Bonolith segfault、rakukan 讓宿主崩潰）。IMK 輸入法是獨立程序（不會像 Windows TSF DLL 一樣把宿主拖垮），但輸入法本身崩潰仍會讓使用者短暫無法打字 → 推論層應可被熔斷（例外/逾時即停用 N 分鐘、回退純辭典），而非整個 session 停用（Bonolith 的 503 教訓）。
12. **改已 commit 文字**只用 AllInOneIME 式 verify-then-replace + app 黑名單，且限 Cocoa/WebKit/Chromium/Electron、永不在終端機。
13. **安裝識別**：bundle ID / `TISInputSourceID` 含 `.inputmethod.`、Developer ID 簽章 + 公證、安裝後登出（ds-input、Squirrel、vChewing）。
