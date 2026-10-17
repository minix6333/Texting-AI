# 主題 A：英文 typo / spelling / grammar 即時修正（實體鍵盤、裝置端、keystroke 級）調研筆記

> 調研日期：2026-10-17。所有連結皆為實際開啟或於搜尋結果中標題吻合者；無法確認者標註「未能確認」。
> 「論文宣稱」與「我的評估」分開寫；我的評估以「【評估】」標示。
> 標記「背景」者為經典/較舊文獻；「近期」= 2024-10 ~ 2026-10。
>
> 檔案結構：§0 調研過程的 raw notes（逐步累積，含未採用細節）→「正式卡片」（16 張論文卡＋5 張短卡，分 5 節）→「## 設計啟示」（A–E）。撰寫報告時請以正式卡片與設計啟示為準。

---

## 0. 進行中工作紀錄（raw notes，逐步累積）

### [raw] Google Pixel 6 Gboard grammar correction (2021) — 已開啟原文
- URL: https://research.google/blog/grammar-correction-as-you-type-on-pixel-6/ (2021-10-27, Tony Mak & Simon Tong)
- seq2seq；Transformer encoder + LSTM decoder（品質/延遲折衷）；shared embedding、factorized embedding；8-bit post-training quantization（品質「not materially affected」）
- 20MB；60 input chars < 22ms on Pixel 6 CPU；打超過 3 個字才送 request；支援 sentence prefix（以 SmartCompose-like LM 補完 prefix 來判定 prefix 是否「可補成正確句」）
- hard distillation：數億句 web 英文句子經雲端大 grammar model 改寫，作為小模型訓練資料
- 輸出整句 → 以最小 Levenshtein 抽取 edit；UI = 底線 + 點擊出現替換建議（非自動替換）
- 無 precision/recall 數字（未公開）

### [raw] Proofread (Gboard, Liu et al. 2024) — 已開啟 arXiv abs + html
- arXiv 2406.04523；ACL Anthology 顯示為 ACL 2024 System Demonstrations（2024.acl-demos.27，搜尋結果所見，待確認）
- server-side PaLM2-XS；SFT（Rewrite → Proofread）+ RL（global / direct reward）；good ratio 85.56%（M5），bad ratio 14.44%
- Table 1：M0 PaLM2-XS good 65.48 / bad 34.52；M3 good 84.68；M5 good 85.56 / bad 14.44
- 延遲：TPU v5e，median 314.4ms → speculative decoding 190.6ms（-39.4%）；8-bit quant、bucket keys [16,32,64,128]、段落切分平行
- 合成錯誤 6 類：omission, insertion, transposition, double tap, omitted double chars, Gaussian positional（hello→jello）
- 定位：key correction / auto-correction / post-correction 只處理短距 typo；Proofread 做句/段層級；一鍵觸發（非即時）

### [raw] Dhakal et al. CHI 2018 136M keystrokes — 已讀 PDF 全文
- 168,960 participants，136,857,600 keystrokes；線上 copy-typing（抄打 15 句），實體鍵盤（瀏覽器）
- WPM 51.56 (SD 20.2)；IKI 238.66ms (SD 111.6)，下界約 60ms；fast typists (top10%, >~78WPM) IKI ~121.7ms；slow (<~26WPM) ~481ms
- Uncorrected error 1.17% (SD1.43)；90% 參與者 <2.66%；Error corrections（Backspace/Delete 鍵佔比）6.31% (SD 4.48)，平均 2.29 次/句；KSPC 1.17
- 錯誤類型（783 人子樣本，TextTest）：substitution 1.65% > omission 0.80% > insertion 0.67%（與早期研究 insertion 最多相反）；fast vs slow substitution 0.84% vs 3.72%
- Rollover ratio 平均 25%，fast typists 49.9%，slow 7.6%
- keypress duration 116ms
- 資料集：userinterfaces.aalto.fi/136Mkeystrokes（1.4GB zip，研究非商業用）
- CHI 2018 Best Paper Honorable Mention（Aalto 專案頁所列）

### [raw] Jack Cook 2023 Apple predictive text reverse engineering — 已開啟原文
- https://jackcook.com/2023/09/08/predictive-text.html
- 在 macOS Sonoma beta 找到 unilm.bundle（/System/Library/LinguisticData/RequiredAssets_en.bundle/AssetData/en.lm/unilm.bundle），含 unilm_joint_cpu.espresso.shape、unilm_joint_ane.espresso.shape（CPU 與 ANE 兩版）
- ~34M params（作者估），hidden 512，6 decoder blocks，vocab 15,000（含 100 emoji 等 special tokens）；GPT-2-like
- 注意：這是 predictive text（inline completion），作者未證實其用於 autocorrect；WWDC23 Apple 僅說 "transformer language model"
- 作者主觀：suggestions 幾乎即時出現

### [raw] Schmid et al. MUM '23 "Effects of Text Input Latency on Performance and Task Load" — 已讀 PDF 全文
- 頁面: https://hci.uni-regensburg.de/publications/effects_of_text_input_latency_on_performance_and_task_load_2024 ；PDF: https://epub.uni-regensburg.de/55007/1/text-input-latency.pdf ；DOI 10.1145/3626705.3627784
- 31 人、實體鍵盤、240Hz 螢幕；系統 base end-to-end latency 19.97ms；條件 20ms vs 200ms
- Copy task：time/char 290.9ms vs 302.7ms（p=0.086 不顯著）；錯誤率 0.11 vs 0.11 不顯著；但 NASA-TLX Effort (p=.022) 與 Frustration (p=.004, 20.94→31.32) 顯著上升
- Correction task（刪除 15 個錯字）：38.9s vs 46.5s（p<.001, d=1.673）；TLX 全部子量表顯著
- 31 人中 20 人自述察覺 200ms 延遲；察覺者打字較快（259.6 vs 347.9 ms/char）
- 相關工作引述：Shneiderman 建議打字 end-to-end 50–150ms；Ng et al. 觸控拖曳可察覺 <20ms（甚至 2ms）；Jota et al. 20–100ms 可感知；商用裝置典型 50–200ms
- 【評估】對 IME：echo 字元路徑絕不能等 LLM；修正必須非同步。編輯/修正情境（使用者回頭改字）對延遲更敏感 → revert/接受建議的互動要即時。

### [raw] Alharbi, Stuerzlinger, Putze, PACM-HCI ISS 2020 "The Effects of Predictive Features of Mobile Keyboards on Text Entry Speed and Errors" — 已開啟頁面
- https://vvise.iat.sfu.ca/pubs/alharbi2020frustration
- 170 人 crowdsourcing，行動 web 模擬（非實體鍵盤）
- word prediction 平均每句省 3.43 字元，但每次使用比直接打字多 ~2 秒 → 淨效果負；修正錯誤 autocorrection 平均 5.5 秒；autocorrection 對速度無顯著效果
- 【評估】錯誤的 auto-correct 代價（~5.5s）遠大於一次正確修正省下的時間 → 高 precision 門檻

### [raw] Banovic et al. MobileHCI 2019 "The Limits of Expert Text Entry Speed on Mobile Keyboards with Autocorrect" — 已開啟專案頁
- https://make4all.org/portfolio/expert-text-entry-with-autocorrect/
- 注意：是 MobileHCI 2019，不是 CHI 2019（使用者給的出處需更正）。相關前作 CHI 2017 "Quantifying Aversion to Costly Typing Errors in Expert Mobile Text Entry"（專案頁 https://make4all.org/portfolio/quantifying-aversion-to-costly-typing-errors-in-expert-mobile-text-entry/ 於搜尋結果所見，未開啟）
- 主張：即使有 autocorrect，使用者仍有 error aversion（會放慢、自行檢查），限制了高準確 autocorrect 的價值；模型可秒級模擬任意 autocorrect accuracy 下的打字速度

### [raw] Pillars of GEC (Omelianchuk et al., BEA 2024) — 已開啟 arXiv html
- https://aclanthology.org/2024.bea-1.3 ; arXiv 2404.14914
- Zero-shot LLM 全部 over-correct → precision 低：GPT-4-ZS CoNLL-14 P59.0/R55.4/F0.5 58.2；BEA-dev F0.5 43.0；Chat-LLaMa-2-7B-ZS CoNLL F0.5 43.7
- Fine-tuned LLaMa-2-7B: CoNLL P75.5/R46.8/F0.5 67.2；BEA-test 71.2；GECToR-2024 (~300M) CoNLL 66.0、BEA-test 73.1（precision 77.7）
- 10–50x 模型放大只帶來 1–2 F0.5；資料品質才是瓶頸；ensemble（majority voting）+3–6 F0.5；SOTA 72.8 CoNLL / 81.4 BEA-test
- 作者承認未評估速度

### [raw] Kernighan, Church, Gale COLING 1990 (背景) — 已 OCR 讀 PDF
- https://aclanthology.org/C90-2036/
- noisy channel：argmax P(w)·P(t|w)；候選 = Unix spell reject 的字經單一 edit（insertion/deletion/substitution/reversal）；四個 confusion matrix；AP newswire 1988（44M words）估計
- 評估：564 個「typo + 兩候選」triples 給 3 位評審；只取至少兩位評審同意的 329 例 → 與多數評審一致 87%；去掉 prior 或 channel 皆顯著變差
- 重要旁證：評審約 20% 認為 spell reject 的根本不是錯字（choice 0）→ 字典外 ≠ 錯字（專有名詞、新詞）→ false positive 問題
- 作者自承：評審看到上下文（concordance line）才敢判斷 → 建議加 trigram context

### [raw] Brill & Moore ACL 2000 (背景) — 已讀 PDF 全文
- https://aclanthology.org/P00-1037/
- generic string-to-string edits（如 ph→f、ent→ant），edit window 最多 4–5 字元，並以 edit 在字中的位置（開頭/中間/結尾）條件化
- 10,000 筆常見英文拼字錯誤（80/20 split），字典 ~200k
- 1-best：Church&Gale-style 89.5% → 本模型 95.0%（相對錯誤降低 52%）；3-best 98.8%；加 positional info 在 window 4 時相對 +22%
- 加 trigram LM（只用左側 context，因為「spell checker 通常在字打完後立刻執行」）→ 相對 CG+LM 再降 74% 錯誤
- 速度：Pentium III 500MHz 上每個字典外字串平均 ~50ms
- 【評估】資料是「認知型拼字錯誤」（不會拼），不是實體鍵盤手滑；兩者 error model 分佈不同，需各自估計

