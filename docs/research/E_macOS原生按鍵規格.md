# 主題 E：macOS 內建輸入方式（注音－繁體／ABC）按鍵行為規格（研究筆記，逐步累加）

- 研究日期：2026-10-17
- 研究機器：MacBook Air M4，macOS 27.0.1（Build 26A434）
- 本機已啟用輸入方式（`defaults read com.apple.HIToolbox` 唯讀讀取）：ABC（KeyboardLayout ID 252）、`com.apple.inputmethod.TCIM.Zhuyin`（注音－繁體）；目前選用注音。
- 撰寫原則：每列都附來源。來源分三類：
  - 【Apple】support.apple.com 官方文件（URL）
  - 【本機】本機系統檔（路徑＋鍵名；唯讀檢查，未改任何設定）
  - 【非 Apple】第三方（vChewing 威注音、McBopomofo 小麥注音、台灣教學文章等），明確標示
- 「未能確認」= 找不到可信來源或本機證據；一律附「在 Mac 上手動驗證步驟」。
- 「處理方式」欄：
  - **IME 自行處理**＝我們的輸入法要在 `handle(_:client:)` 中消化事件（回傳 true）
  - **放行**＝回傳 false，讓 App／系統文字系統處理
  - **先送出再放行**＝若有組字區，先 commit（或依原生行為處理組字區），再把按鍵交給 App

---

## 0. 本機證據摘要（之後各表會引用代號）

| 代號 | 本機檔案／指令 | 重點內容 |
|---|---|---|
| L1 | `/System/Library/Input Methods/TCIM.app/Contents/PlugIns/TCIM_Extension.appex/Contents/Info.plist`（`plutil -p`） | 輸入模式 `com.apple.inputmethod.TCIM.Zhuyin`（注音）、`…ZhuyinEten`（倚天注音）、`…Pinyin` 等；每個模式 `TISDoubleSpaceSubstitution = "。"`；`tsInputModeKeyEquivalentKey` = Z／E／P／D／C／S／H，`tsInputModeKeyEquivalentModifiersKey = 4608`（= Carbon controlKey 4096 + shiftKey 512 → Control-Shift-字母）；`TICapsLockLanguageSwitchCapable = true`；`InputMethodServerControllerClass = TCIMInputController`（IMK 架構）；可見模式順序 `tsVisibleInputModeOrderedArrayKey` 不含 ZhuyinEten |
| L2 | `/System/Library/PrivateFrameworks/CoreChineseEngine.framework/Versions/A/Resources/preferences.plist` | 預設值：`AdjustWordFrequencyZhuyin = true`、`ZhuyinCandidateWindowDirection = 1`、`candidateWindowDirection = 0`、`showAssociatedWords = true`、`usesHalfwidthPunctuation = false`、`usesLastUsedLatinKeyboardLayout = true`、`TCIMShowsSimplifiedCharacters = false`、`fontSize = 16`、`autocorrectionEnabled = true`、`fuzzyPinyinEnabled = false` |
| L3 | 同上 `Base.lproj/CIMMenuController.nib`（以自寫唯讀 NIBArchive 解碼器 `scratchpad/tools/nibdump.py` 解出） | 輸入法選單項目與快捷鍵：Show Facemarks & Punctuation = ⌥⇧B；Show Structural Candidates = ⇧Space；Select Next Candidate Sorting Mode = Tab；Select Previous Candidate Sorting Mode = ⇧Tab；Select Next Tone = ⌥Tab；Select Previous Tone = ⌥⇧Tab；Forget Selected Word = ⇧Delete；Find Input Code = ⌥⇧L；Convert Text to Traditional Chinese = ⌃⇧⌘C；Convert Text to Simplified Chinese = ⌃⌥⇧⌘C；Use Halfwidth Punctuation = ⌥⇧H；Help = ⌥⇧E；另有 Convert Text to Full Width／Half Width、Show Substitution Candidates（無快捷鍵） |
| L4 | 同上 `Base.lproj/TCIMZhuyinPreferencesModule.nib` ＋ `TCIMZhuyinPreferencesModule.loctable` | 注音設定項只有：「調整用字頻率」(Adjust word frequency → `preferences.adjustsWordFrequencyZhuyin`)、「顯示關聯字」(Show predictive completions → `preferences.showsAssociatedWords`)、候選字視窗「方向：水平／垂直」(`preferences.candidateWindowOrientationTCIM`，tag 0=水平、1=垂直)、「字體大小」14/16/18/24/36 (`preferences.TCIMFontSize`) |
| L5 | `/System/Library/ExtensionKit/Extensions/KeyboardSettings.appex/Contents/Resources/Localizable.loctable` | 系統設定「鍵盤」字串：`Press 🌐︎ key to`（按下🌐︎鍵來）／`Press fn key to`／`Press and hold fn key to`；選項 `Change Input Source`、`Show Emoji & Symbols`、`Start Dictation (Press 🌐︎ Twice)`、`Do Nothing`；`Use the Caps Lock key to switch to and from %@`（使用大寫鎖定鍵來切換「%@」及目前輸入方式）、`…from last used Latin input source`；泛化字串 `Use the %1$@ key to switch to and from %2$@`；`Press and hold to enable typing in all uppercase.`；文字輸入區：`Correct spelling automatically`、`Capitalize words automatically`、`Add period with double-space`（zh_TW 介面字串為「加入全形句號」）、`Use smart quotes and dashes`、`Show inline predictive text`（顯示內文預測字詞）、`Show typing suggestions`、`Show suggested replies`、`Automatically switch to a document’s input source` |
| L6 | 以自寫唯讀 Swift 工具 `scratchpad/tools/uchrdump.swift`（`TISCreateInputSourceList`＋`UCKeyTranslate`，不選取／不啟用任何輸入方式）讀出 `com.apple.keylayout.ZhuyinBopomofo`、`com.apple.keylayout.ZhuyinEten`、`com.apple.keylayout.ABC`、`com.apple.keylayout.US` 的按鍵表 | 見第 2 節完整對照 |
| L7 | `…/TCIM_Extension.appex/Contents/Resources/ZhuyinEten.keylayout`（XML，以 `scratchpad/tools/keylayout_dump.py` 解析） | 倚天鍵盤 modifierMap：index0=無修飾、1=Shift+Caps、2=Caps、3=Command、4=Option、5=Shift+Option、6=Control、7=Shift、8=Caps+Command…；結果與 L6 一致 |
| L8 | `defaults read com.apple.HIToolbox`（本機目前值，非預設值） | `AppleFnUsageType = 2`；`AppleCapsLockPressAndHoldToggleOff = 0`；`TextInputGlobalPropertyPerContextInput = 1`；`AppleCurrentKeyboardLayoutInputSourceID = com.apple.keylayout.ZhuyinBopomofo` |
| L9 | `…/CoreChineseEngine.framework/…/Resources/CIMPunctuationCandidates.plist`、`CIMSortedTraditionalPunctuation.plist`、`FullwidthToHalfwidthPunctuationMapping.plist`、`CIMDefaultTraditionalPunctuation.plist` | 標點替代候選（例：「，」的替代為 、　《〈＜；「「」的替代為『【〖〔［｛；「-」的替代為＿—－−）；全形→半形對照（，；：！？。（）【】｜～ → , ; : ! ? . ( ) [ ] \| ~）；符號表分類（音：ˉˊˇˋ˙；標點；貨幣；括號…） |
| L10 | `…/KeyboardSettings.appex` 等處未見「Shift 切換中英」的字串；TCIM 注音設定 nib（L4）也沒有 Shift 相關選項 | 用來支持「原生注音沒有 Shift 切換中英設定」的推論（仍需手測，見第 1 節） |
| L11 | Apple 說明快取（helpd 從 support.apple.com 下載的同一份內容，唯讀解碼，工具 `scratchpad/tools/cshelpindex_dump.py`）：`~/Library/Caches/com.apple.helpd/Generated/chinese-input-method/en_US.cshelpindex`、`…/zh_TW.cshelpindex`（**macOS 27 版**《中文和粵語輸入法使用手冊》全文）；`~/Library/Caches/com.apple.helpd/Generated/com.apple.machelp*27.0.1/zh_TW.cshelpindex`（**macOS 27 版**《Mac 使用手冊》） | 研究期間 support.apple.com 間歇回 503/504，故以本機快取逐字核對下列 A-／M- 代號頁面；URL 一併列出 |
| L12 | `/System/Library/Frameworks/AppKit.framework/Resources/StandardKeyBinding.dict`（Cocoa 文字系統預設按鍵綁定） | 例：`^a`→moveToBeginningOfParagraph:、`^e`→moveToEndOfParagraph:、`^k`→deleteToEndOfParagraph:、`^y`→yank:、`^h`→deleteBackward:、`^d`→deleteForward:、`^f/^b/^n/^p`→移動、`^o`→插入換行並後退、`^t`→transpose:、`^l`→centerSelectionInVisibleArea:、`^v`→pageDown:、`~←/~→`→moveWordLeft/Right:、`~Delete`→deleteWordBackward:、`@←/@→`→行首/行尾、`@↑/@↓`→文件首/尾、`@Delete`→deleteToBeginningOfLine:、`Esc`→cancelOperation:、`~Esc`→complete:、`^'`/`^"`→插入直引號（不做智慧替換）、`@Space`→cycleToNextInputScript:、`@~Space`→cycleToNextInputKeyboardLayout: |
| L13 | `defaults read com.apple.symbolichotkeys AppleSymbolicHotKeys`（唯讀） | ID 60 =（32, 49, 262144）→ ⌃Space；ID 61 =（32, 49, 786432）→ ⌃⌥Space；ID 64 = ⌘Space（Spotlight）；ID 65 = ⌘⌥Space |
| L14 | `defaults read -g`（本機目前值） | `NSAutomaticCapitalizationEnabled=1`、`NSAutomaticDashSubstitutionEnabled=1`、`NSAutomaticPeriodSubstitutionEnabled=1`、`NSAutomaticQuoteSubstitutionEnabled=1`；`NSAutomaticSpellingCorrectionEnabled`、`NSAutomaticInlinePredictionEnabled` 未設（=系統預設） |
| L15 | `/System/Library/Input Methods/` 內 Apple 輸入法 bundle 的 Info.plist（TCIM、SCIM、KIM、日文兩種、越南、坦米爾、音譯、TYIM；不含 50onPalette、CharacterPalette 等非鍵盤 IM） | 皆宣告 `TICapsLockLanguageSwitchCapable = true`；TCIM 各模式宣告 `TISDoubleSpaceSubstitution = "。"` |
| L16 | SDK 標頭 `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/…/InputMethodKit.framework/Headers/IMKInputController.h`、`…/AppKit.framework/Headers/NSSpellChecker.h` | IMK：`handleEvent:client:`／`inputText:…`／`didCommandBySelector:client:`（文件建議：組字區空時回傳 NO 讓 App 收到 Return）、`commitComposition:`（client 要立即結束組字時呼叫）、`recognizedEvents:` 預設只有 NSKeyDownMask（只收 keyDown 時，點組字區外會自動收到 commitComposition:）；NSSpellChecker 類別屬性 `isAutomaticSpellingCorrectionEnabled`、`isAutomaticTextReplacementEnabled`、`isAutomaticQuoteSubstitutionEnabled`、`isAutomaticDashSubstitutionEnabled`、`isAutomaticCapitalizationEnabled`、`isAutomaticPeriodSubstitutionEnabled`、`isAutomaticTextCompletionEnabled`、`isAutomaticInlinePredictionEnabled`(macOS 14+) |

### 0.1 Apple 官方文件代號（內容已以 L11 本機快取核對；zh-tw 版網址把 `/guide/` 前加 `zh-tw/` 即可）

| 代號 | 頁面 | URL |
|---|---|---|
| A-ZY | 在 Mac 上使用繁體注音輸入中文（cimzt15531） | https://support.apple.com/guide/chinese-input-method/cimzt15531/mac |
| A-ET | 在 Mac 上使用繁體倚天注音輸入中文（cimzet40713） | https://support.apple.com/guide/chinese-input-method/cimzet40713/mac |
| A-CW | 使用候選字視窗來輸入中文或粵語（cim12992） | https://support.apple.com/guide/chinese-input-method/cim12992/mac |
| A-LAT | 使用中文輸入法來輸入拉丁字元（cim11841） | https://support.apple.com/guide/chinese-input-method/cim11841/mac |
| A-PUN | 輸入中文標點符號和特殊字元（cim12987） | https://support.apple.com/guide/chinese-input-method/cim12987/mac |
| A-SET | 更改中文和粵語輸入方式設定（cim21aa5fa50） | https://support.apple.com/guide/chinese-input-method/cim21aa5fa50/mac |
| A-SW | 切換至中文或粵語輸入方式（cim119a8d473） | https://support.apple.com/guide/chinese-input-method/cim119a8d473/mac |
| A-KEY | 繁體中文輸入法鍵盤快速鍵（cimkeyt36646） | https://support.apple.com/guide/chinese-input-method/cimkeyt36646/mac |
| A-PY | 使用繁體拼音輸入中文（cimpyt11823） | https://support.apple.com/guide/chinese-input-method/cimpyt11823/mac |
| A-CONV | 轉換中文或拉丁字元（mchlp2865） | https://support.apple.com/guide/chinese-input-method/mchlp2865/mac |
| A-DICT | 編輯中文使用者辭典（cim165f83982） | https://support.apple.com/guide/chinese-input-method/cim165f83982/mac |
| M-IS | Mac 使用手冊：更改「輸入方式」設定（mchl84525d76） | https://support.apple.com/guide/mac-help/mchl84525d76/mac |
| M-KB | Mac 使用手冊：Mac 上的「鍵盤」設定（kbdm162） | https://support.apple.com/guide/mac-help/kbdm162/mac |
| M-LANG | Mac 使用手冊：以另一種語言撰寫（mchlp1406） | https://support.apple.com/guide/mac-help/mchlp1406/mac |
| M-TYPE | Mac 使用手冊：取得輸入建議和修正錯誤（mchlp2299；已用 WebFetch 確認英文版為 macOS 27） | https://support.apple.com/guide/mac-help/mchlp2299/mac |
| M-SUB | Mac 使用手冊：取代文件中的文字和標點符號（mh35735） | https://support.apple.com/guide/mac-help/mh35735/mac |
| M-ACC | Mac 使用手冊：輸入帶有重音符號的字元（mh27474） | https://support.apple.com/guide/mac-help/mh27474/mac |
| M-EMO | Mac 使用手冊：使用表情符號和其他符號（mchlp1560） | https://support.apple.com/guide/mac-help/mchlp1560/mac |
| M-MOD | Mac 使用手冊：更改變更鍵的動作（mchlp1011） | https://support.apple.com/guide/mac-help/mchlp1011/mac |
| K-SC | Apple 支援文章：Mac 鍵盤快速鍵（102650，WebFetch 取得） | https://support.apple.com/en-us/102650 |
| D-EV | Apple Developer（封存）：Cocoa Event Handling Guide – Event Architecture（Command 組合鍵的 key equivalent 先於 first responder 的 keyDown:） | https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/EventArchitecture/EventArchitecture.html |

### 0.2 非 Apple 來源代號（僅作補充，明確標示）