### [raw] Toutanova & Moore ACL 2002 (背景) — 已確認 ACL Anthology 頁面（搜尋結果）
- https://aclanthology.org/P02-1019/ ；加入 pronunciation（letter-to-phone）error model，與 Brill-Moore letter model 結合，對「同音誤拼」大幅改善
- 【評估】對實體鍵盤 typo（motor slip）幫助有限；但對 ESL/台灣使用者英文拼字（cognitive 錯誤）可能有用

### [raw] Damerau 1964 / Kukich 1992 (背景) — 二手來源
- Damerau 1964 CACM 7(3):171-176（Brill-Moore 參考文獻寫 659-664? 待查）—「約 80% 錯字為單一 edit（insertion/deletion/substitution/transposition）」此數字來自二手（講義、Kukich 引述）→ 原文未能確認
- Kukich 1992, ACM Computing Surveys 24(4):377–439, DOI 10.1145/146370.146380；三層問題：nonword detection → isolated-word correction → context-dependent correction；PDF: https://pubs.dbs.uni-leipzig.de/dc/files/Kukich1992Techniqueforautomatically.pdf（搜尋結果所見，未開啟）

### [raw] SymSpell（Wolf Garbe）— 已開啟 GitHub README
- https://github.com/wolfgarbe/SymSpell ；MIT（v6.1 起由 LGPL-3.0 改為 MIT）
- Symmetric Delete：只對字典與輸入生成 deletes → transposes/replaces/inserts 皆化為 deletes 的比對
- 作者 benchmark（2012 MacBook Pro 單核）：edit distance 2 → 0.033 ms/word；ED3 → 0.180 ms/word；vs BK-tree 1,870x（500k 詞典）；vs Norvig 1,000,000x（29k 詞典，ED3）；LookupCompound 0.2ms/word；WordSegmentation 185 字元 4ms
- 預設英文字典 82,765 詞；v6.5 起支援 bigram context
- 【評估】作為「候選生成器」極適合 keystroke 級即時；但純頻率排序無 keyboard-aware error model、無上下文 → 需要搭配 error model + LM rerank

### [raw] Ouyang, Rybach, Beaufays, Riley 2017 "Mobile Keyboard Input Decoding with Finite-State Transducers" — 已開啟 arXiv abs
- https://arxiv.org/abs/1704.03987 （arXiv preprint，無正式會議）
- Gboard 解碼器以 FST 表示 spatial model + lexicon + LM；支援 literal decoding、autocorrection、word completion、next-word prediction、post-correction
- 【評估】觸控鍵盤為主，但「spatial model + LM 在 FST 上做 beam decoding」的架構可直接借鏡到實體鍵盤（spatial model 換成 key-adjacency / timing model）

### [raw] arXiv 2308.01976 "Domain specificity and data efficiency in typo tolerant spell checkers: the case of search in online marketplaces"（搜尋結果摘要所見，未開啟全文）
- 宣稱：實際資料中某些物理上遠的鍵替換機率可能高於鄰鍵（部分是語音因素）→ 純 QWERTY 距離假設不足（未能確認細節）

### [raw] Staruch, Graliński, Dzienisiewicz, BEA 2025 "Adapting LLMs for Minimal-edit Grammatical Error Correction" — 已讀 PDF 全文
- https://aclanthology.org/2025.bea-1.9/ （arXiv 2506.13148）
- 模型大小對比（BEA-dev, 同樣 SFT 設定）：Qwen2.5-1.5B F0.5 53.86（P57.9/R42.1）；Llama-3.2-3B 59.39（P63.3/R47.5）；Gemma-2-9B 65.93（P68.8/R56.4）
- 方法 1（data augmentation）：把每個 (錯句→正句) 拆出一對 (正句→正句)，降低資料 error rate → 提升 precision（BEA-dev F0.5 60.34 → 66.92；P 60.74→71.42）
- 方法 2（training schedule）：最後階段先訓練「只有錯誤句」再以更低 LR 訓練「只有正確句」→ 以 LR 控制 P/R trade-off（LR 1e-7: P65.9/R58.2；4e-7: P77.5/R40.2；5e-7: P79.7/R24.8）——非常敏感
- 測試結果：Gemma-2-9B Training-Schedule BEA-test P79.87/R68.90/F0.5 77.41；Gemma-2-27B 78.70（單模型 SOTA，與 Karpo & Chernodub 2026 所說「差 0.38」吻合：78.32+0.38=78.70）；對照 GECToR (355M) BEA-test P80.70/R53.39/F0.5 73.21；CoNLL-14 64.00
- 作者自承：只用 greedy；LR 超敏感；模型太大不適合在使用者裝置上跑，可用來產生合成資料訓練小模型
- 【評估】對 IME 極具啟示：(a) 加大「無錯→照抄」比例是最便宜的 precision 旋鈕；(b) 1.5B 級 LLM 直接做 GEC 品質明顯不足，需 distill / 改做 tagging / 限定任務（只修 typo）

### [raw] Davis et al. Findings ACL 2024 "Prompting open-source and commercial language models for GEC of English learner text" — 已開啟 arXiv html
- arXiv 2401.07702（ACL Findings 2024, pp. 11952–11967 per DBLP 搜尋結果）
- 7 個開源 + 3 個商用 LLM；CoNLL-14 最佳 LLM F0.5 0.572 vs SOTA 0.696；FCE 0.463 vs 0.626；JFLEG GLEU 0.625 vs 0.647
- 錯誤類型：LLM 在 spelling（R:SPELL ~0.78 F0.5）、SVA、missing det 表現好；open-class noun/verb replacement 差（~0.22–0.29）
- 結論：minimal-edit GEC 仍以監督式模型最佳；LLM 只在 fluency 基準（JFLEG）接近；最小的模型偏 precision
- 【評估】spelling 是 LLM 相對擅長的類別 → 只做 typo/spelling 的小模型可能比全 GEC 更可行

### [raw] Coyne et al. 2023 (arXiv 2303.14342) / Fang et al. 2023 (arXiv 2304.01746) — 搜尋結果摘要所見
- Coyne, Sakaguchi, Galvan-Sosa, Zock, Inui：GPT-3.5 (text-davinci-003) / GPT-4 (gpt-4-0314) 在 JFLEG 強、在 minimal-edit（CoNLL-14、BEA）不及 SOTA；over-correct、extraneous fluency edits（引述自後續論文；原文細節未能逐一確認）
- Fang et al.：ChatGPT recall 高、precision/F0.5 低；人工標註 over-correction 指標；跨句一致性錯誤（agreement、coreference、tense）處理不佳

### [raw] PoCO — Park, Do, Lee, EMNLP 2025 main "Leveraging What's Overfixed: Post-Correction via LLM Grammatical Error Overcorrection" — 已開啟 Anthology 頁
- https://aclanthology.org/2025.emnlp-main.1431/
- sLM（監督微調）= 高 precision 低 recall；LLM = over-correct 低 precision；先讓 LLM 刻意 over-correct 拿 recall，再用小模型 post-correct 恢復 precision（數字頁面未列，未能確認）

### [raw] Goto, Sakai, Watanabe, arXiv 2605.13624 (2026-05, BEA 2026) "Edit-level Majority Voting Mitigates Over-Correction in LLM-based GEC" — 已開啟 arXiv abs
- training-free：同一模型取樣多個候選 → 只保留高頻 edit；9 個基準（英、捷、德、烏、韓、印地、羅馬尼亞）多數優於 greedy 與 MBR；對 prompt 變化較穩定（數字未列）
- 【評估】在 IME 上可用 "N 次取樣/多 beam 的 edit 共識" 當作 confidence → 只有共識 edit 才 auto-apply；但 N 倍運算成本

### [raw] Karpo & Chernodub, arXiv 2609.10810 (2026-09, EMNLP 2026 Findings) "Larger Context Window, Fewer Overcorrections" — 已開啟 arXiv abs
- prompt-based SOTA BEA-test F0.5 78.32（Gemini 3.1-Pro），距 fine-tuned 單模型 SOTA（Staruch 2025）0.38
- taxonomy-based 指令限制編輯範圍；batch 多句一起改 → edit rate 下降（作者假設 attention dilution）
- 【評估】雲端大模型才達到；對裝置端小模型的可遷移性未知

### [raw] Grammarly Engineering：On-device GEC at scale（T5）— 已開啟原文
- https://www.grammarly.com/blog/engineering/on-device-models-scale/ （更新日 2025-03-27；作者 Sri Malireddi）
- 原雲端 T5 GEC 需 ~4GB → 裝置端 <300MB；T5 70 → 297 tokens/s；目標：建議 <100ms 出現、pipeline ≥100 tokens/s；記憶體與延遲降 >50%
- 量化：試 INT4 與 BF16 → BF16 最佳（INT4 未達品質門檻）
- Mac 用 MLX 的 attention kernel + Metal；Rust SDK 跨平台；教訓：手動 graph 最佳化、去掉多餘 typecast/reshape/transpose、資料留在同一處理器、fused kernel
- 已部署到「數百萬」桌面使用者

### [raw] Grammarly Engineering：「One Model to Rule Them All」— 已開啟原文
- https://grammarly.com/blog/engineering/efficient-on-device-writing-assistance （更新日 2025-04-25；作者 Dhruv Matani 等，含 Omelianchuk、Skurzhanskyi）
- ~1B Llama-based decoder-only，統一 spelling + grammar；GQA；16-bit → 4-bit 量化，記憶體 -70%；MLX；M2 Mac ~210 tokens/s（目標 ≥50 tok/s）
- 合成資料：T5 模型生成 grammar 錯誤（C4_200M 子集）；特別注入 whitespace 錯誤（長而罕見的字）
- 弱點（自述）：多種合法拼法的專有名詞、冠詞位置、單句上過早的時態修正；「只在有信心時建議」（high precision）
- 無量化指標公開