| 代號 | 來源 |
|---|---|
| N-MCB | 【非 Apple】小麥注音 McBopomofo《使用手冊》（GitHub wiki）https://github.com/openvanilla/McBopomofo/wiki/使用手冊 |
| N-MCB2 | 【非 Apple】小麥注音〈寫給 macOS 繁體中文注音輸入法用戶〉：macOS 內建注音的頓號在 `\` 鍵 https://github.com/openvanilla/McBopomofo/wiki/使用說明_寫給_macOS_繁體中文注音輸入法用戶 |
| N-VCH | 【非 Apple】vChewing 威注音 README：「Shift 中英模式切換功能要求至少 macOS 10.15」https://github.com/vChewing/vChewing-macOS |
| N-RAY | 【非 Apple】個人部落格（2020-01-22，未註明 macOS 版本）：稱 Mac 預設中文輸入下 Caps Lock 打出全形小寫、Shift 打出全形大寫 https://israynotarray.com/other/20200122/3909813080/ |
| N-UPT | 【非 Apple，未核對】uptogo.com.tw 標點教學（搜尋摘要稱：中文輸入下 Shift+/ 問號、Shift+1 驚嘆號、Shift+, 逗號、Shift+. 句號為全形；頓號說法互相矛盾）https://uptogo.com.tw/?p=1164751 — 原文回 HTTP 403 無法開啟，**僅供參考、不作為任何一列的依據** |
| N-DEVF | 【非 Apple】Apple Developer Forums thread 71279（2017，zonble 詢問第三方 IME 為何沒有 Caps Lock 切換選項，無回覆）https://developer.apple.com/forums/thread/71279 |

---

## 1. 輸入方式切換

| 按鍵/組合 | macOS 原生行為（注音：組字區有/無；英文 ABC 模式） | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| 🌐︎／fn 單按 | 依「系統設定 > 鍵盤 >『按下 🌐︎ 鍵來』（無地球鍵的鍵盤為『按下 fn 鍵來』）」四選一：①**更改輸入方式**：切到下一個輸入方式並在輸入位置附近列出所有輸入方式，連按繼續往下選；②**顯示表情符號與符號**（字元檢視器）；③**啟動聽寫**（按兩下）；④**不執行任何動作**。注音與 ABC 相同 | M-KB、A-SW、M-LANG、L5 | 放行（系統層級） | 組字區有內容時被切走的處理：**未能確認**（T1-1）。本機 `AppleFnUsageType = 2`（L8），整數↔選項對照 Apple 未公開：**未能確認**（測試：逐一切換此設定後執行 `defaults read com.apple.HIToolbox AppleFnUsageType` 比對）。macOS 27 另有「Press and hold fn key to／按住 fn 鍵來」字串（L5），文件未說明：**未能確認**（測試：系統設定 > 鍵盤 查看此選單的選項並實按） |
| 🌐︎+E／fn+E、⌃⌘Space | 打開字元檢視器；先打字再按 fn/🌐︎+E 可用表情符號建議取代剛打的字 | M-EMO、K-SC | 放行 | 組字區有內容時：**未能確認** |
| ⌃Space | 選取「上一個」輸入方式；按住 Control 連按 Space 會在輸入位置附近顯示全部輸入方式供選 | A-SW、M-LANG、K-SC、L13（ID 60 = ⌃Space） | 放行（系統符號快速鍵，可於 鍵盤快速鍵 > 輸入方式 修改） | 組字區有內容時：**未能確認**（T1-1） |
| ⌃⌥Space | 選取輸入法選單中的「下一個」輸入方式 | A-SW、M-LANG、L13（ID 61 = ⌃⌥Space） | 放行 | 同上 |
| Caps Lock 短按（已開「使用大寫鎖定鍵來切換『ABC』及目前輸入方式」） | 在非拉丁（注音）與拉丁（ABC，或「上次使用的拉丁輸入方式」）間切換。此選項只在加入非拉丁輸入方式後出現，選項文字隨輸入方式列表變化 | M-IS、A-SW、M-LANG、L5 | 系統處理（放行）；IME 需在 Info.plist 宣告 `TICapsLockLanguageSwitchCapable = true`（Apple 自家鍵盤輸入法 bundle 都有，L1、L15） | 第三方 IME 宣告此鍵後是否就會出現該選項：**未能確認**（N-DEVF：2017 年第三方 IME 沒有此選項、無解答）→ T1-2 |
| Caps Lock 長按（同上選項開啟時） | 按住直到出現「大寫鎖定」符號 → 進入連續大寫（真正鎖定 Caps Lock） | M-IS（「若要啟用連續大寫輸入，請按住按鍵直到『大寫鎖定』符號顯示」）、L5（"Press and hold to enable typing in all uppercase."） | 放行（系統） | 長按門檻時間未見文件：**未能確認**；本機 `AppleCapsLockPressAndHoldToggleOff = 0`（L8）語意未公開：**未能確認** |
| Caps Lock（未開切換選項） | 一般大寫鎖定；注音模式下字母鍵改出英文（詳見第 6 節） | M-IS、L6 | IME 自行處理（第 6 節） | 寬度（半形/全形）**未能確認**（見第 6 節） |
| 「中／英」專用鍵（中文–拼音、中文–注音實體鍵盤） | 與 Caps Lock 切換相同 | M-IS、A-SW、M-LANG；L5 泛化字串 `Use the %1$@ key to switch to and from %2$@` | 放行 | — |
| 單按 Shift（按下放開，中間無其他鍵） | **內建注音沒有 Shift 切換中英的功能或設定**：Apple 列出的注音設定只有「候選字視窗（方向/字體大小）、顯示關聯字、調整用字頻率、顯示簡體字和罕用字、特殊字彙」；本機注音設定 nib 與鍵盤設定字串也沒有 Shift 選項 → 推論單按 Shift 不做事 | A-SET、L4、L5、L10 | 放行（要「完全一樣」就不要實作）。若想加入（第三方慣例，N-MCB、N-VCH），做成**預設關閉**的選項，並在 `recognizedEvents` 加入 flagsChanged（L16） | 執行期行為**未能確認**（T1-3）。注意：只收 keyDown 時 IMK 才提供「點組字區外自動 commitComposition:」的預設滑鼠處理（L16），加 flagsChanged 不影響此點，但若加了滑鼠事件就要自己處理 |
| ⌃⇧Z／⌃⇧E／⌃⇧P／⌃⇧D／⌃⇧C／⌃⇧S／⌃⇧H | 切換至繁體注音／繁體倚天注音／繁體拼音／繁體雙拼／繁體倉頡／繁體速成／繁體筆畫 | A-KEY、L1（`tsInputModeKeyEquivalentKey`＋`…ModifiersKey = 4608`） | 只有我們也提供多個輸入模式時才需實作；否則放行 | 目標模式尚未加入時是否有效：**未能確認** |
| ⌘Space／⌘⌥Space | 本機預設為 Spotlight／Finder 搜尋（L13 ID 64、65）；Cocoa 預設綁定為 `cycleToNextInputScript:`／`cycleToNextInputKeyboardLayout:`（L12）；K-SC 註明某些版本在多語言時可切換輸入方式 | L12、L13、K-SC | 放行 | — |
| 「自動切換至文件的輸入方式」 | 每份文件記住自己的輸入方式，回到該文件時自動切回 | M-IS、L5；本機已開（L8 `TextInputGlobalPropertyPerContextInput = 1`） | 系統處理；IME 須在 `deactivateServer:`／`commitComposition:` 正確收尾 | — |
| （任何方式）組字區有內容時被切換輸入方式／點到組字區外 | 原生注音是「送出」還是「丟棄」組字區：**未能確認**。IMK 規格：client 想立即結束組字時呼叫 `commitComposition:`；只收 keyDown 的 IME，在點擊組字區外時會自動收到 `commitComposition:` | L16 | IME 自行處理：實作 `commitComposition:`（建議：把目前轉換結果 insertText 送出並清空） | T1-1 |

**第 1 節手動驗證步驟**
- **T1-1**：TextEdit 新文件，切到「注音－繁體」，鍵入 `5` `6`（ㄓˊ，出現帶底線未送出的「直」），按 ⌃Space（或按 🌐︎、或從輸入法選單選 ABC）。觀察「直」是否留在文件中且底線消失（=送出）或整段消失（=丟棄）。再分別用「滑鼠點文件其他位置」「⌘Tab 切到別的 App 再切回」重做。
- **T1-2**：自製 IME 的 Info.plist（與 Apple 相同層級）加入 `TICapsLockLanguageSwitchCapable = true`，安裝後登出再登入；只保留 ABC＋自製 IME，到 系統設定 > 鍵盤 > 文字輸入 > 編輯，看是否出現「使用大寫鎖定鍵來切換…」並實按 Caps Lock 測短按／長按。
- **T1-3**：注音模式、組字區空白時，單按左 Shift 放開，再按 `J`：輸出「ㄨ」表示沒有切換；在組字區已有「ㄓ」時再做一次；右 Shift 也做一次。

---

## 2. 注音鍵盤佈局（標準／大千）與組字規則

### 2.1 37 個注音符號＋聲調：按鍵對照（標準 vs 倚天）

> 依據：**L6**（本機 `com.apple.keylayout.ZhuyinBopomofo` 與 `com.apple.keylayout.ZhuyinEten` 以 `UCKeyTranslate` 讀出的實際碼位）、**L7**（`ZhuyinEten.keylayout` XML）；Apple 範例交叉驗證：A-ZY「ㄈㄢˊㄊㄧˇㄓㄨㄥ ㄨㄣˊ」=`z06wu35j/ jp6`、「ㄓˊㄨˋㄒㄩㄝˊ」=`56j4vm,6`；A-ET 同字串 =`f82te3,x- x92`、`,2x4cuw2`。
> 處理方式：以下全部 **IME 自行處理**（無修飾鍵時）。

| 類別 | 符號（Unicode） | 標準（大千）按鍵 | 倚天按鍵 |
|---|---|---|---|
| 聲母 | ㄅ U+3105 | 1 | B |
| 聲母 | ㄆ U+3106 | Q | P |
| 聲母 | ㄇ U+3107 | A | M |
| 聲母 | ㄈ U+3108 | Z | F |
| 聲母 | ㄉ U+3109 | 2 | D |
| 聲母 | ㄊ U+310A | W | T |
| 聲母 | ㄋ U+310B | S | N |
| 聲母 | ㄌ U+310C | X | L |
| 聲母 | ㄍ U+310D | E | V |
| 聲母 | ㄎ U+310E | D | K |
| 聲母 | ㄏ U+310F | C | H |
| 聲母 | ㄐ U+3110 | R | G |
| 聲母 | ㄑ U+3111 | F | 7 |
| 聲母 | ㄒ U+3112 | V | C |
| 聲母 | ㄓ U+3113 | 5 | , |
| 聲母 | ㄔ U+3114 | T | . |
| 聲母 | ㄕ U+3115 | G | / |
| 聲母 | ㄖ U+3116 | B | J |
| 聲母 | ㄗ U+3117 | Y | ; |
| 聲母 | ㄘ U+3118 | H | ' |
| 聲母 | ㄙ U+3119 | N | S |
| 介音 | ㄧ U+3127 | U | E |
| 介音 | ㄨ U+3128 | J | X |
| 介音 | ㄩ U+3129 | M | U |
| 韻母 | ㄚ U+311A | 8 | A |
| 韻母 | ㄛ U+311B | I | O |
| 韻母 | ㄜ U+311C | K | R |
| 韻母 | ㄝ U+311D | , | W |
| 韻母 | ㄞ U+311E | 9 | I |
| 韻母 | ㄟ U+311F | O | Q |
| 韻母 | ㄠ U+3120 | L | Z |
| 韻母 | ㄡ U+3121 | . | Y |
| 韻母 | ㄢ U+3122 | 0 | 8 |
| 韻母 | ㄣ U+3123 | P | 9 |
| 韻母 | ㄤ U+3124 | ; | 0 |
| 韻母 | ㄥ U+3125 | / | - |
| 韻母 | ㄦ U+3126 | - | = |
| 聲調 | 一聲（不顯示） | Space | Space |
| 聲調 | ˊ 二聲 U+02CA | 6 | 2 |
| 聲調 | ˇ 三聲 U+02C7 | 3 | 3 |
| 聲調 | ˋ 四聲 U+02CB | 4 | 4 |
| 聲調 | ˙ 輕聲 U+02D9 | 7 | 1 |

**佈局中不屬於注音符號的鍵（無修飾鍵）**

| 鍵 | 標準（大千）輸出 | 倚天輸出 | 來源 | 備註 |
|---|---|---|---|---|
| `` ` `` | · U+00B7 | · U+00B7 | L6 | 不是 U+2027「‧」。IME 收到後是直接送出或進組字區：**未能確認**（第 5 節 T5-1） |
| `[` / `]` | 「 / 」 | 「 / 」 | L6 | 見第 5 節 |
| `\` | 、 | 、 | L6、N-MCB2 | 頓號在 `\` |
| `=` | =（ASCII） | ㄦ | L6 | 標準佈局的 `=` 不是注音；候選字視窗開著時 `=`／`-` 換列（第 4 節） |
| `'` | '（ASCII） | ㄘ | L6 | — |
| `5`、`6`（倚天） | （標準為 ㄓ、ˊ） | 5、6（ASCII 數字） | L6 | 倚天佈局中 5、6 不是注音鍵 |

### 2.2 組字規則

| 規則/情境 | macOS 原生行為 | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| 音節完成時機 | 「於一個音節後輸入聲調（空白鍵用來輸入一聲）時轉換文字」；沒打聲調前注音符號留在組字區 | A-ZY、A-ET | IME 自行處理 | — |
| 連續音節自動選字 | 繼續打下一個音節時，文字會依辭典自動改變：`56`→直，再 `j4`→植物，再 `vm,6`→植物學（全部仍在組字區、未送出） | A-ZY、A-ET | IME 自行處理（這正是 LLM 糾錯可以介入的位置） | 原生「後選字」 |
| 只有聲母的音節 | 如 ㄓ＋聲調（直）、ㄗ＋聲調 可成立 | A-ZY 範例 `56` | IME 自行處理 | — |
| 同一槽位打第二個符號（如 ㄅ 後再打 ㄆ；或 ㄧ 後再打 ㄨ） | **未能確認**（取代前一個？兩個並存？） | — | 待測後決定 | T2-1 |
| 先打韻母再打聲母（如 `0` ㄢ 後 `1` ㄅ） | **未能確認**（自動排序為 ㄅㄢ？或視為兩個音節？） | — | 待測 | T2-2 |
| 組字區空時按聲調鍵（如 `3`、`4`、`6`、`7`） | **未能確認**（輸出 ˇ／ˋ 符號？忽略？） | — | 待測 | T2-3 |
| 音節已帶聲調後再按另一聲調鍵 | **未能確認**（換聲調？開始新音節？） | — | 待測 | T2-4 |
| 不合法拼合（如 ㄅㄩ） | **未能確認** | — | 待測 | T2-5 |
| 移除最後一次輸入 | Shift+Delete：例如在「植物學」狀態按 ⇧Delete 會移除最後的聲調 ˊ | A-ZY、A-ET | IME 自行處理 | 詳見第 3 節 |
| 顯示佈局 | 輸入法選單 > 顯示模擬鍵盤 | A-ZY | — | — |