### [raw] Apple iOS 17 autocorrect（WWDC23）— 9to5Mac 報導（未找到 Apple Newsroom 原文）
- https://9to5mac.com/2023/06/05/ios-17-iphone-autocorrect/
- "state-of-the-art transformer language model"；Apple silicon 讓模型「每次按鍵」都能跑；on-device、個人化；從使用者 revert 學習
- UI：被自動改的字「短暫加底線」，點擊可看到原字並 revert；inline sentence-level 預測，空白鍵接受
- macOS Sonoma：MacStories 等二手來源稱鍵盤 autocorrect 與 dictation 皆用 Transformer（未在 Apple 原文確認）

### [raw] Apple Foundation Models（2024 & 2025）— 已開啟原文（pr-mlr-shield-prod 鏡像）
- 2024：~3B on-device，mixed 2/4-bit 平均 3.7 bpw（可到 3.5 bpw）；iPhone 15 Pro TTFT ~0.6 ms/prompt token；生成 30 tokens/s（未含 speculation）；LoRA rank 16；vocab 49K
  - https://machinelearning.apple.com/research/introducing-apple-foundation-models
- 2025：~3B，decoder 2-bpw QAT、embedding 4-bit、KV cache 8-bit；兩 block 5:3、KV-cache sharing → KV 記憶體 -37.5%、TTFT 顯著改善；Foundation Models framework（guided generation、tool calling、LoRA rank 32 adapter；OS daemon 用 constrained + speculative decoding）
  - https://machinelearning.apple.com/research/apple-foundation-models-2025-updates
- 【評估】macOS 上可直接呼叫 Foundation Models framework 的 ~3B 模型做「句級 proofreading 後處理」，但 30 tok/s 級的生成速度不適合 keystroke 級；適合 idle/句末觸發

### [raw] Gboard Writing Tools（Gemini Nano, on-device）— 新聞來源
- Pixel 10（2025）首發，之後擴至 Pixel 9 與部分非 Pixel；on-device（Gemini Nano v2+）；2025-10 beta 出現「Google servers」切換（未確認是否上線）
- 來源：https://www.androidauthority.com/gboard-writing-tools-other-android-phones-3593589 、https://www.androidheadlines.com/2025/09/googles-gboard-ai-writing-tools-start-rolling-out-beyond-pixel-phones.html （皆搜尋結果所見，未開啟）

### [raw] GECToR (Omelianchuk et al., BEA 2020) — 已讀 PDF 全文
- https://aclanthology.org/2020.bea-1.16/ ；arXiv 2005.12592
- sequence tagging（$KEEP/$DELETE/$APPEND_x/$REPLACE_x + g-transformations），tag vocab 5000；encoder BERT/RoBERTa/XLNet；3-stage training；iterative（多數修正在前 2 輪：iter1 F0.5 61.5 → iter3 64.0）
- 單模型 XLNet：CoNLL-14 P77.5/R40.1/F0.5 65.3；BEA-19 test P79.2/R53.9/F0.5 72.4；ensemble 66.5 / 73.6
- 速度（Table 8, V100, batch 128, CoNLL-14 test 全集）：Transformer-NMT beam12 4.35s / beam4 1.25s / beam1 0.71s；GECToR 5 iterations 0.40s / 1 iteration 0.20s
- precision 旋鈕：$KEEP 機率加 confidence bias（0.10–0.35）＋句級 minimum error probability（0.40–0.66），random search on BEA-dev → P↑R↓，F0.5↑
- 【評估】confidence bias 是 IME 最實用的「保守度」旋鈕；tagging 單次 forward、可平行 → 延遲可預測

### [raw] Shallow Aggressive Decoding (Sun, Ge, Wei, Wang, ACL 2021) — 已開啟 ar5iv
- https://aclanthology.org/2021.acl-long.462 ；arXiv 2106.04970
- 機制：以輸入當 draft 平行解碼 → 驗證到第一個 divergence → 之後 greedy re-decode → 輸出後綴與輸入可唯一對齊時切回平行；保證與 greedy 相同輸出；再配淺 decoder（12+2 或 9+3）
- GPU V100 batch 1（CoNLL-13 1,381 句）：beam5 440s → SAD 54s（8.1x）；我算約 319ms → 39ms/句（論文未直接給每句數字）
- CPU（Xeon E5-2690 v4）：6.1x–8.0x
- 12+2 BART-init：CoNLL-14 F0.5 66.4（9.6x）、BEA-19 72.9（9.3x）；中文 NLPCC-18 29.4 vs 29.6（12.0x）

### [raw] Aggressive Decoding (Ge, Xia, Sun, Chen, Wei, arXiv 2205.10350, 2022) — 搜尋結果 + MSR 頁
- Input-guided AD（IAD，input ≈ output 的任務如 GEC）與 Generalized AD（GAD）；lossless；GEC/text simplification 7–9x；MT/摘要 3–5x；code in microsoft/unilm
- 【評估】這就是後來 LLM「prompt lookup / 以輸入當 draft 的 speculative decoding」的前身；對 IME 中「大部分 token 照抄」的修正任務是天然加速器

### [raw] CoEdIT (Raheja, Kumar, Koo, Kang; Findings EMNLP 2023) — 搜尋結果所見
- https://aclanthology.org/2023.findings-emnlp.350/ （preview 鏡像所見）；arXiv 2305.09857；HF grammarly/coedit-large
- 82K task-specific editing instructions 微調 Flan-T5；宣稱與最大的 instruction LLM 競爭但小 ~60x；公開資料集 69k（論文用 82k）
- 【評估】證明「任務專用 + 指令微調的小 seq2seq」可在編輯任務上打敗大型通用 LLM → 支持 IME 用專用小模型

### [raw] NeuSpell (Jayanthi, Pruthi, Neubig; EMNLP 2020 demo) — 已開啟 Anthology 頁
- https://aclanthology.org/2020.emnlp-demos.21/
- 10 個 spell checker；在自然拼錯資料上評估；以「從孤立拼錯反推的 in-context 合成錯誤」訓練 → 比隨機字元擾動 +9%（absolute）correction rate；更豐富的 context 表示再 +3%
- 【評估】合成錯誤要像真實錯誤（而非 uniform noise）是一貫結論

### [raw] Quinn & Zhai CHI 2016 "A Cost–Benefit Study of Text Entry Suggestion Interaction" — 已開啟 Google Research 頁
- https://research.google/pubs/a-costbenefit-study-of-text-entry-suggestion-interaction/ （CHI 2016 pp. 83–88）
- 建議永遠顯示/從不顯示/超過機率門檻才顯示；越積極的建議 → 鍵擊數減少、使用者偏好，但「注意與使用建議的成本」拖慢平均輸入時間（數字頁面未列）

### [raw] Palin et al. MobileHCI 2019 "How do People Type on Mobile Devices? ... 37,000 Volunteers" — 已開啟專案頁
- https://userinterfaces.aalto.fi/typing37k
- 37,370 人；36.2 WPM；uncorrected error 2.3%；autocorrect 使用與輸入速度正相關，word prediction 使用負相關（相關非因果）
- 【評估】行動裝置資料，但「autocorrect（不需注意力）好、prediction（需注意力）差」與 Quinn&Zhai 一致 → IME 應偏好「零注意力」的自動修正而非要使用者看建議列

### [raw] Li & Feit arXiv 2602.06489 (2026-02) "Simulating Word Suggestion Usage in Mobile Typing..." — 已開啟 abs
- WSTypist：RL 模擬打字者如何整合建議（orthographic processing、效益評估、個人 AI 偏好）；可做 what-if 設計分析（venue 未列，未能確認）

### [raw] Deber, Jota, Forlines, Wigdor CHI 2015 "How Much Faster is Fast Enough?" — 搜尋結果所見
- CHI 2015 Honorable Mention；direct vs indirect、tapping vs dragging 的延遲感知
- 二手部落格稱 tapping 約 ~70ms 以上會被察覺、dragging 低至 ~2ms → 原文數字未能確認

### [raw] GitHub Typo Corpus (Hagiwara & Mita, LREC 2020) — 搜尋結果
- https://aclanthology.org/2020.lrec-1.835 ；arXiv 1911.12893
- ~350k edits、15+ 語言、來自 GitHub commit；三特徵分類器辨識 typo edit F1 ~0.9；現有 spell checker 在此資料上 F ~0.5

### [raw] Birkbeck spelling error corpus (Roger Mitton) — 搜尋結果
- Oxford Text Archive ota:0643（https://llds.phon.ox.ac.uk/llds/xmlui/handle/20.500.14106/0643）；含兒童、成人識字班、Sheffield keyboard errors、劍橋入學考作文等；子集 35,610 個拼錯（5,964 字）
- Pedler & Mitton LREC 2010 real-word error confusion sets：https://aclanthology.org/L10-1077/ （搜尋結果所見）

### [raw] 合成 typo 生成
- MulTypo（Zhao, Liu, Altinger, Schütze, Hedderich；arXiv 2510.09536，ACL 2026）：依語言鍵盤佈局與打字行為生成 typo；18 個 LLM 在 typo 下一致退化，生成/推理任務最嚴重；instruction tuning 可能增加脆弱性 — 已開啟 abs
- Ghosh & Kristensson 2017 arXiv 1709.06429 "Neural Networks for Text Correction and Completion in Keyboard Decoding"：以 key 中心 2D Gaussian 取樣噪音（搜尋結果）
- Shah & de Melo LREC 2020 "Correcting the Autocorrect"（arXiv 2005.01158）：由小量標註資料統計錯誤分佈再注入大語料，產生需上下文才能偵測的錯誤；資料 typo.nlproc.org — 已開啟 abs
- Garg et al. arXiv 2609.04262（2026-09）日文音樂搜尋低延遲拼字修正：3+3 層 BART、byte-level BPE、QWERTY+flick 鍵盤模型 + query log 語音混淆先驗；GPU <4ms；EM 41.09%、CER 11.62% — 已開啟 abs

### [raw] 其他 2026 小模型/加速
- Lamelas arXiv 2601.03874（2026-01）：小 decoder-only LM 做 grammar correction/簡化仍不如強 baseline 與 LLM，語意保持與 hallucination 是問題（數字未列）
- Copy-as-Decode arXiv 2604.18170：v2 已撤回（authorship 問題）→ 不採用

### [raw] Grudin 1983 "Error Patterns in Novice and Skilled Transcription Typing"（背景）— 已 OCR 讀全文
- 收於 Cooper (Ed.) Cognitive Aspects of Skilled Typewriting, Springer, pp.121–143；PDF: https://gwern.net/doc/design/typography/1983-grudin.pdf
- Lessenberry (1928) 60,000 個打字錯誤 confusion matrix：substitution 中「同列相鄰鍵」43%、「同欄相鄰」15%、homologous（另一手同指鏡像鍵，如 d↔k）10%
- Grudin 自己的資料（~3,300 novice / ~500 expert substitutions）：同列相鄰 59%（novice）/ 31%（expert）；同指同欄 8% / 16%；homologous 16% / 4%；substitution 有 78%（expert）/ 72%（novice）在同一手內
- 專家最常見錯誤是 insertion，錄影顯示大多是 misstroke（一指同時按到兩鍵）；novice 以 substitution 為主
- 【評估】實體鍵盤 error model 至少要有：同列鄰鍵、同指上下鍵、鏡像鍵（homologous）、相鄰雙擊 insertion、跨手 transposition（rollover 時序）

### [raw] Wilcox-O'Hearn 2014 "Detection is the central problem in real-word spelling correction"（背景）— 已開啟 arXiv abs
- https://arxiv.org/abs/1408.3153 ；相關：Wilcox-O'Hearn, Hirst, Budanitsky CICLing 2008（重做 Mays, Damerau, Mercer 1991 trigram 模型）；Golding & Roth 1999 Machine Learning 34（Winnow, confusion sets）；Hirst & Budanitsky 2005 NLE 11(1)
- 主張：real-word 錯誤的難點是偵測；若把每個字都當可能錯字，trigram 無法在不引入更多錯誤的情況下找出真錯誤（需加結構）
- 【評估】直接支持 IME 只對「confusion set 白名單」（their/there、form/from…）做 real-word 修正，而非開放式

### [raw] 其他驗證
- Proofread 已確認：ACL 2024 Volume 3 System Demonstrations, pp. 286–293 — https://aclanthology.org/2024.acl-demos.27/
- BEA-2019 shared task：Bryant, Felice, Andersen, Briscoe — https://aclanthology.org/W19-4406/ （W&I+LOCNESS，ERRANT F0.5）


---

# 正式卡片（Paper cards）

> 共 16 張論文卡＋5 張短卡（產品/repo/資料集）。「結果」欄為論文/來源宣稱；「【評估】」為我的判斷。

## 1. 經典 noisy channel 與 error model（背景）

### A Spelling Correction Program Based on a Noisy Channel Model（背景）
- 出處: COLING 1990 | 連結: https://aclanthology.org/C90-2036/
- 問題: Unix `spell` 只會標出字典外的字，要如何替候選修正排序？
- 核心想法: 把 typo 看成正確字經過「噪音通道」後的輸出，以 argmax P(w)·P(t|w) 排序。候選只取單一 edit（insertion/deletion/substitution/reversal），P(t|w) 由四個 confusion matrix 估計（AP newswire 1988，約 44M words）。prior（語言）與 channel（打錯方式）分開估計，所以兩者可以各自換資料。
- 結果: 564 個「錯字＋兩個候選」交由 3 位評審判讀，取至少兩位意見一致的 329 例，程式與多數評審一致的比例是 87%；拿掉 prior 或 channel 都會顯著變差。評審在約 20% 的情況認為 spell 標出的字根本不是錯字。
- 為什麼重要: 奠定 noisy channel 框架。今天的鍵盤 decoder（spatial/error model × LM）仍是同一種分解。
- 侷限: 沒有上下文（作者自承評審要看到上下文才敢判斷，並建議加 trigram）；只處理單一 edit、只處理 non-word。【評估】「字典外 ≠ 錯字」約 20% 的比例提醒我們，專有名詞、新詞、程式碼識別字會帶來大量 false positive。
- 對我們輸入法的啟示: 架構上分成「鍵盤 error model × context LM」，error model 用使用者自己的 backspace 修正紀錄個人化；另需使用者字典與專有名詞白名單來壓低 false positive。
- 標籤: #noisy-channel #error-model #背景

### An Improved Error Model for Noisy Channel Spelling Correction（背景）
- 出處: ACL 2000 | 連結: https://aclanthology.org/P00-1037/
- 問題: 單字元 edit 的 channel model 太粗，表達不了 "ph→f"、"ent→ant" 這類多字元錯誤。
- 核心想法: 允許任意 string-to-string edit（window 最多 4–5 字元），並依 edit 在字中的位置（字首/字中/字尾）條件化；edit 機率從 (錯字, 正字) 對齊中學得。
- 結果: 10,000 筆常見拼錯（80/20 split，字典約 200k）上，1-best 從 Church & Gale 式模型的 89.5% 提升到 95.0%（相對錯誤降 52%），3-best 98.8%。再用 trigram LM rescore 5-best（只用左側上下文，因為 spell checker 通常在字打完後立刻執行），相對「CG error model + LM」錯誤再降 74%。Pentium III 500MHz 上每個字平均約 50ms。
- 為什麼重要: 證明 error model 的品質和 LM 一樣重要，好 LM 無法取代好 error model。後續 Toutanova & Moore（ACL 2002，https://aclanthology.org/P02-1019/）再加入發音 error model，處理同音誤拼。
- 侷限: 資料是「不會拼」的認知型錯誤，不是實體鍵盤手滑；只用左側 context。【評估】實體鍵盤 typo 的 edit 分佈（鄰鍵、雙擊、transposition）與這份資料差很多，必須另外估計；不過「依位置條件化」對 typo 也合理。
- 對我們輸入法的啟示: 準備兩種 channel：motor-slip（鍵盤幾何＋按鍵時序）與 cognitive（拼字/發音，台灣使用者的英文常見）。keystroke 級只用左側 context，剛好符合 IME 的即時限制。
- 標籤: #noisy-channel #error-model #背景

### Error Patterns in Novice and Skilled Transcription Typing（背景）
- 出處: Book chapter, Cooper (Ed.) *Cognitive Aspects of Skilled Typewriting*, Springer 1983, pp. 121–143 | 連結: https://gwern.net/doc/design/typography/1983-grudin.pdf
- 問題: 實體鍵盤的打字錯誤有什麼結構？哪些鍵最常被誤按？
- 核心想法: 用 Lessenberry（1928）的 60,000 個打字錯誤，加上作者自己的新手/熟手語料建立 confusion matrix，再用錄影分析錯誤成因（misstroke、homologous）。
- 結果: Lessenberry 資料中，substitution 有 43% 是同列相鄰鍵、15% 是同欄相鄰鍵、10% 是 homologous（另一隻手同一指的鏡像鍵，如 d↔k）。Grudin 自己的資料：同列相鄰 59%（新手）/31%（熟手）；同指同欄 8%/16%；homologous 16%/4%；熟手 78% 的 substitution 發生在同一隻手內。熟手最常見的錯誤是 insertion，錄影顯示大多是一指同時壓到兩鍵（misstroke）。
- 為什麼重要: 這是「實體鍵盤」（非觸控）error model 的經驗基礎。誤按不只來自左右鄰鍵，還有同指上下鍵、鏡像鍵和雙鍵 misstroke。
- 侷限: 1980 年代打字機與抄打情境，樣本小（熟手約 500 個 substitution）；部分表格經 OCR 無法完整讀出。【評估】現代筆電鍵盤（剪刀腳、鍵距）與 rollover 打法可能改變這些比例，應該用 Dhakal 的資料集重新估計。
- 對我們輸入法的啟示: channel model 至少要有這幾種特徵：同列鄰鍵、同指上下鍵、homologous、鄰鍵雙擊造成的 insertion、跨手 transposition。比例用 136M keystrokes 資料重估，再依使用者個人化。
- 標籤: #physical-keyboard #error-model #HCI #背景

### Detection is the Central Problem in Real-Word Spelling Correction（背景）
- 出處: arXiv 2014（未見正式發表場合）；前作 Wilcox-O'Hearn, Hirst & Budanitsky, CICLing 2008 | 連結: https://arxiv.org/abs/1408.3153
- 問題: real-word error（their/there、form/from）本身拼字正確，要怎麼發現？
- 核心想法: 已知 confusion set 時，用 trigram 區分「原字 vs 近似變體」並不難。真正的難點在偵測：當每個字都可能是錯字、又各有很多候選時，不加結構的 trigram 在找出真錯誤的同時會引入更多新錯誤。
- 結果: abstract 沒有量化數字（未能確認）。經典脈絡：Mays, Damerau & Mercer 1991 的 trigram 模型；Golding & Roth 1999（*Machine Learning* 34，Winnow + confusion sets）。
- 為什麼重要: 直接點出 real-word 修正的 over-correction 從何而來。
- 侷限: 預印本、n-gram 時代。【評估】神經 LM 改善了上下文判斷，但「開放式偵測 → 高 false positive」這個結構性問題仍在；LLM 的 over-correction 是同一現象的放大版。
- 對我們輸入法的啟示: real-word 修正只對高頻 confusion set 白名單做（their/there/they're、its/it's、then/than、form/from…），而且只畫底線、不自動替換。
- 標籤: #real-word #context-sensitive #precision #背景

## 2. 實體鍵盤打字行為、延遲與 autocorrect UX