### 2.3 macOS 提供的注音佈局

| 項目 | 內容 | 來源 |
|---|---|---|
| 注音－繁體（Zhuyin – Traditional） | 標準（大千）佈局，使用 `com.apple.keylayout.ZhuyinBopomofo`；輸入模式 ID `com.apple.inputmethod.TCIM.Zhuyin` | L1、L6、L8、A-ZY |
| 倚天注音－繁體（Zhuyin Eten – Traditional） | 獨立的輸入方式（不是注音－繁體裡的選項），使用 `ZhuyinEten.keylayout`／`com.apple.keylayout.ZhuyinEten`；輸入模式 ID `com.apple.inputmethod.TCIM.ZhuyinEten`；行為「與繁體注音相似，只是鍵盤佈局不同」 | L1、L6、L7、A-ET、A-KEY（⌃⇧E） |
| 許氏、IBM、倚天 26 鍵、漢語/通用拼音式注音等 | **macOS 內建沒有**：TCIM 全部輸入模式只有 Cangjie、Jianyi（速成）、Pinyin、Shuangpin、WBH（筆畫）、Zhuyin、ZhuyinEten；Apple 注音設定也沒有「鍵盤佈局」選項 | L1、L4、A-SET |

**第 2 節手動驗證步驟**（都在 TextEdit、注音－繁體、組字區空白開始；每次測完按 Esc 或 Return 清掉）
- **T2-1**：鍵入 `1` 再 `q`：看組字區顯示「ㄆ」（取代）或「ㄅㄆ」；再測 `u` 後 `j`（ㄧ→ㄨ）、`8` 後 `9`（ㄚ→ㄞ）。
- **T2-2**：鍵入 `0`（ㄢ）再 `1`（ㄅ）再 `4`：看結果是「辦/半」等（自動重排）或其他。
- **T2-3**：組字區空白時分別按 `3`、`4`、`6`、`7`：看文件出現 ˇ ˋ ˊ ˙ 符號、或什麼都沒有。
- **T2-4**：鍵入 `5` `6`（直）後立刻按 `4`：看變成「智/志」類（換聲調）或在後面開始新音節。
- **T2-5**：鍵入 `1` `m` `4`（ㄅㄩˋ）：看組字區的處理（拒收、照樣顯示注音、或送出）。

---

## 3. 組字區編輯

> 總原則（Apple IMK 規格，L16）：`didCommandBySelector:client:` 的官方說明舉例——組字區**空**時應回傳 NO，讓 App 收到 Return；組字區**有內容**時才由 IME 處理並回傳 YES。下表「空」一律放行即依此原則。

| 按鍵/組合 | macOS 原生行為（注音：組字區有/無；英文 ABC） | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| Delete（Backspace） | 組字區有內容：**未能確認**（刪最後一個注音符號？刪游標前一個已轉換字？）。組字區空／ABC：App `deleteBackward:` | L12；Apple 只描述 ⇧Delete | 有內容：IME；空：放行 | T3-1 |
| Shift+Delete | 組字中：「若要移除最後一次輸入，請按下 Shift + Delete」，例：「植物學」時按下會移除最後的聲調 ˊ。A-KEY 稱為「還原上一個替代項目」；輸入法選單的同一快速鍵項目為「Forget Selected Word／清除所選的記憶字詞」 | A-ZY、A-ET、A-KEY、L3 | 有內容：IME | 三種描述可能是不同情境（組字中＝退一步；選字中＝遺忘學習字），細節**未能確認**（T3-2）；組字區空時：**未能確認** |
| Forward Delete（fn+Delete；⌃D） | 組字區有內容：**未能確認**。空／ABC：App `deleteForward:`（K-SC：fn-Delete 為向前刪除，Control-D 亦可） | L12、K-SC | 有內容：IME；空：放行 | T3-1 |
| ← / → | 候選字視窗開著：前一個／下一個候選字。視窗沒開、組字區有內容：**未能確認**（推測移動組字區游標）。空／ABC：App `moveLeft:`／`moveRight:` | A-CW、L12 | 有內容：IME；空：放行 | T3-3 |
| ↑ / ↓ | 候選字視窗開著：前一個／下一個候選字。視窗沒開時 ↓ 是否開窗：**未能確認**（Apple 只寫「按兩下空白鍵」） | A-CW | 有內容：IME；空：放行 | T3-4 |
| Home／End、fn+←／→、⌘←／⌘→、⌃A／⌃E | 組字區有內容：**未能確認**。空／ABC：App（⌘←→ 行首尾、fn+←→ 文件首尾、⌃A/⌃E 段首尾） | L12、K-SC | 有內容：待測；空：放行 | T3-3 |
| Esc | 組字區有內容或候選字視窗開著：**未能確認**（先關視窗？一次清空組字區？）。空／ABC：App `cancelOperation:`（ABC 有自動修正建議時，Esc＝忽略建議，見第 8 節） | L12、M-TYPE | 有內容：IME；空：放行 | T3-5 |
| Return | 組字區有內容：「按下 Return 鍵來確認文字」＝送出。候選字視窗開著：選取反白候選字（**注音／倚天注音用 Return，不是空白鍵**）。空／ABC：App `insertNewline:` | A-ZY、A-ET、A-CW、L12、L16 | 有內容：IME；空：放行 | 送出時是否同時換行：**未能確認**；只有未完成注音（無聲調）時按 Return 送出什麼：**未能確認**（T3-6） |
| Space | ①注音符號之後：當一聲並完成音節、自動轉換；②已轉換、視窗未開：開候選字視窗（「鍵入輸入碼，然後按兩下空白鍵」）；③視窗多列時：垂直視窗跳下一頁、水平視窗前往下一列；④組字區空／ABC：App 插入空格 | A-ZY、A-ET、A-CW | ①②③ IME；④ 放行 | ④ 連按兩下的「加入全形句號」行為見第 8 節（T8-3） |
| Tab／Shift+Tab | 候選字視窗開著：切換候選字排序方式（下一個／上一個）。注音是否有排序選項：**未能確認**（「部分輸入方式並不支援排序選項」）。視窗沒開但組字區有內容：**未能確認**。空／ABC：App `insertTab:`／鍵盤導覽（Tab 往前、Shift+Tab 往後移焦點） | A-CW、L3、L12、M-KB | 有內容：IME；空：放行 | T3-7 |
| Shift+Return | 組字區有內容：**未能確認**。空／ABC：交給 App | — | 有內容：待測；空：放行 | T3-6 |
| Shift+Space | A-KEY：「顯示拆字候選字」（拼音的拆字輸入用）；在注音中的效果：**未能確認** | A-KEY、A-PY、L3 | 待測 | T3-8 |
| ⌥Tab／⌥⇧Tab | 選單項目「選取下一個／上一個聲調」；A-CW 說明為**拼音**候選字依四聲過濾 | A-CW、L3 | 拼音才需要；注音效果**未能確認** | T3-8 |
| 組字區最大長度／自動送出 | Apple 文件未提：**未能確認** | — | 待測 | T3-9 |
| 組字區有內容時打標點（如 Shift+,） | **未能確認**（先送出再出標點？標點也進組字區？） | — | 待測 | T3-10 |

**第 3 節手動驗證步驟**（TextEdit、注音－繁體）
- **T3-1**：鍵入 `5` `j`（ㄓㄨ，未打聲調）按 Delete：看剩「ㄓ」還是全刪；鍵入 `56j4`（植物）按 Delete：看變「植ㄨ」、「植」或其他；把游標用 ← 移到中間再按 Delete／fn+Delete 各一次。
- **T3-2**：鍵入 `56j4vm,6`（植物學）按 ⇧Delete（應移除 ˊ，對照 A-ZY）；再按一次看是否繼續退；另外先打一個你常用、已被學習的詞，開候選字視窗選到它，按 ⇧Delete，看是否從學習中移除；組字區空時按 ⇧Delete 看是否刪掉文件中的字。
- **T3-3**：鍵入 `56j4vm,6` 後按 ←、→、Home、End、⌘←、⌃A，每按一次觀察組字區內的游標/反白位置，以及文字是否被送出。
- **T3-4**：鍵入 `56` 後按 ↓：看是否開出候選字視窗；再按 Space 比較。
- **T3-5**：鍵入 `56j4` 後按 Esc；另外鍵入 `56`（已帶二聲、已自動轉換）後按**一下** Space 開窗再按 Esc，再按一次 Esc。分別記錄視窗、組字區、文件內容。
- **T3-6**：鍵入 `56j4` 按 Return：看是否只送出「植物」而不換行；鍵入 `5j`（無聲調）按 Return：看送出「ㄓㄨ」、轉換結果、或什麼都不送；鍵入 `56j4` 按 Shift+Return。
- **T3-7**：鍵入 `56` 按一下 Space 開窗後按 Tab、Shift+Tab：看排序方式是否改變或有無排序列；不開窗時鍵入 `56` 直接按 Tab。
- **T3-8**：鍵入 `56` 後按 Shift+Space；開窗後按 ⌥Tab。
- **T3-9**：不按 Return 連續打 30 個以上音節（例如反覆 `56j4vm,6`），看組字區是否在某長度自動把前段送出。
- **T3-10**：鍵入 `56j4` 後按 Shift+,：看「植物」是否先送出、「，」出現在文件中或組字區中。