### Observations on Typing from 136 Million Keystrokes
- 出處: CHI 2018, Best Paper Honorable Mention | 連結: https://userinterfaces.aalto.fi/136Mkeystrokes/ （DOI 10.1145/3173574.3174220）
- 問題: 一般人在實體鍵盤上打字，速度、錯誤與修正行為究竟如何？
- 核心想法: 透過線上打字測驗（抄打 15 句）收集 168,960 人、136,857,600 次按鍵的 keydown/keyup 時序，用標準 text-entry 指標分析並分群。
- 結果: 平均 51.56 WPM（SD 20.2）；平均 IKI 238.66ms，下界約 60ms，最快 10% 的人 IKI 約 121.7ms。Uncorrected error 1.17%；Backspace/Delete 占所有按鍵 6.31%（平均每句 2.29 次），KSPC 1.17。783 人子樣本中 substitution 1.65% > omission 0.80% > insertion 0.67%；最快組 substitution 0.84%，最慢組 3.72%。Rollover（上一鍵還沒放開就按下一鍵）平均 25%，快手達 49.9%。
- 為什麼重要: 這是實體鍵盤 typo 規模最大的實測。使用者自己就會修掉大部分錯誤（約 6% 的按鍵是修正），最後留下約 1%。
- 侷限: 抄打任務而非自由撰寫；樣本自選；時間戳來自瀏覽器（USB polling 約 10ms）。【評估】抄打時看得到原文，錯誤容易察覺；自由撰寫時錯誤分佈與 real-word 錯誤比例可能不同。
- 對我們輸入法的啟示: (1) 延遲預算：快手兩鍵之間只有約 120ms（下界 60ms），任何同步計算都必須遠小於這個數字；(2) 使用者本來就會用 backspace 修約 6% 的按鍵，IME 的價值在於省下這些 backspace、抓到漏網的約 1%，但不能干擾自我修正（使用者正在按 backspace 時不插入修正）；(3) rollover 很普遍，字元順序必須以 keydown 為準，並把 rollover 造成的 transposition 納入 error model；(4) 資料集可用來估計鍵盤 error model，但授權限研究與非商業用途。
- 標籤: #HCI #physical-keyboard #typing-behavior #latency-budget

### Effects of Text Input Latency on Performance and Task Load
- 出處: MUM 2023 | 連結: https://hci.uni-regensburg.de/publications/effects_of_text_input_latency_on_performance_and_task_load_2024 ；PDF https://epub.uni-regensburg.de/55007/1/text-input-latency.pdf
- 問題: 實體鍵盤輸入的端到端延遲（按鍵到字元出現）對打字與修改文字有什麼影響？
- 核心想法: 31 人、實體鍵盤、240Hz 螢幕，系統基準延遲實測 19.97ms；比較 20ms 與 200ms 兩種延遲，任務是抄打與修正（刪除 15 個錯字）。
- 結果: 抄打速度沒有顯著差異（290.9 vs 302.7 ms/字，p=0.086），錯誤率也相同（0.11 vs 0.11），但 NASA-TLX 的 Effort（p=.022）與 Frustration（20.94→31.32，p=.004）顯著上升。修正任務從 38.9s 變成 46.5s（p<.001，d=1.673），TLX 所有子量表都顯著惡化。31 人中有 20 人察覺到延遲，察覺者打字較快（259.6 vs 347.9 ms/字）。文獻回顧引述：Shneiderman 建議打字的端到端延遲在 50–150ms；觸控拖曳低於 20ms 就可能被察覺。
- 為什麼重要: 少數直接量測「實體鍵盤文字輸入延遲」的研究。熟練打字者可以靠盲打掩蓋延遲，但主觀負擔會上升；需要視覺回饋迴圈的編輯操作受害最大。
- 侷限: 只有兩個延遲水準，無法得出 JND 閾值；樣本小；實驗室情境。【評估】我們面對的是「修正出現的延遲」而非「字元回顯延遲」，但兩者都會影響編輯迴圈。
- 對我們輸入法的啟示: 字元回顯（IME 的 marked text/commit）路徑要維持系統原生水準（附加延遲遠小於 20ms），LLM 推論完全非同步。修正建議的出現、接受、revert 屬於「編輯任務」，對延遲更敏感：建議應在字結束後約 100–150ms 內出現，revert 必須即時完成（本地操作、不經推論）。
- 標籤: #HCI #latency #physical-keyboard

### A Cost–Benefit Study of Text Entry Suggestion Interaction
- 出處: CHI 2016 | 連結: https://research.google/pubs/a-costbenefit-study-of-text-entry-suggestion-interaction/
- 問題: 鍵盤上的修正/補完建議列真的讓人打得更快嗎？
- 核心想法: 抄打實驗中操弄建議呈現的積極程度（永遠顯示 / 從不顯示 / 超過機率門檻才顯示），量測鍵擊數、時間與偏好。
- 結果: 建議越積極，鍵擊數越少，受試者也更喜歡，但「注意並使用建議的成本」讓平均輸入時間變差（頁面未列具體數字，未能確認）。佐證：Palin et al. MobileHCI 2019（37,370 人，https://userinterfaces.aalto.fi/typing37k）發現 autocorrect 使用量與速度正相關，word prediction 使用量與速度負相關（相關不等於因果）。
- 為什麼重要: 建議的價值不等於省下的鍵擊數，注意力切換才是主要成本。
- 侷限: 行動觸控鍵盤、抄打任務。【評估】實體鍵盤的盲打者視線多半停在文字上而非建議列，注意力成本可能更高（必須把視線移到候選窗）。
- 對我們輸入法的啟示: 英文 typo 修正應採「零注意力」形式：高信心直接修（可 revert），中信心在原位畫底線（不彈候選窗），不要在每個字後面跳出候選列。
- 標籤: #HCI #suggestion-cost #UX

### The Effects of Predictive Features of Mobile Keyboards on Text Entry Speed and Errors
- 出處: ISS 2020（PACM HCI Vol. 4, ISS, Article 183） | 連結: https://vvise.iat.sfu.ca/pubs/alharbi2020frustration
- 問題: autocorrect 與 word prediction 的時間成本與效益各是多少？
- 核心想法: 170 人眾包，用行動 web app 模擬 autocorrect 與 prediction，量化每次使用的時間成本與修正錯誤的代價。
- 結果: word prediction 平均每句省 3.43 字元，但每次使用比直接打字多花約 2 秒，淨效果為負；修正一個錯誤的 autocorrection 平均要 5.5 秒；autocorrection 對整體速度沒有顯著影響。相關研究：Banovic et al. MobileHCI 2019（https://make4all.org/portfolio/expert-text-entry-with-autocorrect/）用計算模型指出，即使有 autocorrect，使用者的「錯誤厭惡」仍會讓他們放慢，削弱高準確 autocorrect 的價值（前作為 CHI 2017 "Quantifying Aversion to Costly Typing Errors in Expert Mobile Text Entry"）。註：題目中的「Banovic et al. CHI 2019」經查應為 MobileHCI 2019。
- 為什麼重要: 給出「一次錯誤 autocorrect 約花 5.5 秒」這個可以拿來設定 precision 門檻的數字。
- 侷限: 行動 web 模擬，不是實體鍵盤；眾包樣本。【評估】實體鍵盤上修正錯誤的 autocorrect 可能較快（有方向鍵與快捷鍵），但仍是秒級成本。
- 對我們輸入法的啟示: 用期望值設定 auto-apply 門檻。若一次正確修正省約 1 秒（少按 backspace 加重打）、一次錯誤修正花約 5 秒，則 precision 要超過約 83%（p×1 > (1−p)×5，即 p > 5/6）才值得自動替換。這是【我的推估】，需要實測校正。
- 標籤: #HCI #autocorrect-cost #precision-threshold

## 3. Neural GEC / spelling：速度、保守修正與 over-correction

### GECToR – Grammatical Error Correction: Tag, Not Rewrite
- 出處: BEA Workshop 2020 | 連結: https://aclanthology.org/2020.bea-1.16/ （arXiv 2005.12592）
- 問題: seq2seq GEC 推論慢，難以部署。
- 核心想法: 把 GEC 改成 token-level sequence tagging：每個 token 預測 $KEEP/$DELETE/$APPEND_x/$REPLACE_x 或 g-transformation（如改動詞形、改大小寫），tag 詞彙 5,000 個；Transformer encoder（BERT/RoBERTa/XLNet）只做一次 forward，可迭代 1–5 輪。
- 結果: 單模型 XLNet：CoNLL-14 P77.5/R40.1/F0.5 65.3；BEA-19 test P79.2/R53.9/F0.5 72.4；ensemble 66.5/73.6。在 V100、batch 128 下跑完整個 CoNLL-14 test：Transformer-NMT beam12 4.35s、beam1 0.71s；GECToR 5 輪 0.40s、1 輪 0.20s（約 10 倍）。大部分修正在前兩輪完成。推論時對 $KEEP 加 confidence bias（0.10–0.35）並設句級 minimum error probability（0.40–0.66），以 recall 換 precision。
- 為什麼重要: 「tag 而非 rewrite」天生就是 minimal-edit，延遲可預測，還附帶直接的保守度旋鈕。在 Pillars（2024）中，GECToR-2024（約 300M）仍與 7B–20B 模型同一級。
- 侷限: 需要英文特化的 g-transformation 詞彙；相依錯誤要多輪迭代；需要大幅改寫的錯誤 recall 低。【評估】字內字元錯誤的純 typo 修正不適合 word-level tag（tag 詞彙涵蓋不了任意拼錯），需要搭配 spelling 候選生成器。
- 對我們輸入法的啟示: 句級文法層可用 GECToR 式的小 encoder tagger（可蒸餾到 100M 以下並在 ANE 上跑），並把 $KEEP confidence bias 開放成「修正保守度」設定。
- 標籤: #GEC #tagging #latency #precision-knob

### Instantaneous Grammatical Error Correction with Shallow Aggressive Decoding
- 出處: ACL-IJCNLP 2021（long） | 連結: https://aclanthology.org/2021.acl-long.462 （arXiv 2106.04970）；後續 Ge et al. 2022, "Lossless Acceleration for Seq2seq Generation with Aggressive Decoding"（https://arxiv.org/abs/2205.10350）
- 問題: GEC 的輸出大部分和輸入相同，逐 token 自回歸解碼很浪費。
- 核心想法: 把「輸入」當作 draft，一次平行送進 decoder 驗證；接受到第一個分歧點為止，之後用 greedy 重解；當新生成的後綴能唯一對齊回輸入時，再切回平行模式。輸出保證與 greedy 相同（lossless）。再搭配深 encoder＋淺 decoder（12+2、9+3）。
- 結果: V100、batch 1、CoNLL-13 1,381 句：beam5 440s → 54s（8.1 倍；換算約 319ms → 39ms/句，換算是我做的）；CPU 上 6.1–8.0 倍。12+2 BART-init：CoNLL-14 F0.5 66.4（9.6 倍）、BEA-19 72.9（9.3 倍）；中文 NLPCC-18 加速 12.0 倍，品質幾乎不變（29.4 vs 29.6）。Ge et al. 2022 將其推廣為 Input-guided AD（GEC/簡化 7–9 倍）與 Generalized AD（MT/摘要 3–5 倍）。
- 為什麼重要: 這是今天 LLM「prompt lookup / 以輸入為 draft 的 speculative decoding」的前身。對「大部分照抄」的修正任務，延遲幾乎只剩一次 forward。
- 侷限: 錯誤越多加速越少；batch 1 才看得到效益。【評估】在 Apple Silicon 上，「驗證整句」那一次 forward 的成本約等於 prompt 處理（與 TTFT 同級），適合 30–60 token 的短句。
- 對我們輸入法的啟示: 如果用 decoder-only 小 LLM 做句級修正，一定要實作 input-as-draft 驗證（等同 llama.cpp/MLX 的 prompt lookup decoding），並設計輸出格式，讓「沒有錯」能在一次 forward 內判定。
- 標籤: #GEC #decoding #speculative #latency

### Pillars of Grammatical Error Correction: Comprehensive Inspection of Contemporary Approaches in the Era of Large Language Models
- 出處: BEA Workshop 2024 | 連結: https://aclanthology.org/2024.bea-1.3 （arXiv 2404.14914）
- 問題: 在 LLM 時代，各類 GEC 單模型、ensemble 與 LLM 實際排名如何？
- 核心想法: 在相同基準上系統比較 zero-shot LLM、fine-tuned LLM、T5/UL2 seq2seq、GECToR tagging 等，並測試 majority voting、GRECO、GPT-4 ranking 等組合方式。
- 結果: zero-shot LLM 全部 over-correct，precision 低：GPT-4-ZS CoNLL-14 P59.0/R55.4/F0.5 58.2（作者把 F0.5 60 視為品質可接受的下限），Chat-LLaMa-2-7B-ZS 只有 43.7；fine-tuned LLaMa-2-7B 67.2；GECToR-2024（約 300M）66.0，BEA-test 73.1（P77.7）。模型放大 10–50 倍只帶來 1–2 點 F0.5；majority voting ensemble 加 3–6 點；新 SOTA 為 CoNLL-14 72.8、BEA-test 81.4。
- 為什麼重要: 量化證明 zero-shot LLM 不適合直接當 minimal-edit 修正器，而約 300M 的 tagging 模型仍非常有競爭力。
- 侷限: 作者自承沒有評估速度；資料是英文學習者語料，不是母語者 typo。【評估】ensemble 的增益在裝置端不划算，但「多系統共識」可以當作 confidence 訊號。
- 對我們輸入法的啟示: 不要用 prompt 叫通用小 LLM 直接改句子。要嘛 fine-tune 成 minimal-edit，要嘛用 tagging；資料品質比模型大小重要。
- 標籤: #GEC #LLM #over-correction #benchmark

### Prompting open-source and commercial language models for grammatical error correction of English learner text
- 出處: Findings of ACL 2024 | 連結: https://arxiv.org/abs/2401.07702 （ACL Anthology 亦收錄）
- 問題: 開源與商用 LLM 用 prompting 做 GEC 的真實水準如何？在哪些錯誤類型上有差異？
- 核心想法: 7 個開源模型（Flan-T5-XXL 11B、BLOOMZ-7B 到 Llama-2-70B）加 3 個商用模型（GPT-3.5/4、Cohere），在 CoNLL-14、FCE、JFLEG、W&I 上以 zero/few-shot 評估，並做 ERRANT 錯誤類型分析。
- 結果: 最佳 LLM 在 CoNLL-14 F0.5 0.572，SOTA 為 0.696；FCE 0.463 vs 0.626；JFLEG（fluency）GLEU 0.625 vs 0.647，差距最小。錯誤類型方面，R:SPELL 表現最好（Falcon 0.781、GPT-3.5 0.769），SVA 與缺冠詞也不錯；開放詞類的 noun/verb 替換很差（約 0.22–0.29）。最小的模型偏 precision，GPT 系列 recall 最高。背景：Coyne et al. 2023（arXiv 2303.14342）與 Fang et al. 2023（arXiv 2304.01746）已指出 GPT 系列會 over-correct、做多餘的 fluency 改寫（這兩篇的細節取自摘要與後續引用，原文數字未能確認）。
- 為什麼重要: 指出 LLM 在「拼字」類別相對可靠，在需要語意判斷的替換類別則不可靠。
- 侷限: 2023 年代的模型；用 prompting 而非 fine-tune。【評估】2025 年後的小模型可能有改善，但 Staruch 2025 顯示 1.5B 級仍明顯落後。
- 對我們輸入法的啟示: 把 LLM 的任務收窄到「拼字/typo＋少數封閉類文法（SVA、冠詞）」，開放式詞彙替換一律不自動做。
- 標籤: #GEC #LLM #error-types #over-correction

### Adapting LLMs for Minimal-edit Grammatical Error Correction
- 出處: BEA Workshop 2025 | 連結: https://aclanthology.org/2025.bea-1.9/ （arXiv 2506.13148）
- 問題: decoder-only LLM 偏好流暢改寫，要怎麼調成 minimal-edit，並能控制 P/R？
- 核心想法: (1) 降低訓練資料的 error rate：每一對 (錯句→正句) 另外拆出一對 (正句→正句)；(2) training schedule：最後階段先只訓練錯誤句，再用更低的 learning rate 只訓練正確句，藉 LR 控制 precision/recall。另外用 Llama-3-70B 做資料 detokenize，過程中發現基準資料本身有錯。
- 結果: 同設定下 BEA-dev：Qwen2.5-1.5B F0.5 53.86、Llama-3.2-3B 59.39、Gemma-2-9B 65.93。加入「正→正」資料後 F0.5 從 60.34 升到 66.92（P 60.74→71.42）。最後階段 LR 從 1e-7 調到 5e-7，P 從 65.9 升到 79.7、R 從 58.2 降到 24.8（非常敏感）。BEA-test：Gemma-2-9B P79.87/R68.90/F0.5 77.41，Gemma-2-27B 78.70（單模型 SOTA）；對照 GECToR（355M）73.21（P80.70/R53.39）。
- 為什麼重要: 提供兩個便宜、可遷移的「保守化」訓練技巧，並直接給出小模型的品質落差數據。
- 侷限: 作者自承只用 greedy、LR 極敏感需要大量調參、模型對使用者裝置來說太大（建議用來產生合成資料訓練小模型）。【評估】1.5B 只有 53.86 F0.5，表示在 MacBook Air 上直接跑 1–2B LLM 做完整 GEC 品質不夠，必須限縮任務。
- 對我們輸入法的啟示: 訓練裝置端模型時讓至少一半樣本是「無錯→照抄」，並在最後階段「只餵正確句」，把 precision 拉到 UX 需要的水準；用 9B–27B 教師模型離線產生蒸餾資料。
- 標籤: #GEC #LLM #minimal-edit #precision-knob #2025

### Leveraging What's Overfixed: Post-Correction via LLM Grammatical Error Overcorrection（PoCO）＋ 2025–2026 over-correction 對策
- 出處: EMNLP 2025 main | 連結: https://aclanthology.org/2025.emnlp-main.1431/
- 問題: 監督式小模型（sLM）precision 高但 recall 低，LLM 則會 over-correct。兩者能互補嗎？
- 核心想法: 先刻意讓 LLM over-correct 以取得高 recall，再用微調過的小模型找出並修正第一階段的錯誤輸出，把 precision 拉回來。
- 結果: 摘要宣稱提高 recall 並維持「competitive precision」，頁面未列具體數字（未能確認）。同期相關工作：(a) EPO（Liang et al., COLING 2025；https://aclanthology.org/2025.coling-main.229/ ，於 Anthology 鏡像所見）在 preference optimization 中提高 edit token 的權重，Mistral-7B-EPO BEA-test 75.91（數字引自 Staruch 2025 的表格）；(b) Goto, Sakai, Watanabe（arXiv 2605.13624，BEA 2026）的 edit-level majority voting：同一模型取樣多個候選，只保留高頻 edit，在 9 個基準上多數優於 greedy 與 MBR；(c) Karpo & Chernodub（arXiv 2609.10810，EMNLP 2026 Findings）用錯誤類型 taxonomy 指令與多句 batching 降低 edit rate，純 prompt 在 BEA-test 達 F0.5 78.32（Gemini 3.1-Pro）；(d) JELV（arXiv 2511.21700）做 edit 級有效性判斷，蒸餾出的 DeBERTa 分類器對有效 edit 的 precision 為 85%。
- 為什麼重要: 2025–2026 的主流共識是：over-correction 是 LLM GEC 的核心問題，解法在「edit 級的驗證、共識與權重」，而不是整句接受。
- 侷限: 多數方法需要多次取樣或兩個模型（成本乘以 N）；(b)(c)(d) 為預印本或剛發表，數字未全部驗證。
- 對我們輸入法的啟示: 採「生成器（高 recall）＋ edit 級驗證器（高 precision）」架構。生成器可以是小 LLM 或候選生成器；驗證器是極小的 encoder（DeBERTa-small 等級），只替每個 edit 打分。通過門檻的 edit 才 auto-apply。
- 標籤: #over-correction #edit-verification #2025 #2026

## 4. 產業界裝置端鍵盤修正

### Grammar Correction as You Type, on Pixel 6（Google Research Blog）
- 出處: Google Research Blog, 2021-10-27（Tony Mak, Simon Tong） | 連結: https://research.google/blog/grammar-correction-as-you-type-on-pixel-6/
- 問題: 能不能在手機上「邊打邊改」整句文法，而且不送雲端？
- 核心想法: seq2seq 架構，Transformer encoder + LSTM decoder（在品質與延遲之間折衷）；用 shared/factorized embedding 縮小模型；8-bit post-training quantization；用雲端大模型對數億句 web 文字做 hard distillation；另外訓練「句子前綴」（用類似 SmartCompose 的 LM 補完，判斷前綴能否成為正確句），讓還沒打完的句子也能修。
- 結果: 模型 20MB；Pixel 6 CPU 上 60 字元輸入不到 22ms；使用者打超過 3 個字才觸發；模型輸出整句，再以最小 Levenshtein 距離抽出 edit；UI 是畫底線，點擊後顯示替換建議。沒有公開 precision/recall（未能確認）。
- 為什麼重要: 證明「小 seq2seq + 蒸餾 + 量化」可以在手機 CPU 上 25ms 內完成 as-you-type 文法修正。
- 侷限: 只支援英文，沒有公開品質數字，也不做自動替換。【評估】M4 的 CPU/ANE 遠強於 Pixel 6，同樣架構的延遲應該更低。
- 對我們輸入法的啟示: 句級文法層可以直接照這個配方做（約 20MB、prefix 訓練、底線 UI），並與 keystroke 級的拼字層分開。
- 標籤: #industry #on-device #seq2seq #distillation #latency