---

## 4. 候選字視窗

| 按鍵/組合 | macOS 原生行為（注音） | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| 開啟 | 注音／倚天注音：「鍵入輸入碼，然後按兩下空白鍵」（第一下＝一聲完成音節，第二下開窗）；已自動轉換後按一下空白鍵即可。（其他中文輸入方式多半自動開窗） | A-CW、A-ZY | IME | ↓ 是否開窗：**未能確認**（T3-4） |
| 方向鍵 | 移到下一個／前一個候選字 | A-CW | IME | — |
| `=`／`-`，或 `[`／`]` | 移到下一列／上一列 | A-CW | IME（視窗開啟時才攔截；平時這些鍵是 =、ㄦ、「、」） | — |
| Space（視窗開著） | 垂直視窗：跳到下一頁；水平視窗：前往下一列 | A-ZY、A-ET | IME | 注音的 Space **不會**選字（與其他中文輸入方式不同） |
| 捲動 | 滑鼠／觸控板上下捲動瀏覽 | A-CW | IME 候選窗支援捲動 | — |
| Page Up／Page Down | **未能確認** | — | 待測 | T4-1 |
| 數字鍵 | 按候選字對應的數字鍵選字 | A-CW | IME | 每頁候選數（1–9？0？）：**未能確認**（T4-2） |
| Return | 選取目前反白的候選字（注音、倚天注音） | A-CW | IME | — |
| 滑鼠 | 按兩下候選字選取 | A-CW | IME | — |
| Option＋數字 | 「若要輸入數字，請在輸入數字時按住 Option 鍵」 | A-CW、L6（注音佈局 Option 層＝ASCII 數字） | IME：輸出半形數字（或放行讓佈局輸出） | 組字區有內容時：**未能確認** |
| Esc | 關閉／取消：**未能確認** | — | IME | T3-5 |
| Tab／Shift+Tab | 切換排序方式（如「頻率」「部首」）；也可按視窗右側向下箭頭 | A-CW、L3 | IME | 注音是否有排序：**未能確認** |
| 方向（水平／垂直）、字體大小 | 輸入法選單 > 打開〔輸入方式〕設定 > 選「水平」或「垂直」；字體大小 14/16/18/24/36 | A-CW、A-SET、L4 | IME 提供同等設定 | 預設值：`ZhuyinCandidateWindowDirection = 1`（L2），對照 nib tag 1 = Vertical（L4）→ 推論注音預設**垂直**，**未能確認**（T4-3）；預設字體 16（L2） |
| 反白顏色 | 系統設定 > 外觀 > 反白顏色 | A-CW | 候選窗使用系統強調色 | — |
| 候選內容 | 所有可能字元，注音／拼音含表情符號；常用字詞「輸入建議」在背景定期更新 | A-CW | — | — |
| 顯示關聯字（Show predictive completions） | 打字時在候選字視窗中顯示，「關聯字會在視窗中顯示為第二或第三個候選字」；預設開（L2 `showAssociatedWords = true`） | A-SET、L2、L4 | IME | 送出後是否像 Windows 新注音另跳「聯想詞」：Apple 未描述 → **未能確認**（T4-4） |
| 學習（調整用字頻率） | 「將最常用的候選字優先顯示」（Apple 以拼音舉例：選過「蒲桃」，下次同碼「蒲桃」排第一）；注音預設開（L2 `AdjustWordFrequencyZhuyin = true`） | A-SET、L2、L4 | IME | 注音學習的粒度（詞/字、是否影響自動選字結果）：**未能確認**（T4-5） |
| 遺忘學習字詞 | 輸入法選單「清除所選的記憶字詞」＝ ⇧Delete | L3 | IME | 與第 3 節 ⇧Delete 共用 |
| 顯示簡體字和罕用字／特殊字彙 | 可在候選中顯示簡體與罕用字；可勾選專業詞庫（建築、生物學、佛教、中醫、商業、通訊、電腦、教育、工業、法律與政治、軍事、物理學與化學、運動、西醫） | A-SET；`CoreChineseEngine.framework/…/Localizable.loctable`（`ExpertDict_*`） | 選用 | — |
| 使用者辭典 | 輸入法選單「編輯文字替代」加入的詞即使用者辭典；支援繁體注音、繁體倚天注音；隨 Apple 帳號同步 | A-DICT、M-SUB | IME 建議讀取系統文字替代 | — |
| 替代候選字 | 選取輸入碼 → 輸入法選單 > 顯示替代候選字（內文顯示） | A-CW、L3 | 選用 | — |

**第 4 節手動驗證步驟**
- **T4-1**：`56` 後按一下 Space 開窗（`56` 已帶聲調，第一下即開窗；若打無聲調碼如 `5j/` 才需兩下），按 Page Down／Page Up、fn+↓／fn+↑，看是否翻頁。
- **T4-2**：同上開窗，數每頁編號的候選數；按 `0` 看是否有作用。
- **T4-3**：輸入法選單 > 打開繁體注音設定，記錄「候選字視窗」的方向與字體預設值（新使用者帳號最準）。
- **T4-4**：確認「顯示關聯字」開啟，打 `56j4`（植物）按 Return 送出，觀察是否出現後續聯想視窗；再打 `5` 不打聲調，看候選中第 2、3 位是否出現預測詞。
- **T4-5**：打 `56`，Space 開窗，選一個非第一的同音字（如「職」），送出；再打 `56` 看自動選字是否改變；再打 `56j4` 看詞的結果是否受影響。

---

## 5. 注音模式下的標點與符號

> 關鍵限制：下表「佈局輸出」是本機鍵盤佈局 `com.apple.keylayout.ZhuyinBopomofo` 的實際碼位（L6）。TCIM 引擎拿到後**是否再轉成全形**（例如把 `!` 換成「！」），Apple 文件沒有寫，屬**未能確認**。旁證：引擎的標點替代候選表（L9）是以全形字為鍵（「！」→１、「＠」→２、「（」→９、「）」→０…），且有全形↔半形對照表（，；：！？。（）【】｜～），顯示中文模式的標點以全形為主；【非 Apple】N-RAY 亦稱中文模式 Shift+標點為全形。Apple 的注音設定**沒有**「使用半形標點符號」（該選項只列給拼音、雙拼、筆畫、粵語等，A-SET）。
> 處理方式：組字區空時，以下由 IME 直接送出對應字元（IME 自行處理）；組字區有內容時的行為見 T3-10。

### 5.1 標準（大千）佈局 Shift 層與其他符號鍵

| 按鍵 | 佈局輸出（L6，Unicode） | 倚天佈局輸出（L6） | IME 最終輸出 | 來源 | 備註 |
|---|---|---|---|---|---|
| Shift+, | ，U+FF0C | ，U+FF0C | ，（佈局本身即全形） | L6 | — |
| Shift+. | 。U+3002 | 。U+3002 | 。 | L6 | — |
| `[` ／ `]` | 「 U+300C／」 U+300D | 同左 | 「」 | L6 | — |
| Shift+[ ／ Shift+] | 『 U+300E／』 U+300F | 同左 | 『』 | L6 | — |
| `\` | 、U+3001 | 、 | 、 | L6、N-MCB2 | 頓號在 `\`（非 Shift） |
| `` ` `` | · U+00B7 | · | **未能確認**（直接送出／進組字區／開符號表？） | L6 | T5-1 |
| Shift+/ | ? U+003F | ？U+FF1F | **未能確認**（? 或 ？） | L6、L9、N-RAY | T5-3 |
| Shift+1 | ! | ！U+FF01 | **未能確認**（! 或 ！） | L6、L9、N-RAY | T5-3 |
| Shift+; | : | ：U+FF1A | **未能確認**（: 或 ：） | L6、L9 | 「；」沒有直接按鍵：在 L9 中是「：」的替代候選（T5-2） |
| Shift+' | " | " | **未能確認**（" 或「“”」） | L6、L9（" 的替代：“ ” 〃 々） | T5-3 |
| Shift+9 ／ Shift+0 | ( ／ ) | （ ／ ） | **未能確認** | L6、L9 | T5-3 |
| Shift+` | ~ | ～U+FF5E | **未能確認** | L6、L9（~ 的替代：～ ﹋） | T5-3 |
| Shift+2…8 | @ # $ % ^ & * | ＠ ＃ ＄ ％ ^ ＆ ＊ | **未能確認** | L6 | 倚天的 Shift+6 仍是半形 ^ |
| Shift+- ／ Shift+= | _ ／ + | ＿ ／ ＋ | **未能確認** | L6 | — |
| Shift+\ | \| | ｜ | **未能確認** | L6 | — |
| `=`、`'`（標準佈局） | = ／ '（ASCII） | （倚天為 ㄦ、ㄘ） | **未能確認** | L6 | 標準佈局中這兩鍵不是注音 |
| Option+數字 | 0–9 ASCII | 同左 | 半形數字（Apple：「按住 Option 鍵」輸入數字） | A-CW、L6 | — |
| Option+字母／符號 | ASCII 小寫字母與原符號；**Option+- = —（U+2014）**；Option+Shift = ASCII 大寫與 Shift 符號；**沒有任何 dead key** | 同左 | **未能確認**（推測照佈局輸出） | L6 | 與 ABC 的 Option 層（™ £ ¢ ∞ …、dead keys）完全不同 |
| Caps Lock＋鍵（未用於切換） | 字母→ASCII **小寫**、數字列→數字、標點→ASCII；Shift+Caps→ASCII 大寫與 Shift 符號 | 同左 | **未能確認**（見第 6 節） | L6、L7 | — |

### 5.2 常用中文標點怎麼打（依上表＋替代候選）