### Proofread: Fixes All Errors with One Tap
- 出處: ACL 2024 System Demonstrations（pp. 286–293） | 連結: https://aclanthology.org/2024.acl-demos.27/ （arXiv 2406.04523）
- 問題: 打字快的人不想逐字檢查，而 key correction、auto-correct、post-correct 都只處理短距 typo。要怎麼一鍵修好整句或整段？
- 核心想法: 伺服器端 PaLM2-XS。合成資料管線模擬鍵盤錯誤（omission、insertion、transposition、double tap、少打雙字母、Gaussian 位置誤差如 hello→jello）與文法錯誤；先 SFT（Rewrite→Proofread）再做 RL（global/direct reward）。部署時用 8-bit 量化、bucket（16/32/64/128）、段落切分平行處理與 speculative decoding。
- 結果: golden set 的 good ratio：PaLM2-XS 65.48% → SFT 後 84.68% → RL direct reward 85.56%（bad ratio 14.44%）。TPU v5e 上 median latency 從 314.4ms 降到 190.6ms（speculative decoding，−39.4%）。已上線 Pixel 8。
- 為什麼重要: 第一篇公開細節的「LLM 鍵盤修正」產品論文。它也顯示，即使用雲端 LLM，延遲仍在數百 ms，所以產品形式是「使用者主動一鍵觸發」，而不是即時修正。
- 侷限: 伺服器端執行；good ratio 依賴 LLM 判斷與人工 golden set；約 14% 的 bad ratio 若用於自動替換無法接受。【評估】合成錯誤類型清單可以直接沿用到實體鍵盤，只要把 Gaussian 位置誤差換成鍵盤鄰接機率。
- 對我們輸入法的啟示: 把「整句 LLM 修正」設計成使用者觸發（快捷鍵，或句末停頓後顯示 diff 預覽），不要做成 keystroke 級的自動替換；RL 中保留語意的 reward 值得借鏡。
- 標籤: #industry #LLM #keyboard #synthetic-noise #speculative-decoding

### [產品短卡] Apple iOS 17 / macOS Sonoma transformer autocorrect ＋ Jack Cook 逆向分析
- 連結: https://9to5mac.com/2023/06/05/ios-17-iphone-autocorrect/ ；https://jackcook.com/2023/09/08/predictive-text.html
- 是什麼: WWDC23 宣稱鍵盤改用「state-of-the-art transformer language model」，Apple silicon 讓模型每次按鍵都能跑；模型在裝置上、會個人化，也會從使用者的 revert 中學習。被自動修正的字會短暫加底線，點擊可看到原字並 revert；inline 預測按空白鍵接受。
- 關鍵數字: Jack Cook 在 macOS Sonoma beta 的 AppleSpell 相關路徑找到 unilm.bundle（含 CPU 與 ANE 兩版 espresso 模型），估計約 34M params、6 層、hidden 512、vocab 15,000，架構類似 GPT-2。
- 相關性: 證明約 30M 級 transformer 每鍵推論在 Apple silicon（含 ANE）上可行；Apple 的 UX 答案是「自動修＋短暫底線＋一鍵 revert＋從 revert 學習」。
- 侷限: Cook 找到的是 predictive text 模型，未證實就是 autocorrect 模型；Apple 沒有公開架構與指標；macOS 上 transformer autocorrect 只有二手來源（未能在 Apple 原文確認）。
- 標籤: #industry #apple #on-device #UX-revert

### [產品短卡] Grammarly 裝置端 GEC（兩篇工程部落格，2025）
- 連結: https://www.grammarly.com/blog/engineering/on-device-models-scale/ ；https://grammarly.com/blog/engineering/efficient-on-device-writing-assistance
- 是什麼: (1) 把雲端 T5 GEC（約 4GB）搬到桌面裝置：縮到 300MB 以下，T5 速度從 70 提升到 297 tokens/s；目標是建議在 100ms 內出現、pipeline 至少 100 tokens/s。INT4 品質不達標，改用 BF16；Mac 上用 MLX 的 attention kernel 與 Metal；已部署給數百萬桌面使用者。(2) 約 1B 的 Llama-based 模型統一處理 spelling 與 grammar，使用 GQA、4-bit 量化（記憶體 −70%）、MLX，M2 Mac 上約 210 tokens/s（門檻為 50）；合成資料特別注入 whitespace 錯誤。
- 相關性: 與我們硬體（Apple silicon Mac）最接近的公開工程數據。
- 侷限: 沒有公開品質指標。自述弱點：有多種合法拼法的專有名詞、冠詞位置、單句上過早的時態修正。
- 標籤: #industry #on-device #apple-silicon #MLX #quantization

### [產品短卡] 2024–2026 系統級裝置端 LLM 校對：Apple Foundation Models / Writing Tools、Gboard Writing Tools（Gemini Nano）
- 連結: https://machinelearning.apple.com/research/introducing-apple-foundation-models ；https://machinelearning.apple.com/research/apple-foundation-models-2025-updates ；Gboard 新聞：https://www.androidauthority.com/gboard-writing-tools-other-android-phones-3593589
- 是什麼: Apple 約 3B 的 on-device 模型。2024 版：mixed 2/4-bit 平均 3.7 bpw，iPhone 15 Pro 上 TTFT 約 0.6ms/prompt token、生成 30 tokens/s。2025 版：2-bpw QAT、KV-cache sharing 讓 KV 記憶體降 37.5%，並透過 Foundation Models framework 開放給開發者（含 guided generation 與 rank-32 LoRA adapter）。Gboard Writing Tools 在 2025 年隨 Pixel 10 推出，以 Gemini Nano 在裝置端做 proofread 與 rephrase。
- 相關性: 在 macOS 26 上，我們或許能直接呼叫系統的約 3B 模型做「句級校對」，不必自帶 LLM（IME 進程能否使用此 framework 未能確認）。
- 侷限: Apple 公開資料沒有 Writing Tools/Proofread 的延遲或品質數字；30 tok/s 等級的生成速度不適合 keystroke 級；Gboard 的細節多來自新聞。
- 標籤: #industry #on-device-LLM #apple #gboard

### [Repo 短卡] SymSpell（Symmetric Delete spelling correction）
- 連結: https://github.com/wolfgarbe/SymSpell
- 是什麼: 預先對字典生成 deletes；查詢時只對輸入生成 deletes 並查表，把 insert/replace/transpose 都轉成 delete 比對。v6.1 起採 MIT 授權。
- 關鍵數字（作者在 2012 MacBook Pro 單核上的 benchmark）: edit distance 2 每字 0.033ms、ED3 每字 0.180ms；比 BK-tree 快 1,870 倍；LookupCompound 每字 0.2ms；WordSegmentation 處理 185 字元需 4ms；預設英文字典 82,765 詞。
- 相關性: keystroke 級「候選生成」最好的現成方案，0.1ms 以內，遠低於 IKI 預算。
- 侷限: 排序只看 edit distance 與詞頻，沒有鍵盤 error model，上下文只有可選的 bigram；記憶體隨字典大小與 edit distance 增長；數字為作者自測。
- 標籤: #spelling #candidate-generation #latency

## 5. 資料集與合成 typo

### [資料集短卡] Typo / GEC 評測資料與合成 typo 生成
- GitHub Typo Corpus（Hagiwara & Mita, LREC 2020, https://aclanthology.org/2020.lrec-1.835）：約 350k edits、15+ 語言，取自 GitHub commit 的真實 typo；三特徵分類器挑出 typo edit 的 F1 約 0.9；既有 spell checker 在這份資料上只有約 0.5 F。【評估】最接近「實體鍵盤＋開發者」的真實 typo，但混有程式碼與文件語境。
- Birkbeck spelling error corpus（Roger Mitton；Oxford Text Archive https://llds.phon.ox.ac.uk/llds/xmlui/handle/20.500.14106/0643）：包含兒童、成人識字班、Sheffield keyboard errors 等子集；一個工作子集有 35,610 個拼錯（5,964 個字）。偏向認知型錯誤。real-word confusion sets 見 Pedler & Mitton LREC 2010（https://aclanthology.org/L10-1077/）。
- CoNLL-2014（Ng et al., https://aclanthology.org/W14-1701/）：NUS 學習者作文，M2 scorer，F0.5（precision 權重是 recall 的兩倍）。
- BEA-2019（Bryant et al., https://aclanthology.org/W19-4406/）：W&I+LOCNESS，ERRANT F0.5，test set 盲測（Codalab）。
- JFLEG（Napoles, Sakaguchi, Tetreault, EACL 2017, https://aclanthology.org/E17-2037/）：fluency 改寫，GLEU 指標；作者指出當時系統常漏掉拼字錯誤；CC BY-NC-SA 4.0。
- 合成 typo：Proofread 的 6 類鍵盤噪音（見上）；NeuSpell（EMNLP 2020 demo，https://aclanthology.org/2020.emnlp-demos.21/）用「從真實孤立拼錯反推的 in-context 錯誤」訓練，比隨機擾動多 9%（absolute）correction rate；Shah & de Melo（LREC 2020，https://arxiv.org/abs/2005.01158）先從小量標註資料統計錯誤分佈，再注入大語料；MulTypo（Zhao et al., ACL 2026，https://arxiv.org/abs/2510.09536）依各語言的鍵盤佈局與打字行為生成 typo，18 個 LLM 在 typo 下一致退化；Ghosh & Kristensson 2017（https://arxiv.org/abs/1709.06429）以按鍵中心的 2D Gaussian 取樣（觸控）。CoEdIT（Findings EMNLP 2023，https://arxiv.org/abs/2305.09857）則顯示任務專用、指令微調的小 seq2seq 能在編輯任務上與大 60 倍的 LLM 競爭。
- 對我們輸入法的啟示: 學術 GEC 基準（CoNLL/BEA/JFLEG）是學習者作文、句級、離線評估，反映不了「實體鍵盤＋keystroke 級＋自動替換」。我們需要自建評測：用 Dhakal 136M keystrokes（含 keydown 時序與 backspace 歷程）重建「意圖文字 vs 實際按鍵」配對，再加上 GitHub Typo Corpus 的英文子集；主指標用 precision@auto-apply、每千字誤改數與 revert 率。
- 標籤: #dataset #benchmark #synthetic-noise