| 標點 | 原生取得方式 | 來源 | 備註 |
|---|---|---|---|
| 「」『』 | `[` `]`／Shift+`[` `]` | L6 | 「 的替代候選：『【〖〔［｛（L9） |
| 、 | `\` | L6、N-MCB2 | 、 的替代：\ ＼ ｜（L9） |
| ，。 | Shift+, ／Shift+. | L6 | ， 的替代：、　《〈＜；。 的替代：⋯⋯　・·．》〉＞（L9） |
| ；： | Shift+;（得 : 或 ：），；需經替代候選或符號視窗 | L6、L9 | 叫出替代候選的按鍵：**未能確認**（T5-2） |
| ？！ | Shift+/、Shift+1 | L6 | 全形與否**未能確認**（T5-3） |
| ⋯⋯（刪節號） | 繁中預設標點清單第一項為「⋯⋯」（U+22EF×2；簡中是「……」U+2026×2）；也是「。」的替代候選 | L9（`CIMDefaultTraditionalPunctuation.plist`、`CIMDefaultSimplifiedPunctuation.plist`、`CIMPunctuationCandidates.plist`） | 直接按鍵：**未能確認** |
| ——（破折號） | 「＿」的替代候選第一項為「——」；Option+- 得單一「—」 | L9、L6 | — |
| ～ | Shift+`（得 ~ 或 ～），替代：～ ﹋ | L6、L9 | — |
| 《》〈〉【】〔〕 | 經替代候選（L9）或「表情與標點符號」視窗 | L9、A-PUN | — |
| 全部符號 | 「表情與標點符號」視窗：**Option+Shift+B**（或輸入法選單 > 顯示表情與標點符號）；視窗內按一下「最近使用過的項目／符號／表情符號」或按 Tab 循環；方向鍵選＋Return、數字鍵、或按兩下插入；找不到時用字元檢視器（輸入法選單 > 顯示表情符號與符號） | A-PUN、A-KEY、L3 | IME 若要相容，可實作同快捷鍵 |

### 5.3 全形／半形

| 項目 | 原生行為 | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| 拉丁字元預設寬度 | 「使用中文輸入方式輸入拉丁字元時，字元會依照預設以半形輸入」 | A-LAT | IME | 與 N-RAY（全形）矛盾 → 第 6 節 T6-1 |
| 轉換已輸入文字 | 選取文字 → 輸入法選單 > 轉換成全形文字／轉換成半形文字 | A-LAT、A-CONV、L3 | 選用（輸入法選單項目） | — |
| 半形標點快速鍵 | Option+Shift+H「使用半形標點符號」（繁中快速鍵表有列） | A-KEY、L3 | 選用 | 注音設定中沒有此選項（A-SET），在注音中是否有效：**未能確認**（T5-4） |
| 全形數字 | 數字鍵本身是注音；可用替代候選（如「！」的替代為「１」）或轉換成全形文字 | L9、A-CONV | 選用 | 直接輸入全形數字的方法：**未能確認** |