---

## 設計啟示

> 以下把「文獻證據」與「【評估】/【推估】」分開。【推估】表示我依文獻數字推算、尚未實測。

### A. 裝置端 word-level typo 修正：合理的 precision / recall 預期

文獻中的錨點：
- 「已知是錯字」時的孤立 non-word 修正 top-1：KCG 1990 與評審一致 87%（兩候選情況）；Brill & Moore 2000 為 95.0%（認知型拼錯，加 trigram LM 後更高）。
- 偵測端的 false positive：KCG 1990 中約 20% 被 spell 標出的字其實不是錯字；GitHub Typo Corpus 上現成 spell checker 只有約 0.5 F。
- LLM 在拼字類別相對可靠：W&I dev 上 R:SPELL F0.5 約 0.77–0.78（Davis 2024）。
- 完整 GEC 的上限：GECToR 單模型 P 約 77–80、R 約 40–54；調過的 Gemma-2-9B 在 BEA-test P 79.9/R 68.9；Qwen2.5-1.5B 在 BEA-dev 只有 F0.5 53.9（Staruch 2025）。句級 LLM 產品 Proofread 的 good ratio 是 85.6%（使用者主動觸發）。
- 使用者行為：使用者自己會用 backspace 修約 6.3% 的按鍵，最後只留下約 1.17% 的錯誤（Dhakal 2018）；一次錯誤 autocorrect 平均花 5.5 秒修正（Alharbi 2020）。

【評估】分層目標：
- T1 non-word typo（字典外＋鍵盤 error model＋左側 context LM＋使用者字典）：auto-apply 的 precision 應以 ≥97–98% 為目標（約每 40–50 次自動修正最多錯 1 次），可接受 recall 約 50–70%。期望值門檻（p > 5/6 ≈ 83%）只是理論下限；error aversion 讓使用者對信任流失更敏感，所以實務門檻要高得多。
- T2 real-word（confusion set 白名單）與封閉類文法（SVA、冠詞、單複數）：precision 約 70–85%、recall 約 30–50% 是現實範圍 → 只畫底線。
- T3 開放式改寫（詞彙替換、語序、fluency）：小模型 precision 明顯低於 70% → 只在使用者觸發時提供，附 diff。

### B. 自動替換 vs 底線

| 層級 | 觸發時機 | 動作 | 條件 |
|---|---|---|---|
| T1 自動替換 | 字邊界（空白/標點） | 直接替換＋短暫標示＋一鍵 revert | non-word、最佳候選與次佳 margin 大、不在使用者字典/專有名詞/程式碼情境、使用者沒有正在按 backspace |
| T2 底線 | 字邊界或停頓 ≥300ms | 原位底線（marked text 或游標附近 overlay），點擊或快捷鍵套用 | real-word 白名單、封閉類文法、未達 T1 門檻的 typo |
| T3 按需 | 快捷鍵或句末長停頓 | diff 預覽，逐 edit 接受 | 整句/整段 LLM proofread |

細節：
- revert：自動修正後的第一次 Backspace（或 Esc/⌘Z）還原原字。revert 後同一 session 內不再修同一個 token；同一字被 revert 兩次就加入使用者字典。Apple iOS 17 的做法也是短暫底線、點擊 revert，並從 revert 中學習。
- 預設不修：含數字、camelCase、全大寫、URL/email/路徑的 token；Terminal、IDE 等程式碼 app 預設關閉，並提供 per-app 設定。
- 只動游標附近的最後一兩個字。決策若在期限內（建議約 50–80ms，低於快手 IKI 約 120ms）沒完成就降級成底線或放棄，避免使用者已經在打下一個字時文字「跳動」。
- 【評估，待另題驗證】macOS IMK 通常只能替 marked text 加樣式，對已 commit 到其他 app 的文字畫底線不見得做得到。T2 底線可能要靠「讓最後一個字保持 marked text」或游標附近的 overlay 視窗；替換已 commit 文字則依賴各 app 對 replacementRange 的支援程度。

### C. 低延遲架構與模型大小對延遲的數據

建議的分層管線（【評估】）：
1. 按鍵路徑（同步，目標 <1ms）：回顯字元、更新 buffer、增量更新候選（prefix trie 或 SymSpell，作者測得 ED2 每字 0.033ms）；同時記錄鍵盤 error model 特徵（鄰鍵/同指/鏡像/雙擊/transposition，加上 IKI 與 rollover 時序）。
2. 字邊界路徑（非同步，預算約 20–30ms）：以 channel model × 小 LM（n-gram 或約 10–50M 的 transformer，跑在 ANE/CPU；參考 Apple 鍵盤 LM 約 34M）排序候選，決定 T1 或 T2。
3. 句/停頓路徑（非同步，50–300ms）：GECToR 式 tagger（100–300M 以下）、Pixel 6 式蒸餾 seq2seq（約 20MB），或微調成 minimal-edit 的 0.5–1.5B LLM 搭配 input-as-draft 驗證（SAD/AD/prompt lookup），再加 edit 級驗證器；結果只畫底線。
4. 按需路徑：系統 Foundation Models（約 3B）或自帶 1–3B 模型，以 diff UI 呈現。
5. 能耗：M4 Air 無風扇，避免持續 decode。採事件驅動，新按鍵到來就取消過期工作（debounce）；模型常駐以避免冷啟動；小型常駐模型優先放 ANE（Cook 發現 Apple 鍵盤 LM 也有 ANE 版本）。

模型大小與延遲（文獻/產品宣稱，硬體各異，不可直接比較）：

| 系統 | 模型 | 硬體 | 數字 |
|---|---|---|---|
| SymSpell | 82k 字典 | 2012 MacBook Pro 單核 | 每字 0.033ms（ED2）/ 0.180ms（ED3） |
| Brill & Moore 2000 | noisy channel | Pentium III 500MHz | 每字約 50ms |
| Apple 鍵盤 LM（逆向） | 約 34M、6 層 | Apple silicon CPU/ANE | Apple 宣稱每次按鍵都推論；未公開延遲 |
| Pixel 6 grammar | seq2seq、20MB、int8 | Pixel 6 CPU | 60 字元 <22ms |
| SAD（ACL 2021） | Transformer 12+2 / 9+3 | V100、batch 1 | 比 beam 快 8–10 倍；約 39ms/句（我的換算） |
| GECToR | 約 300M encoder | V100、batch 128 | 整個 CoNLL-14 test 1 輪 0.20s（吞吐量，非單句延遲） |
| Grammarly T5 | <300MB、BF16 | Mac（MLX） | 297 tok/s；目標 <100ms |
| Grammarly 1B | 4-bit Llama-based | M2 Mac（MLX） | 約 210 tok/s |
| Apple FM 2024 | 約 3B、3.7 bpw | iPhone 15 Pro | TTFT 約 0.6ms/prompt token；30 tok/s |
| Proofread | PaLM2-XS | TPU v5e（伺服器） | median 190.6ms（speculative decoding 後） |

【推估】在 M4 Air 上，1B 4-bit 模型每個 decode token 約 4–5ms（由 M2 的 210 tok/s 推得，M4 應更快）。一個 20 token 的句子若用 input-as-draft：1 次驗證 forward（約 10–30ms prefill）加 k 個修正 token × 約 5ms，總計約 30–80ms。3B 模型大約慢 2–3 倍。這些都必須實測。

### D. Over-correction 與 revert 的 UX 教訓

1. 錯誤的修正比漏修貴約 5 倍（Alharbi 2020：修正錯誤 autocorrect 平均 5.5 秒），而 error aversion（Banovic 2017/2019）會讓使用者因幾次壞經驗就放慢打字、自行檢查，autocorrect 的價值隨之縮水。所以要優化 precision，並以 revert 率作為線上的 precision 代理指標。
2. LLM 預設就會 over-correct（Pillars 2024、Davis 2024、Coyne/Fang 2023）。已驗證的對策：minimal-edit 微調並加入照抄配對（Staruch 2025）、edit 級驗證器（JELV、PoCO）、取樣共識（Goto 2026）、$KEEP confidence bias（GECToR）、限定錯誤類型的指令（Karpo 2026）。
3. 建議會消耗注意力（Quinn & Zhai 2016），所以英文模式下避免彈出候選窗，優先採用安靜的自動修正或底線。
4. 延遲：即使打字速度不受影響，200ms 延遲也會提高挫折感（Schmid 2023）；修正晚到則會造成文字跳動。因此要設決策期限，而且不改游標已經離開的文字。
5. 從 revert 中學習（Apple 的做法）：revert 後不重犯同一修正，並累積個人字典與個人 error model（例如某人常把 "teh" 打成 "the"，但另一人刻意寫 "teh"）。
6. 情境感知：程式碼、專有名詞、台灣常見的中英混打（中文句中夾英文單字時英文上下文很短）。這些情況應更依賴 channel model 與使用者字典，並降低 auto-apply 比例。
7. 評測指標要對齊 UX：precision@auto-apply、每千字誤改數、revert 率、backspace 減少量、修正延遲 p95。學術 F0.5 只能當參考。

### E. 尚未解決／需實驗確認
- 實體鍵盤＋自由撰寫情境下的 typo 分佈與 real-word 比例（Dhakal 是抄打）。
- 實體鍵盤 autocorrect 的 HCI 研究很少（多數為行動觸控），「可接受的誤改率」沒有直接文獻，需自己做使用者研究。
- macOS IMK 對已 commit 文字的替換與底線能力，因 app 而異（另題）。
- Apple Foundation Models framework 能否在 IME 進程中低延遲呼叫、是否有速率限制（未能確認）。
- 0.5–1.5B 模型限縮為「typo＋封閉類文法」後的實際 precision，目前沒有公開數據，需自建基準。