**第 5 節手動驗證步驟**（TextEdit、注音－繁體、組字區空）
- **T5-1**：按 `` ` ``：記錄文件出現「·」、出現符號視窗、或進入組字區（有底線）。
- **T5-2**：按 Shift+;（：）後立刻按 Space、再試 ↓：看是否出現「；」等替代候選；對 Shift+,（，）、Shift+.（。）、`[`（「）同樣測試。
- **T5-3**：依序按 Shift+/、Shift+1、Shift+9、Shift+0、Shift+`、Shift+'、Shift+2：用「編輯 > 表情符號與符號」或把結果貼到 `python3 -c "print([hex(ord(c)) for c in input()])"` 檢查是半形還是全形碼位。
- **T5-4**：按 Option+Shift+H 後重做 T5-3，看是否改變；再按一次恢復。

---

## 6. 注音模式中的英文

| 按鍵/組合 | macOS 原生行為 | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| Shift+字母（組字區空） | 佈局輸出：**標準佈局＝ASCII 大寫**（Shift+Q→Q）；**倚天佈局＝全形小寫**（Shift+Q→ｑ U+FF51）。Apple：「針對大寫拉丁字元，請在你鍵入時按住 Shift 鍵」、「字元會依照預設以半形輸入」。【非 Apple】N-RAY（2020）：中文模式 Shift 打出全形大寫「ＡＢＣＤ」 | L6、A-LAT、N-RAY | IME 自行處理 | 證據互相矛盾 → **未能確認**（T6-1） |
| Shift+字母（組字區有內容） | **未能確認**（先送出組字區再輸出字母？字母進組字區？） | — | 待測 | T6-1 |
| Caps Lock 開啟（未設為切換鍵）＋字母 | 佈局 Caps 層：**ASCII 小寫**（標準與倚天皆同），Shift+Caps＋字母＝ASCII 大寫；數字列＝ASCII 數字。【非 Apple】N-RAY：Caps Lock 打出全形小寫「ａｂｃｄ」 | L6、L7、N-RAY | IME 自行處理 | 寬度與是否繞過組字：**未能確認**（T6-2）。注意：與 ABC 相反（ABC 的 Caps 層是大寫） |
| Option＋字母 | 佈局 Option 層＝ASCII 小寫（無 dead key） | L6 | 待測 | **未能確認**（T6-3） |
| 輸入數字 | Option＋數字鍵 | A-CW、L6 | IME | — |
| 輸入英文單字並從候選選取 | 拼音／倉頡／五筆等才有（「輸入英文單字，然後在候選字視窗中選取單字…專有名詞會自動加上大寫」）；注音的字母鍵都是注音符號，Apple 未描述 | A-PY、A-SET | 不適用 | — |
| 「使用最近使用過的拉丁鍵盤佈局輸入」 | 只列給拼音、雙拼、粵語拼音（且需加入一個以上拉丁輸入方式）；注音沒有 | A-SET、L2（`usesLastUsedLatinKeyboardLayout = true`）、CoreChineseEngine `Localizable.loctable`（TypeUsingLastUsedLatinKeyboardLayout） | 不適用 | — |

### 6.1 繁體拼音（Pinyin – Traditional）與注音的主要差異（供次要參考）

| 項目 | 繁體注音 | 繁體拼音 | 來源 |
|---|---|---|---|
| 輸入碼 | 注音符號（大千／倚天佈局） | 拉丁字母（全拼、簡拼 pggs、混用；撇號分隔音節；v＝ü）；佈局檔另為 `TraditionalPinyin.keylayout`（本機 `…/TCIM_Extension.appex/Contents/Resources/`，未逐鍵分析） | A-ZY、A-PY；本機檔案目錄 |
| 開候選字視窗 | 打完輸入碼按兩下 Space（第一下是一聲；碼已帶聲調並自動轉換時按一下即可） | 打碼即自動開啟 | A-CW、A-ZY |
| 確認反白候選 | Return | Space | A-CW |
| 聲調過濾 | （未描述） | ⌥Tab 循環四聲／取消過濾 | A-CW |
| 拆字 | （未描述） | Shift+Space（例 `jiji`） | A-PY |
| 筆畫混打 | （未描述） | 前置鍵 `u`＋筆畫碼 h s p n z（x 萬用） | A-PY |
| 英文單字 | 無 | 可打英文單字從候選選取，專有名詞自動大寫 | A-PY |
| 專屬設定 | — | 「使用最近使用過的拉丁鍵盤佈局輸入」（預設開）、「更正拼字」（預設開，L2 `autocorrectionEnabled`）、「更正模糊拼音」（預設關，L2）、「使用半形標點符號」（預設關，L2） | A-SET、L2 |
| 共同設定 | 候選字視窗方向/字體、顯示關聯字、調整用字頻率、顯示簡體字和罕用字、特殊字彙 | 同左 | A-SET |
| 預設候選窗方向 | `ZhuyinCandidateWindowDirection = 1`（推論垂直） | `candidateWindowDirection = 0`（推論水平） | L2、L4（推論，**未能確認**） |

**第 6 節手動驗證步驟**
- **T6-1**：注音－繁體、組字區空，按 Shift+A、Shift+B；用 T5-3 的方法檢查是 `A`（U+0041）還是 `Ａ`（U+FF21）；組字區有「植物」（`56j4`）時再按 Shift+A，看「植物」是否先送出。倚天注音再做一次（預期依佈局為 ｑ 類全形小寫，需確認）。
- **T6-2**：關閉 Caps Lock 切換選項後開啟 Caps Lock（燈亮），打 `abc`、`123`、`,./`，檢查大小寫與寬度；再按住 Shift 打 `abc`。
- **T6-3**：注音模式按 Option+A、Option+-，檢查輸出（預期 a、—）。

---

## 7. IME 不可攔截的 macOS 標準文字編輯快速鍵

> 事件流程（D-EV）：Command 類 key equivalent 會先由 NSApp 以 `performKeyEquivalent:` 送給視窗與選單，處理不了才以 `keyDown:` 送 first responder（文字輸入／IME 在這條路上）。因此在 Cocoa App 中 ⌘C／⌘V 這類**選單快速鍵通常根本不會到 IME**；非 Cocoa App（Electron、終端機、遊戲）可能不同。
> 原生注音在**組字區有內容**時對下列按鍵的反應，Apple 沒有文件 → 全部**未能確認**，統一以 T7-1／T7-2 驗證。組字區空時一律放行。

| 按鍵/組合 | macOS 原生行為（ABC／組字區空） | 來源 | 處理方式 | 備註 |
|---|---|---|---|---|
| ⌘＋任意鍵（⌘C ⌘V ⌘X ⌘Z ⌘A ⌘S ⌘W ⌘Q…） | App 選單指令 | D-EV、K-SC | 放行（回傳 false）；若事件真的到 IME 且組字區有內容 → 建議先 commit 再放行（待 T7-1 校正成原生行為） | 組字區有內容時原生行為：**未能確認**（T7-1） |
| ⌘← ⌘→ ⌘↑ ⌘↓（＋⇧ 選取） | 行首／行尾／文件開頭／文件結尾 | K-SC、L12 | 空：放行；有內容：依 T7-2 | **未能確認**（有內容時） |
| ⌥← ⌥→（＋⇧ 選取）、⌥↑ ⌥↓ | 前一個字詞開頭／下一個字詞結尾；段落移動 | K-SC、L12 | 同上 | 同上 |
| ⌥Delete、⌥fn+Delete | 刪除插入點左側／右側的字詞 | K-SC、L12 | 同上 | 同上 |
| ⌘Delete | 文字中 `deleteToBeginningOfLine:`（Finder 中是丟到垃圾桶） | L12、K-SC | 同上 | 同上 |
| fn+Delete、⌃D | 向前刪除 | K-SC、L12 | 同上 | 同上 |
| fn+↑ ↓ ← → | Page Up／Page Down／Home／End | K-SC | 同上 | 同上 |
| ⌃A ／ ⌃E | 行或段落開頭／結尾 | K-SC、L12 | 同上 | 同上 |
| ⌃K ／ ⌃Y | 剪下插入點到段落結尾；⌃Y 貼回（只限同一 App） | K-SC、L12 | 同上 | 同上 |
| ⌃H ／ ⌃D | 刪除左／右一個字元 | K-SC、L12 | 同上 | 同上 |
| ⌃F ⌃B ⌃P ⌃N | 右移一字元／左移一字元／上一行／下一行 | K-SC、L12 | 同上 | 同上 |
| ⌃O ⌃T ⌃L ⌃V | 在插入點後插入新行／對調插入點兩側字元／使插入點置中／下一頁 | K-SC、L12 | 同上 | 同上 |
| ⌃' ／ ⌃" | 插入不做智慧型替換的直引號 | L12 | 放行 | — |
| ⌥Esc | `complete:`（文字自動完成清單） | L12 | 放行 | — |
| ⌃Tab ／ ⌃⇧Tab | 鍵盤焦點前後移動（`selectNextKeyView:` 等） | L12 | 放行 | — |
| 按住字母鍵（重音選單） | ABC：按住字母鍵開啟重音選單，按選單下方的數字或用方向鍵移動後按空白鍵選取；「重音符號並非適用於所有 App」 | M-ACC；L8（`com.apple.PressAndHold` 為已選取的 Non Keyboard Input Method） | 英文模式若要和 ABC 一樣：讓字母鍵事件放行，或自行實作同等選單 | 注音模式按住鍵的結果、以及 IME 消化字母鍵時系統重音選單是否仍出現：**未能確認**（T7-3） |
| Option dead keys（例 ⌥E 再按 e → é；⌥N 再按 a → ã） | ABC：⌥`、⌥E、⌥I、⌥N、⌥U 為 dead key（以 NoDeadKeys 模式翻譯時輸出為空）；Apple 範例「Option + N 輸入 ˜，然後按下 a」→ ã。**注音佈局的 Option 層沒有 dead key**（⌥E→e） | M-ACC、L6 | 英文模式若由 IME 自己處理按鍵：需以 ABC 的 uchr＋`deadKeyState` 自行模擬（`UCKeyTranslate`）；否則放行 | 注音模式下 ⌥E→e 的實際輸出：**未能確認**（T6-3）；ABC 下 ⌥E e → é 為佈局語意（L6）＋Apple 範例同類（M-ACC），可視為已確認 |

**第 7 節手動驗證步驟**（TextEdit、注音－繁體）
- **T7-1**：鍵入 `56j4`（「植物」未送出）後分別按 ⌘C、⌘V（剪貼簿先放「TEST」）、⌘A、⌘Z、⌘S：每次記錄「植物」是否先被送出、被丟棄、或仍在組字區，以及指令是否生效。另在 Chrome 網頁文字框與 Terminal 重做 ⌘V。
- **T7-2**：鍵入 `56j4` 後分別按 ⌃A、⌃E、⌃K、⌥←、⌘←、⌥Delete：記錄組字區與文件變化。
- **T7-3**：ABC 下按住 `e` 兩秒（應出現重音選單）；切到注音按住 `e`（ㄍ）兩秒，記錄是重複輸入、出現選單或其他。
- **T7-4**：ABC 下按 ⌥E 再按 `e`（預期 é），⌥U 再按 `o`（預期 ö），⌥N 再按 `n`（預期 ñ）；切到注音做同樣按鍵記錄輸出。

---

## 8. ABC（英文）模式：由 App／文字系統提供、而非輸入方式提供的功能

| 功能（系統設定 > 鍵盤 > 文字輸入 > 編輯 > 所有輸入方式） | macOS 原生行為 | 來源 | 對我們 IME 的影響／處理方式 | 備註 |
|---|---|---|---|---|
| 自動更正拼字（Correct spelling automatically） | 「在支援自動更正拼字的 App 中」生效；也可在各 App 的「編輯 > 拼字和文法檢查 > 自動更正拼字」個別關閉。只有一個建議時繼續打字即自動更正；**按 Esc 忽略建議**；更正後可把插入點放在字後選原拼字，或 Control-點按選「改回『原始拼字』」 | M-IS、M-TYPE（英文版確認 macOS 27：「Press Esc, then continue typing」）、L16（`NSSpellChecker.isAutomaticSpellingCorrectionEnabled`） | 由 App 文字系統執行。若我們放行英文按鍵，系統自動更正照常運作（與 ABC 完全一致）；若我們自己做英文糾錯，可能與系統「雙重修正」 | 本機未設 `NSAutomaticSpellingCorrectionEnabled`（=系統預設，L14） |
| 自動大寫文字（Capitalize words automatically） | 句首與專有名詞（如 New York）自動大寫 | M-IS、M-TYPE、L16 | 同上 | 本機 `NSAutomaticCapitalizationEnabled = 1`（L14） |
| 加入句號（Add period with double-space；zh_TW 介面字串為「加入全形句號」） | 連按兩下空白鍵以句號結束句子；**輸入方式可宣告自己的替代字串**：TCIM 各模式 `TISDoubleSpaceSubstitution = "。"` | M-IS、M-SUB、L1、L5、L15 | 我們的 IME 可在 Info.plist 各輸入模式宣告 `TISDoubleSpaceSubstitution`（中文模式「。」） | 注音模式實際效果：**未能確認**（T8-3）；本機 `NSAutomaticPeriodSubstitutionEnabled = 1`（L14） |
| 使用智慧型引號和破折號（Use smart quotes and dashes）＋雙引號／單引號樣式 | 直引號自動轉印刷引號、連字號轉長破折號（—）；可選引號樣式；「替代文字、智慧型引號和破折號及智慧型連結並非適用於所有 App」 | M-IS、M-SUB、L5 | 同上；⌃' ⌃" 可插入不替換的直引號（L12） | 本機 `NSAutomaticQuoteSubstitutionEnabled = 1`、`NSAutomaticDashSubstitutionEnabled = 1`（L14）。英文版 M-SUB（WebFetch，macOS 27）：「…double hyphens to em dashes (—)」；zh_TW 版譯成「雙引號轉換為長破折號」，以英文為準（`--` → `—`） |
| 顯示內文預測字詞（Show inline predictive text） | 打字時以灰色在內文顯示單字／詞句的預測；**按空白鍵接受，繼續打字則忽略**（Apple 文件**沒有**提到 Tab；Tab 是否也能接受：**未能確認**，T8-2）；部分語言不支援 | M-IS、M-TYPE（英文 macOS 27：「Press the Space bar to accept a prediction, or keep typing to ignore it」）、L5、L16（`isAutomaticInlinePredictionEnabled`，macOS 14+） | 與我們的 marked text／行內建議會在畫面與 Space 鍵語意上衝突 | 我們 IME 消化按鍵時系統是否仍顯示內文預測：**未能確認**（T8-2） |
| 顯示輸入建議（Show typing suggestions）／顯示建議的回覆（Show suggested replies） | macOS 27 鍵盤設定新字串；文件未詳述 | L5 | — | **未能確認**（T8-4） |
| 文字替代（Text Replacements） | 系統層級替代（teh→the）；使用中文輸入方式時，替代文字會納入使用者辭典 | M-SUB、A-DICT | 我們的 IME 可讀取同一資料作為使用者詞庫 | — |
| 拼字檢查語言 | 預設「按照語言自動執行」，可指定或多選 | M-IS、M-TYPE | — | — |
| 自動切換至文件的輸入方式 | 見第 1 節 | M-IS | — | — |

### 8.1 若我們的 IME 自己做英文即時糾錯：衝突與建議

| 議題 | 說明 | 依據 | 建議 |
|---|---|---|---|
| 功能歸屬 | 上表功能由 App 的文字系統（NSTextView／NSSpellChecker）執行，只在支援的 App 生效；不是 ABC 輸入方式本身的功能 | M-IS（「在支援自動更正拼字的 App 中」）、M-SUB（「並非適用於所有 App」）、L16 | 要「和 ABC 一模一樣」，英文模式最安全的做法是**按鍵放行**（回傳 false），讓系統照舊處理 |
| 雙重修正 | 我們先改字，App 的自動更正再改一次（或反向），使用者難以預期 | 推論（M-TYPE 描述系統會在輸入時自動修正） | 在英文模式啟動時讀 `NSSpellChecker.isAutomaticSpellingCorrectionEnabled`／`isAutomaticTextReplacementEnabled`／`isAutomaticInlinePredictionEnabled`（L16）；若系統自動更正開啟，我們只做**非破壞性建議**（候選窗／底線提示），不自動替換 |
| Space 鍵語意 | 系統內文預測用 Space 接受（M-TYPE）；注音中 Space 是一聲／開窗；我們若也用 Space 接受修正會三方衝突 | M-TYPE、A-ZY | 我們的英文修正不要佔用 Space；可用候選窗＋數字/Return，或使用者可設定 |
| Esc 鍵語意 | 系統：Esc 忽略自動更正建議（M-TYPE）；App：`cancelOperation:`（L12） | M-TYPE、L12 | 英文模式組字區空時 Esc 一律放行 |
| 使用者設定建議 | 若使用者要以我們的 LLM 糾錯為主：引導至「系統設定 > 鍵盤 > 文字輸入 > 編輯 > 所有輸入方式」關閉「自動更正拼字」與「顯示內文預測字詞」（必要時「自動大寫文字」） | M-IS、M-TYPE | 在 onboarding 中說明，**不要**自行寫入 defaults |
| 英文模式按鍵細節 | 若 IME 自己處理英文按鍵，需自行重現：ABC dead keys（第 7 節）、Caps Lock 大寫、按住重音選單（是否可行未能確認）、⌥ 層特殊符號（™ £ ¢ ∞ … 見 L6 ABC Option 層） | L6、M-ACC | 偏好方案：英文模式**不攔截**一般按鍵，只在詞邊界（空白、標點）時以讀取 client 內容的方式提供建議；或乾脆讓使用者用 Caps Lock 切到真正的 ABC |

**第 8 節手動驗證步驟**
- **T8-1**：（等我們 IME 原型完成後）英文模式在 TextEdit 打「teh 」：分別測「IME 放行」與「IME 以 insertText 送出」兩種實作，看系統是否仍自動更正為「the」、是否出現灰色內文預測。
- **T8-2**：同上，觀察「顯示內文預測字詞」的灰字是否與我們的 marked text 同時出現；另在 ABC 下出現灰字時按 Tab，看是否接受預測（Apple 只寫 Space）。
- **T8-3**：TextEdit、注音－繁體，打 `56j4` Return 送出「植物」，再快速按兩下 Space：記錄出現「。」、「。 」、兩個空格、或其他；再到 ABC 打「hello」後連按兩下 Space 對照（預期「. 」）。
- **T8-4**：系統設定 > 鍵盤 > 文字輸入 > 編輯 > 所有輸入方式，截圖並記錄「顯示輸入建議」「顯示建議的回覆」的說明與預設值，並在「訊息」App 試用。

---

## 9. 實作總表：IME 自行處理 vs 放行（建議，依上列各節）

| 狀態 | 按鍵 | 建議處理 | 依據 |
|---|---|---|---|
| 組字區空 | 41 個注音鍵（含聲調鍵） | IME 處理：開始組字（空組字區按聲調鍵的行為待 T2-3） | 第 2 節 |
| 組字區空 | Return、Delete、fn+Delete、Esc、Tab、方向鍵、Home/End、Space | **放行** | L16（IMK 建議）、L12 |
| 組字區空 | `` ` `` `[` `]` `\` Shift+, Shift+. Shift+[ Shift+] 等標點 | IME 直接送出對應標點（寬度依 T5-3 校正） | 第 5 節 |
| 組字區空 | Shift+字母、Caps Lock＋字母、Option＋數字/字母 | IME 送出英文/數字（寬度依 T6-1、T6-2 校正）；或放行讓佈局處理 | 第 6 節 |
| 任何時候 | 帶 ⌘ 的組合 | **放行**（Cocoa 通常先交選單處理，D-EV）；若到 IME 且有組字區 → 暫定先 commit 再放行（依 T7-1 校正） | 第 7 節 |
| 組字區空 | 帶 ⌃ 的組合（emacs 綁定）、⌥＋方向/Delete、⌥Esc | **放行** | L12、K-SC |
| 組字區有內容 | 注音鍵、聲調鍵、Space、Return、Delete、⇧Delete、←→↑↓、Esc、Tab | IME 處理 | 第 3 節 |
| 組字區有內容 | ⌃／⌥ 編輯組合、Shift+字母、標點 | 待 T3-10、T6-1、T7-2 決定；暫定先 commit 再處理/放行 | 第 3、6、7 節 |
| 候選字視窗開著 | 方向鍵、`=` `-` `[` `]`、Space（翻頁/下一列）、數字鍵、Return（選取）、Tab/⇧Tab（排序）、Esc | IME 處理 | 第 4 節 |
| 系統層級 | 🌐︎/fn、⌃Space、⌃⌥Space、Caps Lock（切換用途）、⌘Space | 系統處理；IME 只需在 `commitComposition:`／`deactivateServer:` 收尾 | 第 1 節、L16 |
| Info.plist | `TICapsLockLanguageSwitchCapable = true`；各模式 `TISDoubleSpaceSubstitution = "。"`；`TISIntendedLanguage = zh-Hant` | 照 Apple TCIM 宣告 | L1、L15 |
| 輸入法選單 | ⌥⇧B 表情與標點符號、⌥⇧L 尋找輸入碼、⌥⇧H 半形標點、⌥⇧E 說明、⇧Delete 遺忘記憶字、轉換全形/半形、繁簡轉換（⌃⌥⇧⌘C／⌃⇧⌘C） | 依需要提供；注意這些 ⌥⇧ 組合在注音模式被 TCIM 拿去當指令（ABC 下 ⌥⇧B 等會輸出字元） | A-KEY、L3、L6 |

## 10. 未能確認清單（彙整；測試步驟見各節）

| 代號 | 項目 | 測試步驟所在 |
|---|---|---|
| U1 | 組字區有內容時切換輸入方式／點擊他處／⌘Tab：送出或丟棄 | T1-1 |
| U2 | 第三方 IME 宣告 `TICapsLockLanguageSwitchCapable` 後是否出現 Caps Lock 切換選項 | T1-2 |
| U3 | 單按 Shift 是否有任何作用（文件與設定皆無此功能，但未實測） | T1-3 |
| U4 | Caps Lock 長按門檻時間；`AppleCapsLockPressAndHoldToggleOff`、`AppleFnUsageType` 數值語意；「按住 fn 鍵來」選項 | 第 1 節備註 |
| U5 | 同槽位第二個符號、韻母先打、空組字區按聲調鍵、改聲調、不合法拼合 | T2-1～T2-5 |
| U6 | Delete／fn+Delete 在組字區的刪除粒度；⇧Delete 三種描述的實際差異 | T3-1、T3-2 |
| U7 | 組字區內 ←→、Home/End、⌘←、⌃A 的游標行為；↓ 是否開候選窗 | T3-3、T3-4 |
| U8 | Esc（組字區／候選窗） | T3-5 |
| U9 | Return 是否換行、無聲調時 Return、Shift+Return | T3-6 |
| U10 | Tab／⇧Tab 在注音是否有排序選項；視窗未開時 Tab | T3-7 |
| U11 | Shift+Space、⌥Tab 在注音的作用 | T3-8 |
| U12 | 組字區最大長度／自動送出 | T3-9 |
| U13 | 組字區有內容時打標點 | T3-10 |
| U14 | Page Up/Down；每頁候選數；預設候選窗方向（推論垂直）；送出後聯想詞；學習粒度 | T4-1～T4-5 |
| U15 | `` ` `` 鍵行為；標點替代候選的叫出方式；Shift+/ 等是否轉全形；⌥⇧H 在注音是否有效 | T5-1～T5-4 |
| U16 | Shift+字母、Caps Lock＋字母的寬度（L6 半形 vs N-RAY 全形）；⌥＋字母 | T6-1～T6-3 |
| U17 | 組字區有內容時 ⌘C/⌘V/⌘A/⌘Z、⌃A/⌃E/⌃K、⌥←、⌥Delete | T7-1、T7-2 |
| U18 | 注音模式按住鍵；IME 消化字母鍵時系統重音選單是否出現；注音模式 ⌥ dead key | T7-3、T7-4 |
| U19 | IME 送出英文時系統自動更正／內文預測是否仍作用；Tab 是否接受內文預測 | T8-1、T8-2 |
| U20 | 注音模式連按兩下空白的實際結果（「。」？） | T8-3 |
| U21 | 「顯示輸入建議」「顯示建議的回覆」的行為 | T8-4 |

## 11. 值得注意（可能出乎意料）的發現

1. 注音／倚天注音**用 Return 確認反白候選字，空白鍵在候選窗中是翻頁/換列**；其他中文輸入方式才是空白鍵選字（A-CW）。
2. 注音開候選字視窗要**按兩下空白鍵**（第一下是一聲；若碼已帶聲調並自動轉換，按一下即開窗）（A-CW、A-ZY）。
3. 在注音中要打阿拉伯數字須**按住 Option**（A-CW；佈局 Option 層＝ASCII 數字，L6）。
4. 標準注音佈局的 **Caps Lock 層是 ASCII 小寫字母**（與 ABC 的大寫相反）；Shift 層字母是 ASCII 大寫；倚天佈局的 Shift 層卻是**全形**（含全形小寫字母 ｑ）（L6、L7）。
5. 注音佈局的 Option 層**沒有 dead key**，⌥- 是「—」（L6）；ABC 的 ⌥E/⌥U/⌥N/⌥I/⌥` 才是 dead key。
6. 佈局本身：Shift+, ＝全形「，」、Shift+. ＝「。」、`\`＝「、」、`[ ]`＝「」、`` ` ``＝「·」(U+00B7)；但 Shift+/、Shift+1 等在佈局裡是半形（引擎是否轉全形未確認）（L6）。
7. 內建注音**沒有 Shift 切換中英**、也**沒有許氏／IBM 等佈局**，倚天是獨立輸入方式（A-SET、L1、L4）。
8. 「使用半形標點符號」**不適用於注音**（只列給拼音、雙拼、筆畫、粵語，A-SET），但 ⌥⇧H 仍列在繁中快速鍵表（A-KEY）。
9. 繁中刪節號預設是「⋯⋯」（U+22EF），簡中才是「……」（U+2026）（L9）。
10. 「加入句號（double-space）」在 zh_TW 介面叫「加入全形句號」；TCIM 各模式宣告 `TISDoubleSpaceSubstitution = "。"`（L1、L5）。
11. Apple 自家的鍵盤輸入法 bundle（含 TCIM、SCIM、韓文、日文、越南等）都宣告 `TICapsLockLanguageSwitchCapable = true`（L15），這很可能是第三方 IME 取得 Caps Lock 切換選項的關鍵（待 T1-2 驗證）。
12. ⇧Delete 在 Apple 文件中有三種說法：「移除最後一次輸入」（A-ZY）、「還原上一個替代項目」（A-KEY）、選單「清除所選的記憶字詞」（L3）。
13. macOS 27 的《中文和粵語輸入法使用手冊》與《Mac 使用手冊》全文可在本機 helpd 快取讀到（L11），離線查證方便。
