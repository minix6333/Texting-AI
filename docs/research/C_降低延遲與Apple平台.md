# 主題 C：裝置端近零延遲 LLM 文字校正技術與證據（Apple Silicon）

> 建立日期：2026-10-17。研究筆記（逐步追加）。
> 規則：所有連結皆為實際開啟或於搜尋結果中標題吻合者；無法確認者標「未能確認」。
> 「來源宣稱」與「我的評估」分開寫；我的評估以「（評估）」標示。

---

## 卡片索引（共 32 張；★＝核心論文卡 19 張，其餘為家族總表／產業／執行環境／模型短卡）

- 第一部分（解碼加速）：★SAD（ACL 2021）｜★Aggressive Decoding IAD/GAD（2022）｜★LLMA（2023）｜PLD（GitHub）｜★Speculative Decoding（ICML 2023 Oral）｜★Speculative Sampling（DeepMind 2023）｜★Spec 變體家族總表（Medusa/EAGLE-1/2/3/Lookahead/REST/LayerSkip/SpecInfer）｜★CopySpec + PLD+（2025）｜★AdaEdit（ACL 2026 Findings）｜★編輯式模型總表（LaserTagger/Seq2Edits/FELIX/EdiT5/GECToR）｜Pixel 6 Gboard 文法校正（產業）｜Grammarly 裝置端 1B（產業）
- 第二部分（增量推論）：★Prompt Cache（MLSys 2024）｜APC/RadixAttention（vLLM 文件／NeurIPS 2024）｜★CacheBlend（EuroSys 2025）｜★PatchKV + KV-as-notes（2026）｜KV 截斷/回滾實作（mlx-lm/llama.cpp）｜★Two-pass ASR + Deliberation｜★Re-translation（IWSLT 2020）
- 第三部分（打分與成本）：★Tree attention/共享前綴｜成本模型（M4 推導）
- 第四部分（壓縮）：★GPTQ + AWQ｜★小模型 QAT（Gemma 3 QAT/Meta/AFM/ParetoQ）｜MLX 量化與 K-quants
- 第五部分（Apple 執行環境）：llama.cpp #4167｜Apple M5 vs M4 MLX｜社群跨 runtime 基準｜Core ML/ANE（含 ANEMLL）｜Core AI + Foundation Models 2026｜★AFM Tech Report 2025 + AFM 3｜功耗/散熱缺口
- 第六部分（感知）：★延遲感知文獻群
- 表格：延遲預算建議｜小模型表｜執行環境數字總表｜設計啟示｜未解問題

---

## 第一部分：輸出≈輸入時的解碼加速（校正／編輯）

### Instantaneous Grammatical Error Correction with Shallow Aggressive Decoding (SAD)
- 出處: ACL 2021 (main, long; 2021.acl-long.462) | 連結: https://arxiv.org/abs/2106.04970 ；https://aclanthology.org/2021.acl-long.462
- 問題: Transformer seq2seq GEC 線上（batch=1）推論太慢，無法做「即時」改錯。
- 核心想法: (1) Aggressive decoding：假設輸出≈輸入，直接把輸入 token 當 decoder 輸入一次平行驗證；遇到第一個分歧點（bifurcation）就丟掉其後預測、改回逐字 greedy，re-decode 後若輸出後綴能在輸入中唯一對齊，則再切回平行模式。因每一步都經目標模型驗證，輸出與 greedy 完全相同（lossless）。(2) Shallow decoder：把層數從 6+6 改成 9+3、12+2（深 encoder 淺 decoder），因為 decoder 是逐步執行的瓶頸。
- 結果: V100、batch=1：aggressive decoding 單獨對 Transformer-big 約 7–8×（CoNLL-13：beam5 440s → aggressive 54s，8.1×）；9+3 + aggressive 在 CoNLL-14 約 10.5×；最終 12+2（BART init）CoNLL-14 F0.5 66.4（9.6×）、BEA-19 F0.5 72.9（9.3×）。CPU（Xeon，2/8 threads）亦有 6.1–8.0×。中文 NLPCC-18：9+3 F0.5 29.4（基線 29.6）且 12.0× 加速。
- 為什麼重要: 首個證明「校正任務的輸出幾乎抄寫輸入」可以被直接拿來做免草稿模型的 lossless 平行解碼；（評估）也是後來 draft-then-verify／speculative decoding 一脈的思想前身之一。
- 侷限: 作者承認編輯多的句子因頻繁 re-decode 加速有限，且被丟棄的預測帶來額外計算；限制 aggressive 長度反而變慢（Lmax=2 僅 1.6×）。（評估）論文是 encoder-decoder 小模型 + GPU；decoder-only LLM 上要換成「把原句當 draft」的 speculative 驗證（即後來的 IAD/PLD），且中文資料為簡體 NLPCC。
- 對我們輸入法的啟示: 對「整句改錯」可把使用者已輸入的片段直接當 draft，用一次 forward 驗證整段；若錯誤很少，延遲≈一次 prefill，而非 N 次 decode。
- 標籤: #decoding-acceleration #aggressive-decoding #GEC #lossless

### Lossless Acceleration for Seq2seq Generation with Aggressive Decoding（IAD / GAD）
- 出處: arXiv 2205.10350（Microsoft Research Technical Report, 2022；無同儕審查會議標註） | 連結: https://arxiv.org/abs/2205.10350
- 問題: 把 SAD 的「輸入引導平行解碼」推廣成一般 seq2seq 的 lossless 加速框架。
- 核心想法: 兩種變體：Input-guided Aggressive Decoding（IAD）在輸出≈輸入的任務（GEC、文本簡化）直接複製輸入為草稿並平行驗證；Generalized Aggressive Decoding（GAD）用額外的 non-autoregressive 模型產生草稿，再由 AR 模型驗證（即「draft-then-verify」）。
- 結果: 6 層 Transformer、GPU：IAD 在 GEC 與 Text Simplification 7×–9×，輸出與 greedy 相同；GAD 在 MT 與摘要 3×–5×，品質相同或更好。摘要中亦指出「在平行運算更強的硬體上增益可能更大」。
- 為什麼重要: 明確把「輸入即草稿」與一般 speculative decoding 統一起來，是 GEC 類任務在 LLM 時代的直接理論基礎。
- 侷限: 技術報告，與 2106.04970、2203.16487 內容重疊；實驗仍為小型 seq2seq + GPU。（評估）Apple GPU 在 batch=1 時驗證 k 個 token 的成本並非完全免費（見第三部分成本模型），加速比會比 V100 低。
- 對我們輸入法的啟示: 對「已上屏文字的後校正」應採 IAD 式：以原文為草稿，一次 forward 找出第一個不一致位置，只在分歧處做少量 AR decode。
- 標籤: #decoding-acceleration #aggressive-decoding #draft-then-verify

### Inference with Reference: Lossless Acceleration of Large Language Models（LLMA）
- 出處: arXiv 2304.04487（2023；Microsoft，MSR-TR-2023-17；未見會議） | 連結: https://arxiv.org/abs/2304.04487
- 問題: decoder-only LLM 的輸出常與上下文中的參考文字（檢索文件、對話歷史）有大段重疊，如何利用它加速？
- 核心想法: 偵測輸出已生成的尾端與參考文字匹配時，從參考文字複製一段 span 作為 decoder 輸入，在單一步內平行檢查；接受最長一致前綴。這是 Aggressive Decoding 在 decoder-only LLM 上的版本。
- 結果: 摘要稱在參考與輸出高度重疊情境（檢索增強、多輪對話）下 >2× 加速且與 greedy 結果相同（MSR 頁面與報導稱約 2×，摘要另有「up to 2–3×」說法）。
- 為什麼重要: 證明「複製 + 平行驗證」可直接套到現成 LLM，不需額外模型或訓練。
- 侷限: 加速完全取決於重疊程度；摘要未列出具體模型名稱（需看全文）。（評估）在校正任務中重疊度通常 >90%，理論上比 RAG 情境更有利。
- 對我們輸入法的啟示: 對 decoder-only 小模型（Qwen3/Gemma 等），「把目前句子貼在 prompt 裡、讓模型輸出校正版」時，可用 LLMA/PLD 方式從 prompt 中的原句複製草稿。
- 標籤: #decoding-acceleration #copy #reference-decoding

### Prompt Lookup Decoding（PLD，Apoorv Saxena 2023）
- 出處: GitHub 專案（2023-11；非論文） | 連結: https://github.com/apoorvumang/prompt-lookup-decoding
- 問題: 不要 draft model 的 speculative decoding 如何在「輸入落地」任務（摘要、QA、編輯）上加速？
- 核心想法: 用最後 n 個生成 token 在 prompt 中做字串比對（n-gram 最大 3），把 prompt 中其後的 token（如 10 個）當作候選，交給模型一次驗證。
- 結果: README：A100 40GB、Mistral-7B-Instruct-v0.1、greedy，CNN/DailyMail 與 HAGRID 各 100 例平均約 2.4×；宣稱輸入落地任務 2×–4×，不影響輸出品質；已整合進 HF transformers（prompt_lookup_num_tokens）、vLLM（"[ngram]"）、llama.cpp（examples/lookup）。
- 為什麼重要: 實作最簡單、零訓練、零額外記憶體，是校正任務最實用的第一步。
- 侷限: 作者自稱超參數幾乎沒調；sampling 未測（撰寫時）；無授權聲明。（評估）草稿長度上限固定，對 Apple GPU 的「驗證 k token 成本」需實測找最佳 k。
- 對我們輸入法的啟示: 先用 PLD（以原句為檢索來源）當 baseline，在 MLX/llama.cpp 上量「一次驗證 10–20 token」相對於「decode 1 token」的時間比，即可估算真實加速比。
- 標籤: #decoding-acceleration #n-gram-draft #training-free

### Fast Inference from Transformers via Speculative Decoding
- 出處: ICML 2023 (Oral) | 連結: https://arxiv.org/abs/2211.17192 ；https://proceedings.mlr.press/v202/leviathan23a.html ；Oral 頁面 https://icml.cc/virtual/2023/oral/25546
- 問題: 自回歸解碼一次一 token、受記憶體頻寬限制，如何在不改變輸出分佈下加速？
- 核心想法: 小的近似模型（draft）先猜 γ 個 token，大模型一次平行計算這些位置的分佈，用改良的 rejection sampling 接受或修正；可保證輸出分佈與大模型完全相同。
- 結果: T5-XXL 上相對 T5X 標準實作 2×–3×，輸出相同。
- 為什麼重要: speculative decoding 的奠基論文之一；後續 Medusa/EAGLE/PLD 都是「草稿從哪來」的變體。
- 侷限: 需要一個好的、同 tokenizer 的 draft model；加速依賴接受率。（評估）對 0.6B 以下目標模型，再找更小的 draft 模型意義不大——輸入法場景應以「原句」或「n-gram」當 draft。
- 對我們輸入法的啟示: 校正場景的天然 draft 就是使用者的原句（接受率極高），不需額外 draft 模型。
- 標籤: #speculative-decoding #lossless #foundational

### Accelerating Large Language Model Decoding with Speculative Sampling
- 出處: arXiv 2302.01318（DeepMind, 2023-02；未見會議） | 連結: https://arxiv.org/abs/2302.01318
- 問題: 同上，獨立提出（DeepMind）。
- 核心想法: draft 模型提出短續寫，目標模型平行打分；作者指出「平行打分短續寫的延遲與從大模型取樣一個 token 相近」，再以修改的 rejection sampling 保證分佈不變。
- 結果: Chinchilla 70B 分散式設定下 2×–2.5× 解碼加速，樣本品質不變。
- 為什麼重要: 給出「驗證 k 個 token ≈ 1 個 token 的成本」這個關鍵觀察（在大模型、記憶體頻寬瓶頸下成立）。
- 侷限: 70B 分散式設定；（評估）對 Apple Silicon 上 <1B 的小模型，瓶頸可能不是權重讀取而是 kernel dispatch/啟動開銷，「k token ≈ 1 token」仍大致成立但需實測。
- 對我們輸入法的啟示: 驗證整句候選的成本與生成 1 個 token 接近 → 應盡量把工作設計成「打分/驗證」而非「生成」。
- 標籤: #speculative-decoding #cost-model

### Speculative decoding 變體家族總表（Medusa / EAGLE-1/2/3 / Lookahead / REST / LayerSkip / SpecInfer）
- 出處與連結（皆已核對標題）：
  - Medusa: ICML 2024 | https://arxiv.org/abs/2401.10774 ；https://proceedings.mlr.press/v235/cai24b.html
  - EAGLE: ICML 2024 | https://mlanthology.org/icml/2024/li2024icml-eagle/ （arXiv 2401.15077）
  - EAGLE-2: EMNLP 2024 (main) | https://aclanthology.org/2024.emnlp-main.422 ；https://arxiv.org/abs/2406.16858
  - EAGLE-3: NeurIPS 2025 | https://arxiv.org/abs/2503.01840 ；https://neurips.cc/virtual/2025/poster/119930
  - Lookahead Decoding: ICML 2024 | https://arxiv.org/abs/2402.02057 ；https://proceedings.mlr.press/v235/fu24a.html
  - REST: NAACL 2024 (long) | https://aclanthology.org/2024.naacl-long.88 ；https://arxiv.org/abs/2311.08252
  - LayerSkip: ACL 2024 (long) | https://aclanthology.org/2024.acl-long.681 ；https://arxiv.org/abs/2404.16710
  - SpecInfer: ASPLOS 2024 | https://arxiv.org/abs/2305.09781

| 方法 | 草稿來源 | 需額外訓練？ | 來源宣稱加速 | 對輸入法（輸出≈輸入、<1B 模型、batch=1）的適用性（評估） |
|---|---|---|---|---|
| Medusa-1/2 | 在 LM 頂層加多個 decoding heads，tree attention 驗證 | 是（heads；Medusa-2 聯合微調） | Medusa-1 >2.2×；Medusa-2 2.3–2.8×（v1 曾寫 2.3–3.6×） | 中：小模型加 heads 成本低，但校正任務原句已是更好草稿 |
| EAGLE | 在 second-to-top 特徵層自回歸 + 前移一步的 token | 是（輕量 draft head） | LLaMA2-Chat 70B 2.7–3.5× | 低：為通用聊天設計 |
| EAGLE-2 | 同上 + 依 draft 信心動態建樹 | 是 | arXiv 3.05–4.26×（比 EAGLE-1 快 20–40%）；Anthology 版「最高 5×」 | 低–中：「依信心動態調整樹」概念可借用 |
| EAGLE-3 | 改為直接預測 token、融合多層特徵（training-time test） | 是 | 最高 6.5×、比 EAGLE-2 約 1.4×；SGLang batch 64 吞吐 1.38× | 低：0.6B 級目標模型上效益未知 |
| Lookahead | Jacobi 迭代產生 n-gram 池 + 驗證分支，免草稿模型 | 否 | MT-bench 約 1.8×；多 GPU 程式碼補全約 4× | 中：免訓練，但每步要額外算 lookahead 分支，Apple GPU 上 FLOPs 不免費 |
| REST | 從外部資料庫檢索續寫當草稿 | 否 | 7B/13B 單 batch 1.62–2.36× | 中：可用「使用者個人詞庫/歷史句」當資料庫 |
| LayerSkip | 用前幾層 early-exit 當草稿、剩餘層驗證（共享 KV/權重） | 是（layer dropout + early-exit loss 訓練） | CNN/DM 最高 2.16×、coding 1.82×、TOPv2 2.0× | 中：無額外記憶體，適合記憶體吃緊的 IME 背景程式 |
| SpecInfer | 多個小模型組成 token tree，大模型以 tree attention 一次驗證 | draft 需對齊 | 分散式 1.5–2.8×；offloading 2.6–3.5× | tree attention 是「一次 forward 打分多候選」的關鍵技術（見第三部分） |

- 為什麼重要: 這些方法本質上都在回答「草稿從哪來」；它們共同的前提是「驗證 k 個 token 的成本 ≈ 生成 1 個 token」（decode 受記憶體頻寬限制）。
- 侷限: 幾乎所有數字都是資料中心 GPU（A100/H100）、7B–70B 模型；（評估）在 M4 + <1B 模型 + batch=1 下，模型權重只有 0.3–1 GB，每 token 讀權重時間僅數 ms，固定的 kernel dispatch 開銷佔比變大，speculative 的相對收益通常會縮水；需自行量測。
- 對我們輸入法的啟示: 校正任務的最佳「草稿」是原句本身（接受率遠高於任何 draft head），所以優先 IAD/PLD/CopySpec 類；Medusa/EAGLE 只在需要「續寫/補全」功能時才值得。
- 標籤: #speculative-decoding #survey-table

### CopySpec（EMNLP 2025）與 PLD+（NAACL Findings 2025）：2025 年的「複製式」草稿
- 出處: CopySpec — EMNLP 2025 main（2025.emnlp-main.1337）| 連結: https://arxiv.org/abs/2502.08923 ；PLD+ — Findings of NAACL 2025（2025.findings-naacl.338，Adobe Research）| 連結: https://arxiv.org/abs/2412.01447
- 問題: 在輸入落地（input-guided）任務與多輪對話中，如何免訓練、免額外記憶體地從上下文複製草稿，並比 PLD 更準？
- 核心想法: CopySpec 在聊天歷史/上下文中找重複 token 序列，推測其後續並交模型驗證，可與一般 speculative decoding 疊加。PLD+ 利用推論中本就算出的 attention 與 hidden states 來挑選要複製的輸入 span（比單純 n-gram 比對更聰明），目標任務明列 code editing、text editing、summarization。
- 結果: CopySpec：CNN/DM 最高 2.35×、MT-Redundant 第二輪部分類別 3.08×、GSM8K 自我修正第三輪 2.66×；疊加在 speculative decoding 上再平均 +49%。PLD+：5 個任務中勝過所有免調參方法，greedy 下 4/5 任務勝過需訓練的 EAGLE，平均加速差距最高 2.31（摘要原文為 margin，非相對 greedy 的倍數）。
- 為什麼重要: 證明 2025 年的研究仍持續在「從輸入複製」這條線上推進，且 text editing 被明確列為目標任務。
- 侷限: 摘要頁未列模型與硬體（需讀全文）；（評估）兩者都以中大型模型 + GPU 為主，未針對 <1B 與 Apple Silicon。
- 對我們輸入法的啟示: 若用 decoder-only LLM 輸出「完整校正句」，PLD+ 的「用 attention 對齊挑 span」可在分歧後更快重新對齊原句，降低 SAD 所說的 re-decode 成本。
- 標籤: #decoding-acceleration #copy #2025

### To Diff or Not to Diff? Structure-Aware and Adaptive Output Formats for Efficient LLM-based Code Editing（AdaEdit）
- 出處: Findings of ACL 2026（arXiv 2026-04-30） | 連結: https://arxiv.org/abs/2604.27296
- 問題: 讓 LLM 輸出完整程式碼很慢；但傳統 unified diff 對 LLM 不自然（offset 脆弱、hunk 破碎），該用什麼輸出格式？
- 核心想法: 提出結構感知的 BlockDiff / FuncDiff（以語法完整區塊為單位重寫），並訓練模型自適應地在「diff」與「完整輸出」之間選擇 token 較少者（AdaEdit）。
- 結果: 在長程式碼編輯任務上，準確度與完整輸出相當，延遲與成本降低 >30%（摘要未提供更細數字與模型名稱）。
- 為什麼重要: 2026 年最直接討論「編輯輸出格式 vs 延遲」的論文；結論是「格式要對 LLM 自然，且應自適應」。
- 侷限: 領域是程式碼而非自然語言；第三方評論指出細節與消融不足（未能確認其細節）。（評估）對短句校正，完整輸出本來就短，diff 格式的收益主要來自「不改的句子只輸出一個 <KEEP> token」。
- 對我們輸入法的啟示: 對短句可讓模型先輸出 1 個 token 判斷「需不需要改」（無錯則立即結束）；需要改時才輸出「位置+替換」或整句——這是自適應格式在 IME 的簡化版。
- 標籤: #edit-format #diff #2026

### 編輯式（tagging / edit-op）模型總表：LaserTagger、Seq2Edits、FELIX、EdiT5、GECToR
- 出處與連結：
  - LaserTagger（Encode, Tag, Realize）: EMNLP 2019 | https://arxiv.org/abs/1909.01187
  - Seq2Edits: EMNLP 2020（2020.emnlp-main.418）| https://arxiv.org/abs/2009.11136 ；https://aclanthology.org/2020.emnlp-main.418
  - FELIX: Findings of EMNLP 2020（2020.findings-emnlp.111）| https://arxiv.org/abs/2003.10687
  - EdiT5: Findings of EMNLP 2022 | https://arxiv.org/abs/2205.12209
  - GECToR（Tag, Not Rewrite）: BEA Workshop 2020（ACL 同場）| https://arxiv.org/abs/2005.12592
- 問題: 校正輸出大部分抄寫輸入，逐 token 重新生成浪費；能否只預測「編輯」？
- 核心想法: 把生成改為「對每個輸入 token 標 KEEP/DELETE/插入片語」（LaserTagger、GECToR）、「預測 span 級編輯操作序列」（Seq2Edits，推論步數∝編輯數而非輸出長度）、「非自回歸 tagging + pointer 重排 + MLM 插入」（FELIX）、「非自回歸 tag/重排 + 僅插入部分自回歸」（EdiT5）。
- 結果（來源宣稱）:
  - LaserTagger：比可比 seq2seq 快「兩個數量級以上」。
  - Seq2Edits：GEC 上比完整序列模型最高 5.2×。
  - GECToR：比 Transformer seq2seq GEC 最高快 10×；CoNLL-14 F0.5 65.3（單模型）/66.5（ensemble），BEA-19 72.4/73.6。
  - EdiT5（Cloud TPU v4、batch=1、只計裝置上計算）：BEA F0.5 71.58，平均延遲 1.8 ms、P95 2.5 ms；T5-base（F0.5 72.39）平均 35.5 ms、P95 74.1 ms；EdiT5-large 4.1 ms / P95 6.6 ms（F0.5 72.93）。
- 為什麼重要: 這類模型把延遲從「與輸出長度成正比」變成「近似一次 encoder forward」，是 IME「每次按鍵都跑」最可能達到 <10 ms 的路線；EdiT5 的 P95 數字證明 tail latency 也穩定。
- 侷限: tagging 需要預先定義的轉換字典（GECToR 的 g-transformations），對中文同音字替換需要「候選字集合」設計；（評估）都是 encoder 模型，難以直接利用新一代 decoder-only 小 LLM 的知識；中文能力取決於 encoder 預訓練。
- 對我們輸入法的啟示: 混合架構的第一層：用 encoder tagger（或把 decoder-only LLM 當 tagger：每個位置輸出 KEEP/替換字）做「每鍵即時」偵錯；只有被標為可疑的片段才送 LLM 做第二階段改寫。
- 標籤: #edit-based #tagging #non-autoregressive #GEC

### Grammar Correction as You Type, on Pixel 6（Google Research Blog, 2021-10-27）
- 出處: Google Research Blog（產業案例） | 連結: https://research.google/blog/grammar-correction-as-you-type-on-pixel-6/
- 問題: 在手機鍵盤（Gboard）上邊打字邊做文法校正，完全離線。
- 核心想法: Transformer encoder + LSTM decoder 的 seq2seq 混合架構（品質/延遲平衡）；共享 embedding、factorized embedding 縮小模型；從雲端大模型做 hard distillation（數億句網頁句子）；8-bit PTQ。針對「未完成句子」：若前綴可被自動補完成合法句子即視為正確，訓練資料中這類前綴映射到自身；使用者打超過 3 個字（words）就送出請求。輸出為完整校正句，事後用 Levenshtein 對齊轉成替換建議。
- 結果: 模型儲存 20 MB；「60 個輸入字元推論 <22 ms（Pixel 6 CPU）」；8-bit 量化不實質影響品質。
- 為什麼重要: 最接近本專案的量產先例：證明「邊打字邊改」需要 (a) 前綴感知訓練、(b) 從大模型蒸餾到極小模型、(c) 量化、(d) 觸發門檻。
- 侷限: 只支援英文（上線時）；2021 年非 LLM；（評估）22 ms 是 60 字元的完整推論，無錯時輸出=輸入，仍需逐 token 解碼——若結合 aggressive decoding 可再降。
- 對我們輸入法的啟示: 一定要用「前綴（未完成句）」資料訓練，否則模型會把未打完的句子「改」掉；觸發條件（如：一個詞組/標點/停頓後）比每鍵觸發更實際。
- 標籤: #industry #on-device #distillation #prefix-aware

### One Model to Rule Them All: Our Path to Efficient On-Device Writing Assistance（Grammarly Engineering Blog）
- 出處: Grammarly 工程部落格（頁面標「Updated on April 25, 2025」） | 連結: https://www.grammarly.com/blog/engineering/efficient-on-device-writing-assistance/
- 問題: 在 Mac 上以單一本地模型做拼字＋文法校正。
- 核心想法: 約 1B 參數的 decoder-only Llama 架構（選它而非 T5 encoder-decoder），GQA；以 Apple MLX 在 M 系列 GPU 上跑、利用 unified memory 避免 CPU↔GPU 複製；權重 16-bit → 4-bit 量化；以 T5 錯誤注入模型產生合成錯誤資料。
- 結果: 目標 ≥50 tok/s，最終 M2 Mac 上約 210 tok/s；4-bit 使記憶體足跡減少 70% 而校正品質不變；未提供毫秒延遲。弱點：專有名詞、冠詞位置、單句上過早改時態。
- 為什麼重要: 直接證明「~1B LLM + MLX + 4-bit」在 Apple Silicon 上做即時校正是量產可行的（且是 2025 年的商業部署）。
- 侷限: 部落格未說明輸出格式（整句 vs 編輯）、未用 speculative decoding、未給 TTFT/prefill 數字。（評估）210 tok/s ≈ 4.8 ms/token，一句 20 token 完整重寫約 100 ms，加上 prefill；這正是 IAD/PLD 可以再壓縮之處。
- 對我們輸入法的啟示: M4 的記憶體頻寬（120 GB/s）高於 M2（100 GB/s），同級 1B 4-bit 模型可預期 decode ≥210 tok/s 的量級；但 IME 更該關注 prefill 與「無錯即提早結束」。
- 標籤: #industry #MLX #4bit #on-device


---

## 第二部分：邊打字邊推論（增量／串流推論、KV-cache 重用）

### Prompt Cache: Modular Attention Reuse for Low-Latency Inference
- 出處: MLSys 2024 | 連結: https://arxiv.org/abs/2311.04934 ；https://mlsys.org/virtual/2024/poster/2643
- 問題: 許多 prompt 共享相同片段（系統提示、模板、文件），每次都重算 prefill 浪費 TTFT。
- 核心想法: 以 schema 定義可重用的「prompt modules」，預先計算其 attention states（KV），並在重用時保持位置編碼一致；不只限於前綴，可模組化拼接。
- 結果: 摘要：TTFT 在 GPU 上 8×、CPU 上 60× 改善，不損準確度、不改模型參數（搜尋摘要指出 HTML v2 評估段寫 GPU 1.5–10×、CPU 20–70×，兩版數字不同；未開啟 v2 全文確認）。原型基於 HF transformers，測 Llama 2、Falcon、MPT。
- 為什麼重要: 證明在 CPU/低算力平台上，重用 KV 對 TTFT 的影響比 GPU 更大——與「裝置端」情境最相關。
- 侷限: 非前綴模組拼接時，模組之間的跨注意力被忽略（近似）；（評估）IME 的系統提示短，主要收益來自「固定的指令前綴 + 使用者左文」的前綴快取，而非模組拼接。
- 對我們輸入法的啟示: 把「任務指令」與「使用者已確認的左側上下文」固定在 prompt 最前面並常駐 KV；每次按鍵只 prefill 新增的幾個 token。
- 標籤: #kv-cache #prefix-reuse #TTFT

### Automatic Prefix Caching（vLLM）與 RadixAttention（SGLang, NeurIPS 2024）
- 出處: vLLM 設計文件（非論文）；SGLang: Efficient Execution of Structured Language Model Programs，NeurIPS 2024 | 連結: https://docs.vllm.ai/en/v0.9.1/design/automatic_prefix_caching.html ；https://arxiv.org/abs/2312.07104 ；https://proceedings.neurips.cc/paper_files/paper/2024/hash/724be4472168f31ba1c9ac630f15dec8-Abstract.html
- 問題: 伺服器端多請求共享前綴時如何自動重用 KV。
- 核心想法: vLLM：每個 KV block 以「前綴 token + 本 block token」做 hash，全域 hash table 對應實體 block，可共享、引用計數、按 OS cache 方式淘汰（不維護樹）。SGLang：以 radix tree 管理所有請求的 KV，LRU 淘汰，自動找最長共同前綴。
- 結果: SGLang 宣稱在 agent 控制、JSON 解碼、多輪聊天等任務最高 6.4× 吞吐量提升（RadixAttention + 壓縮 FSM 合計）。
- 為什麼重要: 「精確前綴匹配」是所有現行引擎的 KV 重用語意——遇到第一個不同 token 即停止，之後全部重算。
- 侷限: 只對精確前綴有效；中段編輯後的後綴必須重算（PatchKV 論文亦指出 SGLang 的精確前綴重用在第一個不匹配處終止）。（評估）IME 單機單使用者，不需要 radix tree 的多請求共享；但「候選句共享前綴」的樹狀結構仍有用（見第三部分）。
- 對我們輸入法的啟示: 使用者游標通常在句尾 → 99% 的按鍵是「追加」或「刪除最後幾個字」，精確前綴語意就夠；只有在使用者把游標移回句中修改時才需截斷重算。
- 標籤: #kv-cache #prefix-caching #serving

### CacheBlend: Fast Large Language Model Serving for RAG with Cached Knowledge Fusion
- 出處: EuroSys 2025 | 連結: https://arxiv.org/abs/2405.16444
- 問題: 預算好的文件 KV 只在它是前綴時可直接用；放在中段時，其 KV 未看到前面的文字，直接拼接會掉品質。
- 核心想法: 重用預算 KV，但選擇性地重算一小部分「跨注意力偏差最大」的 token 的 KV 來修補；並把重算與 KV 載入管線化。
- 結果: 相較全量重算，TTFT 降 2.2–3.3×、吞吐量升 2.8–5×，品質無明顯損失（3 個開源 LLM、4 個基準）。實作於 vLLM（LMCache）。（數字來自搜尋結果中的 arXiv HTML 摘要，未開啟全文）
- 為什麼重要: 首個在品質可接受下「非前綴 KV 重用」的系統化方法，是處理「中段編輯」的研究方向起點。
- 侷限: 針對長 RAG 文件；（評估）IME 句子短（數十到數百 token），全量重算後綴的成本本來就低，CacheBlend 類近似不值得引入誤差。
- 對我們輸入法的啟示: 對 IME 的「中段編輯」：截斷到編輯點 + 重算後綴即可；只有在保留非常長的文件級上下文時才考慮 CacheBlend/PatchKV 類修補。
- 標籤: #kv-cache #non-prefix-reuse

### PatchKV（2026）與「KV cache 是 prefill 時寫下的筆記」（2026）：中段編輯的最新證據
- 出處: PatchKV — arXiv 2609.26219（2026-08-18，cs.DC，未見會議）；Models Take Notes at Prefill — arXiv 2606.17107（2026-06-14，單一作者，未同儕審查） | 連結: https://arxiv.org/abs/2609.26219 ；https://arxiv.org/abs/2606.17107
- 問題: 上下文中段被修改、但長後綴不變時，後綴的 KV 因因果歷史與 RoPE 位置改變而不再精確，能否少算一點？
- 核心想法: PatchKV 把相鄰版本分成「精確前綴／更新區段／對齊後綴」，用離線的漂移模型預測編輯附近的「髒」區塊，加上從儲存的 attention 選出的少量遠端區塊重算，其他後綴區塊從 CPU 記憶體還原並做 RoPE 修正。後者論文指出：只覆寫被修改欄位自己的 KV 幾乎無效（該欄位自身 KV 只佔決策 <1%），因為模型在 prefill 時已把結論寫進下游位置。
- 結果: PatchKV：三模型三長上下文 QA，resume TTFT 比全量後綴重算快 2.51–3.85×、比 CacheBlend 快 1.26–2.06×，F1 在 9 個設定中 6 個持平或更好。後者：vLLM 測試 p90 TTFT 降 53–398×（agent 情境）。
- 為什麼重要: 給了一個重要的反面證據：「只更新被改的那幾個 token 的 KV」是錯的；正確性要求重算下游。
- 侷限: 皆為長上下文 agent 情境、arXiv 預印本。（評估）IME 的後綴很短，直接重算即可；這兩篇主要是提醒不要做「只 patch 被改字的 KV」這種錯誤捷徑。
- 對我們輸入法的啟示: KV 管理規則：編輯點之前保留、之後一律丟棄重算；不要嘗試局部 patch。
- 標籤: #kv-cache #mid-context-edit #2026

### KV cache 截斷／回滾：mlx-lm 與 llama.cpp 的實際行為（對 hybrid 模型極重要）
- 出處: mlx-lm 原始碼 mlx_lm/models/cache.py；llama.cpp GitHub discussion #19264（2026-02-02）與相關 issues | 連結: https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/models/cache.py ；https://github.com/ggml-org/llama.cpp/discussions/19264 ；https://github.com/ggml-org/llama.cpp/issues/21831 ；https://github.com/ggml-org/llama.cpp/issues/28425
- 什麼: 使用者按退格（刪字）時，引擎能否把 cache 回滾 n 個 token？
- 事實（原始碼）：mlx-lm 中 `KVCache`、`QuantizedKVCache`、`ChunkedKVCache`、`ConcatenateKVCache`、`BatchKVCache` 的 `is_trimmable()` 為 True，`trim(n)` 只是把 `offset` 往回移（O(1)）；`RotatingKVCache` 只有在 `offset < max_size`（尚未繞回）時可 trim；`ArraysCache`（SSM／卷積狀態用）繼承 `_BaseCache` 的 False，無 `trim()`。`can_trim_prompt_cache()` 要求所有層都可 trim，所以只要模型有任一 SSM/conv 層，整個 cache 就無法回滾。
- 事實（llama.cpp）：recurrent／hybrid 記憶體的 `seq_rm` 對部分範圍回滾會失敗（只有 speculative decoding 設定 `n_rs_seq` 時才支援單 token 回滾，見 #28425），server 會退回「清空重算」；解法是 context checkpoints（`--ctx-checkpoints`），在特定位置存下 recurrent state／SWA KV 的快照。#19264 的使用者回報 LFM2.5 每個 recurrent state checkpoint 約 0.47 MiB。多個 issue 回報 Qwen3.5/3.6 hybrid 模型每輪重算整個 prompt（狀態：截至 2026 年仍有未完全修好的回報；需看最新 upstream）。
- 對輸入法的意義（評估）: 純 attention 模型（Qwen3-0.6B/1.7B、Gemma 3 270M/1B、Llama 3.2 1B）回滾是 O(1)；hybrid conv/SSM 模型（LFM2、Granite 4.0 H、Qwen3-Next/3.5 hybrid）需要「每個 token（或每個字）存一份 state 快照」——但因 state 很小（LFM2.5 約 0.47 MiB/份），存最近 32–64 份快照只需數十 MB，完全可行；只是需要自己寫，現成引擎不幫你做。
- 標籤: #kv-cache #rollback #hybrid-ssm #mlx #llama.cpp

### Two-Pass End-to-End Speech Recognition（及 Deliberation、裝置端 RNN-T+LAS）— 架構類比
- 出處: Sainath et al., Interspeech 2019（arXiv 1908.10992）；Hu et al., Deliberation Model Based Two-Pass E2E ASR, ICASSP 2020（arXiv 2003.07962）；Sainath et al., A Streaming On-Device E2E Model Surpassing Server-Side Conventional Model Quality and Latency, ICASSP 2020（arXiv 2003.12710） | 連結: https://arxiv.org/abs/1908.10992 ；https://arxiv.org/abs/2003.07962 ；https://arxiv.org/abs/2003.12710
- 問題: 串流（即時）模型品質不夠，非串流模型品質好但延遲高——如何兼得？
- 核心想法: 第一階段 RNN-T 串流輸出即時結果（使用者馬上看到），句末時第二階段 LAS 對 top-K 假設做 rescoring（或 beam search）；兩者共用 encoder。Deliberation 第二階段同時注意聲學與第一階段假設（雙向 encoder），品質更好但計算更多，作者建議跨假設 batch 以降延遲。
- 結果: Two-pass：相對 RNN-T 單獨 WER 降 17–22%，摘要原文稱延遲「increases latency by a small fraction over RNN-T」。表 2（據搜尋摘要轉述，未開啟全文）：rescoring SU 6.2/LU 4.1，beam search 6.1/4.8，RNN-T 6.9/4.5——作者選 rescoring（長句更穩）。Deliberation：比 LAS rescoring 再降 12%（專有名詞集 23%）。ICASSP 2020 裝置端版本：利用 RNN-T 的句末決策關麥並加速 LAS rescoring，在相同延遲下 WER 相對改善 8%、模型小 400 倍以上（相對傳統伺服器模型）。
- 為什麼重要: 這是「快速第一階段即時顯示 + 重模型第二階段只在邊界處修正」的經典範式，可直接映射到 IME：注音解碼器（第一階段）即時出字，LLM（第二階段）在詞/句邊界做 rescoring。
- 侷限: 語音領域；（評估）ASR 的第二階段在使用者講完後才跑，對應 IME 的「標點/空白/停頓」時機；rescoring（打分 K 個候選）比重新生成更省、更穩。
- 對我們輸入法的啟示: 採 rescoring 而非 generation：讓注音引擎產生 top-K 候選句（或 lattice），LLM 一次 forward 打分；「句末/停頓」才觸發較重的第二階段。
- 標籤: #two-pass #rescoring #architecture-analogy #ASR

### Re-translation versus Streaming for Simultaneous Translation（IWSLT 2020）— 顯示穩定性類比
- 出處: IWSLT 2020（2020.iwslt-1.27） | 連結: https://arxiv.org/abs/2004.03643 ；Google AI Blog「Stabilizing Live Speech Translation in Google Translate」https://ai.googleblog.com/2021/01/stabilizing-live-speech-translation-in.html
- 問題: 每來一個新輸入就整段重算（re-translation）會讓螢幕上文字「閃爍改寫」，如何在延遲與穩定性間取捨？
- 核心想法: 以 erasure（被抹除重寫的 token 數）衡量不穩定；推論時用 masking（暫不顯示尾端 k 個 token，直到更多輸入到來）與 biasing（偏向沿用前一次輸出）降低閃爍；訓練時加入前綴對（prefix pairs）。
- 結果: 在低 erasure（NE < 0.2）下 re-translation 品質不低於最佳串流系統；無 erasure 時退化為 wait-k，且只要訓練含前綴對，效果遠比以往報告好。
- 為什麼重要: 給 IME 一個可量化的「不穩定性」指標與兩個現成的穩定化技巧。
- 侷限: 翻譯領域；（評估）IME 的閃爍成本比字幕更高（使用者正在注視游標處）。
- 對我們輸入法的啟示: 定義 erasure 指標：「已顯示的校正建議被撤回/改變的次數」；校正建議應延遲到詞/句邊界、並偏向維持前一次建議（biasing），避免每鍵閃爍。
- 標籤: #stability #flicker #re-translation #UI


---

## 第三部分：批次候選打分與成本模型

### Tree attention／共享前綴的多候選一次打分（SpecInfer、Medusa tree、Hydragen、ChunkAttention）
- 出處: SpecInfer — ASPLOS 2024（https://arxiv.org/abs/2305.09781）；Medusa — ICML 2024（tree attention 驗證多個 head 候選）；Hydragen: High-Throughput LLM Inference with Shared Prefixes — arXiv 2402.05099（搜尋結果顯示 ICML 2024 頁面 https://icml.cc/virtual/2024/39631，是否為主會議或 workshop 未能確認）；ChunkAttention — arXiv 2402.15220（ACL 2024 會議歸屬未能確認） | 連結: 見前述
- 問題: 一次 forward 能否同時給 K 個候選續寫（共享同一前綴）打分？
- 核心想法: 把多個候選序列排成一棵 token tree，用 tree-shaped attention mask（每個節點只看得到自己的祖先）在單一 forward 中算出所有節點的 logits；共享前綴只算一次。Hydragen 把 attention 分解為「對共享前綴」與「對各自後綴」兩部分，前綴部分可跨候選做成矩陣乘法（提高算術密度）；ChunkAttention 把 KV cache 組織成以 chunk 為單位的前綴樹，共享前綴只存一份。
- 結果: SpecInfer：分散式 1.5–2.8×、offloading 2.6–3.5×（相對既有 serving 系統）。Hydragen（搜尋摘要）：CodeLlama-13b 端到端吞吐量最高 32×；前綴從 1K 長到 16K 吞吐下降 <15%（未讀全文）。
- 為什麼重要: 這正是「注音輸入的 N-best 候選句 rescoring」所需的運算原語：候選句共享使用者左側上下文，只在最後幾個字不同。
- 侷限: 現成 MLX/llama.cpp API 主要支援 batch（多序列）而非任意 tree mask；（評估）在 MLX 中可以自己組 additive attention mask 做 tree attention（純 attention 模型可行；hybrid SSM/conv 模型因狀態是序列遞迴的，無法用 tree mask 一次處理分支——每個分支要各自從共享前綴的 state 複製一份再往下跑）。
- 對我們輸入法的啟示: 對純 attention 模型：把 K 個候選（每個 2–8 token）攤平成一個 tree，K×L 個 token 一次 forward（如 8×6=48 token，屬「小 prefill」），只需數 ms；對 hybrid 模型：改用 batch=K 並把前綴 state 廣播成 K 份。
- 標籤: #tree-attention #rescoring #batch-scoring

### 成本模型：prefill 受算力限制、decode 受頻寬限制——對 M4 上 0.3–1.7B 模型的含義
- 出處（數據來源）: llama.cpp discussion #4167（https://github.com/ggml-org/llama.cpp/discussions/4167）；Apple ML Research「Exploring LLMs with MLX and the Neural Accelerators in the M5 GPU」（2025-11-19，https://machinelearning.apple.com/research/exploring-llms-mlx-m5）；Apple Newsroom M4 Pro/Max（M4 頻寬 120 GB/s，https://www.apple.com/newsroom/2024/10/apple-introduces-m4-pro-and-m4-max/）；LLM Inference Unveiled: Survey and Roofline Model Insights（arXiv 2402.16363，roofline 分析框架）
- 已確認事實:
  - M4：120 GB/s 記憶體頻寬、最多 10 核 GPU、16 核 Neural Engine（Apple 稱最高 38 TOPS，2024-05-07 新聞稿）；MacBook Air M4 為無風扇設計；Apple 新聞稿稱「最高 10 核 GPU」（暗示有較少核心的配置；基本款 8 核 GPU 為一般認知，未於本次確認）。現行 apple.com 規格頁已是 M5 Air（153 GB/s；13 吋基本款 8 核 GPU 可升 10 核）。
  - Apple 官方文字：「首個 token 的生成（prefill）受算力限制」、「後續 token 受記憶體頻寬而非算力限制」；M5 vs M4（同為 24GB MBP）：TTFT 3.3–4.1× 加速（M5 GPU 有 Neural Accelerators，需 macOS 26.2+），但 decode 只快 19–27%（頻寬 153 vs 120 GB/s，+28%）。
  - llama.cpp #4167（Llama 2 7B、2023-11 build 8e672ef、M4 10 核 GPU）：F16 pp512 230.18 / tg128 7.43 tok/s；Q8_0 223.64 / 13.54；Q4_0 221.29 / 24.11。→ prefill 幾乎不隨量化位元改變（算力瓶頸），decode 幾乎與權重位元組數成反比（頻寬瓶頸）。
- 推導（評估，非來源宣稱）:
  - 有效頻寬：Q4_0 7B ≈ 3.8 GB × 24.1 tok/s ≈ 92 GB/s（約峰值 76%）；F16 13.5 GB × 7.43 ≈ 100 GB/s。
  - 有效 FP16 算力：2 × 6.7e9 FLOP × 230 tok/s ≈ 3.1 TFLOPS（M4 10 核 GPU，llama.cpp 2023 build；新版與 MLX 可能更高）。
  - 套到小模型（純權重估算、忽略 KV 與固定開銷）：
    | 模型（4-bit 權重） | 權重大小 | decode 頻寬上限 | 20 token prefill 算力下限 |
    |---|---|---|---|
    | Gemma 3 270M | ~0.15 GB（170M embedding 與 lm_head 共用；decode 時 lm_head 仍需整個讀取） | 400+ tok/s | <4 ms |
    | Qwen3-0.6B | ~0.35 GB（M4 Max 實測 artifact 335 MB） | ~260 tok/s | ~8 ms |
    | Llama 3.2 1B | ~0.7 GB | ~130 tok/s（oMLX 實測 M4 約 96–104 tok/s @1k ctx） | ~15 ms |
    | Qwen3-1.7B | ~1.0 GB | ~90 tok/s | ~22 ms |
  - 關鍵含義：對 <1B 模型，單 token decode 的頻寬時間只有 3–8 ms，此時「每次 forward 的固定開銷」（Metal command buffer 提交、kernel 數 × dispatch、Python/Swift 端 graph 建構；MLX 文件明言「每次 graph evaluation 都有固定開銷」）會成為不可忽略的地板；而驗證 k 個 token（k≤32）幾乎不增加讀權重成本，只增加少量算力 → 「一次 forward 驗證/打分 16–64 個 token」的成本 ≈ 1–2 次 decode。這是 IME 採用「打分/驗證」而非「生成」的物理依據。
  - prefill 長上下文時注意力成本 O(n²) 會出現：oMLX 實測 Llama 3.2 1B 4-bit 在 M4 上 PP 1k ctx 約 1,135–1,337 tok/s → 1k token 完整 prefill 需 767–903 ms（TTFT），遠超互動預算 → 左側上下文必須常駐 KV、不可每鍵重算。
- 侷限: 2023 build 的 llama.cpp 數字偏舊（頁面註明新 build 更快）；Apple 的 M4/M5 比較只給倍率，絕對值在圖中未能取得；小模型實測需以使用者自己的 micro-benchmark 為準。
- 對我們輸入法的啟示: 設計原則：(1) 左文 KV 常駐、每鍵只增量 prefill 1–3 token；(2) 把校正做成「一次 forward 打分 K 個候選」；(3) 避免每鍵觸發長 decode（>10 token）；(4) 若未來換 M5，prefill 類工作（打分）受益 3–4×，decode 類只 +25%——更支持「打分」路線。
- 標籤: #cost-model #roofline #M4 #memory-bandwidth

---

## 第四部分：小模型壓縮（量化、QAT、蒸餾）

### GPTQ（ICLR 2023）與 AWQ（MLSys 2024 Best Paper）：weight-only PTQ 的兩大基線
- 出處: GPTQ: Accurate Post-Training Quantization for Generative Pre-trained Transformers — ICLR 2023 | 連結: https://arxiv.org/abs/2210.17323 ；AWQ: Activation-aware Weight Quantization for LLM Compression and Acceleration — MLSys 2024 Best Paper Award | 連結: https://arxiv.org/abs/2306.00978 ；https://hanlab.mit.edu/projects/awq
- 問題: 不重新訓練，如何把 LLM 權重壓到 3–4 bit 而幾乎不掉品質？
- 核心想法: GPTQ 用近似二階資訊逐層、一次性（one-shot）量化，使每層輸出誤差最小；AWQ 觀察到只要保護約 1% 的「顯著權重」就能大幅降低誤差，而顯著性應依 activation 分佈（而非權重大小）判斷，並以 per-channel scaling 實作（不需混合精度）。
- 結果: GPTQ 摘要：3–4 bit 幾乎無精度損失，2-bit/三值也有可用精度。AWQ：保護 1% 顯著權重即可大幅降低量化誤差（具體數字見全文）。
- 為什麼重要: 兩者都已內建於 mlx-lm（`mlx_lm.gptq`、`mlx_lm.awq`）與多數推論框架，是小模型 4-bit 部署的標準工具。
- 侷限: 皆以 7B+ 模型為主要驗證對象；SLMQuant（arXiv 2511.13023，2025-11）指出 SLM 與 LLM 的量化敏感度不同，「直接套用為 LLM 最佳化的技巧會得到次佳結果」。（評估）0.3–0.6B 模型 4-bit PTQ 的退化通常比 7B 明顯，且 embedding/lm_head 佔參數比例高（Gemma 3 270M 有 170M 在 embedding），lm_head 宜保留 8-bit 以上。
- 對我們輸入法的啟示: 對 <1B 模型，decode 本來就快，4-bit 帶來的延遲收益有限（權重已小），應優先 8-bit 或「4-bit 主體 + 8-bit embedding/lm_head」，以品質換取極小的延遲成本；以 `mlx_lm.dynamic_quant` 找敏感層。
- 標籤: #quantization #PTQ #GPTQ #AWQ

### 小模型的 QAT：Gemma 3 QAT、Meta 量化 Llama 3.2、Apple AFM 2-bit QAT、ParetoQ（NeurIPS 2025）
- 出處與連結:
  - Gemma 3 QAT（Google Developers Blog）| https://developers.googleblog.com/en/gemma-3-quantized-aware-trained-state-of-the-art-ai-to-consumer-gpus/
  - Meta 量化 Llama 3.2 1B/3B（QAT + LoRA adaptors、SpinQuant；2024-10-24）| https://ai.meta.com/blog/meta-llama-quantized-lightweight-models/
  - Apple Intelligence Foundation Language Models Tech Report 2025 | https://arxiv.org/abs/2507.13575
  - ParetoQ: Improving Scaling Laws in Extremely Low-bit LLM Quantization — NeurIPS 2025 | https://arxiv.org/abs/2502.02631
- 問題: 小模型對 PTQ 敏感，訓練時就把量化納入能否補回？
- 核心想法與結果:
  - Gemma 3 QAT：約 5,000 步、以未量化 checkpoint 的機率為目標做 QAT；量化到 Q4_0 時「perplexity 下降幅度減少 54%」（llama.cpp perplexity 評估）。Gemma 3 270M 也提供 QAT INT4 checkpoint（Pixel 9 Pro 上 25 段對話耗電 0.75%）。
  - Meta：1B/3B instruct 量化版 2–4× 加速、模型大小平均 −56%、記憶體 −41%（相對 BF16，OnePlus 12 Android CPU，ExecuTorch）。
  - Apple AFM 2025：~3B 裝置端模型以 2-bit QAT 壓縮權重、embedding 4-bit、KV cache 8-bit；並以 LoRA adapters 回補品質。2024 版則為 2/4-bit 混合 palettization 平均 3.7 bpw。
  - ParetoQ：在 2 與 3 bit 之間有「學習轉折」——≥3 bit 的模型接近原分佈、≤2 bit 表徵大幅改變；三值/2/3-bit 在大小-精度權衡上普遍優於 4-bit；其三值 600M 模型勝過先前三值 3B SoTA。
- 為什麼重要: 證明小模型經 QAT 可在 2–4 bit 維持品質，且 Apple 自家量產模型就是 2-bit QAT。
- 侷限: QAT 需要訓練資源與原始資料分佈；（評估）IME 用的是「任務微調後的模型」，最實際的做法是在任務 SFT 的最後階段加入 QAT（或 mlx-lm DWQ 這類「以全精度模型為老師的量化蒸餾」）。
- 對我們輸入法的啟示: 若選 1–1.7B 模型才需要壓到 4-bit；用 DWQ/QAT 在自己的校正資料上做，而不是用通用 PTQ。
- 標籤: #QAT #low-bit #small-models

### MLX 量化與 llama.cpp K-quants（實作層）
- 出處: MLX 文件 mlx.core.quantize | 連結: https://ml-explore.github.io/mlx/build/html/python/_autosummary/mlx.core.quantize.html ；mlx-lm LEARNED_QUANTS.md https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LEARNED_QUANTS.md ；llama.cpp K-quants（PR #1684，作者 ikawrakow；PR 頁面本身未能直接開啟，資訊來自 commit 標題「ggml : add SOTA 2,3,4,5,6 bit k-quantizations (#1684)」與 quantize README 鏡像）
- 什麼: MLX affine 量化支援 1/2/3/4/5/6/8 bit、group size 32/64/128（預設 4-bit、g64），亦支援 mxfp4（g32）、mxfp8（g32）、nvfp4（g16）；mlx-lm 支援 DWQ（以全精度模型為老師微調量化參數）、AWQ、GPTQ、dynamic quant（敏感層 5-bit、其餘 4-bit，可設 target bpw）；文件建議 DWQ 用於 2–4 bit、較小 group size（32）效果常更好。llama.cpp K-quants：Q4_K 以 super-block 結構約 4.5 bpw；Q4_K_M 為 4/6-bit 混合，Llama 2 7B 上約 4.83–4.84 bpw（鏡像 README 數字）。
- 數字: 見上；量化對延遲的影響見第三部分（decode ∝ 權重位元組）。
- 對輸入法的意義（評估）: MLX 4-bit g64 的實際 bpw ≈ 4.5（含 scale/bias）；8-bit ≈ 8.5。對 0.6B 模型 8-bit ≈ 0.64 GB、4-bit ≈ 0.34 GB——兩者在 M4 上的 decode 差距約 2×，但都在 5 ms/token 以內。
- 標籤: #MLX #llama.cpp #quantization-formats

---

## 第五部分：Apple Silicon 執行環境與實測數字

### llama.cpp「Performance of llama.cpp on Apple Silicon M-series」（discussion #4167）
- 連結: https://github.com/ggml-org/llama.cpp/discussions/4167
- 什麼: 社群標準基準：`llama-bench -p 512 -n 128 -ngl 99`（Metal），Llama 2 7B 的 F16/Q8_0/Q4_0；比較數字固定用 2023-11-21 commit 8e672ef 以保公平（頁面 2026-08-25 更新加入 M5 指引，註明新 build 絕對值已提升）。
- 數字（M4 系列）: M4 10 核 GPU、120 GB/s：F16 230.18/7.43、Q8_0 223.64/13.54、Q4_0 221.29/24.11（pp512/tg128 tok/s）；M4 8 核 GPU：無資料。M4 Pro 20 核：Q4_0 439.78/50.74。對照 M2 10 核（100 GB/s）Q4_0 179.57/21.91。
- 限制: 7B 模型、舊 build；無 0.5–2B 小模型數字（對小模型而言固定開銷比例更高，不能直接線性外推）。
- 標籤: #llama.cpp #benchmark #M4

### Apple ML Research：Exploring LLMs with MLX and the Neural Accelerators in the M5 GPU（2025-11-19）
- 連結: https://machinelearning.apple.com/research/exploring-llms-mlx-m5 （本次以 Apple 鏡像網域讀取內容；9to5Mac 報導 https://9to5mac.com/2025/11/20/apple-shows-how-much-faster-the-m5-runs-local-llms-compared-to-the-m4/ 交叉確認）
- 什麼: Apple 官方以 `mlx_lm.generate`、4096 token prompt + 128 token 生成，比較 24GB M5 MBP 與 M4 MBP。
- 數字: Qwen3-1.7B-MLX-bf16：TTFT 3.57×、生成 1.27×（記憶體 4.40 GB）；Qwen3-8B 4-bit：3.97×/1.24×；Qwen3-14B 4-bit：4.06×/1.19×；gpt-oss-20b：3.33×/1.24×；Qwen3-30B-A3B 4-bit：3.52×/1.25×。頻寬 M4 120 GB/s、M5 153 GB/s。Neural Accelerators 需 macOS 26.2+。絕對 TTFT/tok/s 僅在圖中，未能取得文字數值。
- 對輸入法的意義: 官方背書「prefill 算力瓶頸 / decode 頻寬瓶頸」；M4 沒有 GPU Neural Accelerators，prefill 類工作（打分）在 M4 上相對昂貴，更需要 KV 重用。
- 標籤: #MLX #Apple-official #M4-vs-M5

### 社群跨執行環境基準（apple-silicon-llm-bench，2026）與 oMLX 社群數字（M4 10 核）
- 連結: https://github.com/john-rocky/apple-silicon-llm-bench ；oMLX 社群：https://omlx.ai/benchmarks/n8bzsl21 、https://omlx.ai/benchmarks/rar2w39b 、https://omlx.ai/benchmarks/k566m707
- 什麼: 同一模型、同一 harness 比較 Apple Core AI、MLX/mlx-swift、llama.cpp、LiteRT-LM、Core ML（裝置：M4 Max、iPhone 17 Pro；無 M4 基本款）。oMLX 為 M4（10c）社群上傳的 MLX 數字。
- 數字（來源宣稱，社群量測）:
  - M4 Max、Qwen3-0.6B 4-bit：MLX decode 555.9 tok/s（short-chat，warm）；Apple llm-benchmark 協定（512 prompt/1024 gen）Core AI 503.1、MLX 432.3；官方配方矩陣 Core AI 484（prefill 9,396）vs MLX 432（prefill 9,366），artifact 335 MB。
  - M4 Max、TTFT（short-chat）：Qwen2.5-0.5B llama.cpp Q4_K_M 22 ms／mlx-swift Q4 21 ms／coreml-llm(ANE, FP16) 171 ms；Qwen3.5-0.8B 22/36/405 ms；Qwen3.5-2B 29/42/665 ms。decode：Qwen2.5-0.5B mlx-swift 531.1、llama.cpp 297.1、ANE 181.2 tok/s。
  - M4 Max hybrid Mamba-2：Granite-4.0-H 350M MLX 521.7 / llama.cpp 282.5 tok/s；Granite-4.0-H 1B 275.8/141.9。
  - iPhone 17 Pro、Qwen3-0.6B：Core AI GPU INT4 193.3（cold）、MLX Q4 178.8（warm）、Core AI ANE 4-bit 116.9、LiteRT-LM 122.1、Core ML INT8 37.7 tok/s。
  - oMLX（M4 10c，Llama-3.2-1B-Instruct 4-bit，macOS 26.2，2026-03）：PP 1,135–1,337 tok/s、TG 95.8–104.5 tok/s @1k ctx；TTFT（1k ctx）767–903 ms；4k ctx TG 降至 70.5–75.5。
- 侷限: 社群數字、M4 Max（最高 546 GB/s、32 或 40 核 GPU，repo 未註明配置）≠ M4 Air；repo 自述跨 session 比值無效。（評估）M4 Max 上 0.5B 模型 TTFT 約 21–22 ms，顯示「短 prompt 的固定開銷 + prefill」在高階機約 20 ms 量級；在 M4 基本款 GPU 核數少 4×，短 prompt TTFT 預期落在數十 ms——需使用者自測。ANE 路徑（coreml-llm）TTFT 171–665 ms，對每鍵互動不適用。
- 標籤: #benchmark #MLX #llama.cpp #CoreAI #ANE

### Core ML／Apple Neural Engine 路線：Deploying Transformers on the ANE（2022）、On Device Llama 3.1 with Core ML（2024）、ANEMLL
- 連結: https://machinelearning.apple.com/research/neural-engine-transformers （2022-06-06）；https://machinelearning.apple.com/research/core-ml-on-device-llama （2024-11-01）；https://github.com/Anemll/Anemll
- 什麼與數字:
  - ANE Transformers（2022）：(B,C,1,S) 資料格式、Linear→Conv2d、chunk 化注意力、避免 reshape/transpose；DistilBERT 在 iPhone 13 最高 10× 加速、峰值記憶體 14× 降低；seq 128、batch 1：ANE 平均 3.47 ms @0.454 W 與 9.44 ms @0.072 W。文章也指出短序列時讀參數主導延遲（頻寬瓶頸）。
  - Core ML Llama 3.1 8B（M1 Max、GPU）：stateful KV cache（MLState）使 extend 從 1.25 → 16.26 tok/s；再加 Int4 block 量化（block 32）→ 33.67 tok/s、prompt TTFT 51.91 ms（context 2048）；模型 16 GB → 4.2 GB。文章明言 block-wise 量化是為 GPU 最佳化，palettization 才有利於 ANE。
  - ANEMLL（v0.3.5 beta）：支援 Llama 3.x、Qwen 3（0.6B/1.7B/8B）、Qwen 2.5、Gemma 3（270M/1B/4B QAT）；context 512–4096（建議 512–1024）；已知限制：ANE 無 block 量化故 LUT4 品質「相當低」、FP16 範圍溢位（Gemma 3 殘差需縮放）、首次載入慢。
- 對輸入法的意義（評估）: ANE 的優勢是極低功耗（72 mW 級 DistilBERT 推論），劣勢是固定形狀、首載慢、LLM decode/TTFT 慢（社群 TTFT 171–665 ms）。適合放「常駐、低功耗的小 encoder tagger（第一階段偵錯）」，不適合每鍵 LLM 生成。stateful KV（MLState）是 Core ML 跑 LLM 的必要條件。
- 標籤: #CoreML #ANE #stateful-kv #power

### Apple Core AI（WWDC 2026，macOS/iOS 27）與 Foundation Models 框架 2026 更新
- 連結: Core AI 文件 https://developer.apple.com/documentation/coreai ；產品頁 https://developer.apple.com/core-ai/ ；InfoQ 報導 https://www.infoq.com/news/2026/06/apple-core-ai-wwdc/ ；Foundation Models 文件 https://developer.apple.com/documentation/foundationmodels ；「Running a Core AI model in a Foundation Models session」https://developer.apple.com/documentation/foundationmodels/running-a-core-ai-model-in-a-foundation-models-session ；「Optimizing key-value caching in language model sessions」https://developer.apple.com/documentation/foundationmodels/optimizing-key-value-caching-in-language-model-sessions
- 已確認事實:
  - Core AI：可用版本 iOS/iPadOS/macOS/visionOS/tvOS/watchOS 27.0+；跨 CPU/GPU/Neural Engine；Swift API 可控制 specialization、caching；`.aimodel` 格式由 Core AI PyTorch Extensions（coreai-torch）轉換、Core AI Optimization 做量化/palettization；支援自訂 Metal 4 kernels；AOT 編譯（`coreai-build`）避免首次載入慢；產品頁提到「stateful execution」；Apple 建議非神經網路模型仍用 Core ML。
  - Foundation Models（2026）：新增「Custom language model provider」——`LanguageModel` protocol、`LanguageModelExecutor`；可用 `coreai-models` 套件的 `CoreAILanguageModel` 把匯出的開源模型（文件建議先選約 0.6B 參數的模型）放進同一個 `LanguageModelSession`（需 macOS 27/Xcode 27）；Core AI 會依匯出方式自動選 GPU/CPU/ANE。
  - KV caching 文件：快取值依賴所有前序 token，「任何位置的 token 改變，系統會從該點往後重算」；附加內容是 cache-friendly；`prewarm(promptPrefix:)` 可預先計算前綴（建議在使用前 1–2 秒呼叫）；可用 Foundation Models instrument 算 cache 命中率。
  - `GenerationOptions` 只有 sampling mode、temperature、maximumResponseTokens、toolCallingMode——文件中未見 log-prob／token 機率／打分 API。
  - `SystemLanguageModel` 有 3 個模型版本（26.0–26.3、26.4、27.0）；提供 `contextSize`、`supportedLanguages`、`tokenCount(for:)`；2026 文件的主題清單中未見 2025 年的 `SystemLanguageModel.Adapter`（自訂 adapter）條目（其現況未能確認；2025 tech report 稱框架提供 LoRA adapter 微調）。
  - 語言：Apple 支援頁（iOS/macOS 27）列出 Chinese (traditional)；但 Apple Intelligence 在中國大陸購買的裝置／大陸帳號不可用。
- 對輸入法的意義（評估）:
  - 系統模型（AFM ~3B）無 logprob、不能做候選打分、生成延遲以「秒」計，且版本會隨 OS 更新改變行為 → 不適合每鍵校正；最多用於「使用者主動觸發的整段改寫」。
  - Core AI 是 2026 年在 Apple 平台上跑自訂小模型的官方路線，可能在 TTFT/decode 上與 MLX 相當或略優（社群 M4 Max：Qwen3-0.6B decode Core AI 484–503 vs MLX 432 tok/s），但若透過 FoundationModels session 介面就拿不到 logits；要做打分需直接用 Core AI 的 `InferenceFunction` 自己輸出 logits（需驗證）。
  - 輸入法（InputMethodKit 程序）能否使用 FoundationModels／Apple Intelligence：文件未提及，未能確認。
- 標籤: #CoreAI #FoundationModels #Apple-2026 #kv-cache

### Apple Intelligence Foundation Language Models Tech Report 2025 與 AFM 3（2026）
- 出處: arXiv 2507.13575（v1 2025-07-17，v3 2025-08-27）；Apple ML Research「Introducing the Third Generation of Apple's Foundation Models」（2026-06-08，2026-09-09 更新）；Apple ML Research「Introducing Apple's On-Device and Server Foundation Models」（2024-06-10） | 連結: https://arxiv.org/abs/2507.13575 ；https://machinelearning.apple.com/research/introducing-third-generation-of-apple-foundation-models ；https://machinelearning.apple.com/research/introducing-apple-foundation-models
- 問題: 在 Apple Silicon 上量產一個 ~3B 裝置端 LLM，如何壓延遲與記憶體？
- 核心想法: 2025：模型切成兩個 block（5:3，Block 1 佔 62.5% 層、Block 2 佔 37.5%），Block 2 重用 Block 1 的 KV cache → KV 記憶體 −37.5%、TTFT 約 −37.5%；2-bit QAT 權重、4-bit embedding、8-bit KV cache；詞表 100k→150k；16 種語言；框架提供 guided generation、tool calling、LoRA adapter 微調（toolkit 為 rank 32）。2024 版：iPhone 15 Pro 上 TTFT 約 0.6 ms／prompt token、生成 30 tok/s（未使用 token speculation），3.7 bpw palettization、49K 詞表、rank-16 adapter 數十 MB。2026 AFM 3：AFM 3 Core（3B dense）＋ AFM 3 Core Advanced（20B sparse，每次啟用 1–4B，權重放 flash、按需載入 experts），與 Google 合作；官方頁面未提供裝置端延遲數字、未提供量化位元數，並稱技術報告稍後發布（截至本筆記未見 2026 tech report）。
- 結果: 見上（2024：0.6 ms/token TTFT、30 tok/s；2025：KV/TTFT −37.5%）。
- 為什麼重要: 「跨層共享 KV」與「低位元 KV cache」是降低 TTFT 與記憶體的量產級技巧；0.6 ms/prompt token 給了 3B 級模型在 Apple 晶片上的 prefill 參考點（100 token ≈ 60 ms）。
- 侷限: Apple 未公開 Mac 上的數字；系統模型對開發者無 logprob；（評估）3B 模型對每鍵校正太大。
- 對我們輸入法的啟示: 自訓小模型可借用 KV-sharing（減少 prefill 成本）與 8-bit KV cache；以 0.6 ms/token（3B、iPhone 15 Pro）為參考，0.6B 模型在 M4 GPU 上增量 prefill 1–3 token 應遠低於 10 ms（需實測）。
- 標籤: #Apple #AFM #kv-sharing #2bit-QAT

### 功耗／散熱（無風扇 MacBook Air）——證據缺口
- 已知: Apple 新聞稿確認 M4 MacBook Air 為無風扇設計（https://www.apple.com/newsroom/2025/03/apple-introduces-the-new-macbook-air-with-the-m4-chip-and-a-sky-blue-color/）；ANE 可在 72 mW 級功耗跑 DistilBERT（iPhone 13，見上）；Gemma 3 270M INT4 在 Pixel 9 Pro 25 段對話耗電 0.75%（Google 內部測試）；apple-silicon-llm-bench 只計 thermal-nominal 的回合，表示熱降頻確實影響量測。
- 未能確認: 未找到 M4 MacBook Air 在持續 LLM 推論下降頻曲線、GPU 功耗（W）與 tok/J 的公開量測（本次搜尋額度用盡）。
- （評估）IME 工作負載是「突發、短、低占空比」：每鍵數 ms GPU 計算，平均功耗遠低於持續生成；風險在於使用者連續打字數十分鐘時 GPU 不斷被喚醒。建議使用者自測：powermetrics 記錄 GPU/ANE 功率、連續輸入 30 分鐘的延遲 P95 漂移。
- 標籤: #power #thermal #gap


---

## 第六部分：人類對延遲的感知門檻

### 延遲感知文獻群（Miller 1968 → Card et al. → Ng 2012 → Jota 2013 → Ng 2014 → Deber 2015 → Forch 2017）
- 出處與連結（逐項標示確認程度）:
  - Miller, R. B. (1968). Response time in man-computer conversational transactions. AFIPS Fall Joint Computer Conference, Vol. 33 —— 書目經 NN/g 文章確認；原文未開啟（未能確認其內具體數值）。
  - Nielsen, J. (1993). Response Times: The 3 Important Limits（Usability Engineering 節錄）| https://www.nngroup.com/articles/response-times-3-important-limits/ —— 0.1 s「感覺即時、直接操作資料」；1 s「思路不中斷，但已失去直接操作感」；10 s「注意力極限」。來源標註為 Miller 1968 與 Card, Robertson & Mackinlay 1991。
  - Card, Moran & Newell (1983). The Psychology of Human-Computer Interaction —— Model Human Processor 的知覺處理器週期常被引用為約 100 ms（50–200 ms）；本次未能線上確認原文（未能確認）。
  - Ng, Lepinski, Wigdor, Sanders, Dietz. Designing for Low-Latency Direct-Touch Input. UIST 2012, pp. 453–464 —— 據 Ng et al. CHI 2014 原文轉述：在 1 ms 基線的原型上，使用者無法察覺低於約 6 ms 的延遲（範圍 2–11 ms，直接觸控拖曳）。
  - Jota, Ng, Dietz, Wigdor. How Fast Is Fast Enough? A Study of the Effects of Latency in Direct-Touch Pointing Tasks. CHI 2013, pp. 2291–2300（DOI 10.1145/2470654.2481317）—— 摘要：商用裝置延遲可達 125 ms，拖曳任務的表現隨延遲增加而下降；據 Ng et al. 2014 轉述，其感知 JND 約 64 ms（tapping 類任務）。
  - Ng, Annett, Dietz, Gupta, Bischof. In the Blink of an Eye: Investigating Latency Perception during Stylus Interaction. CHI 2014（DOI 10.1145/2556288.2557037）| 全文 PDF：https://webdocs.cs.ualberta.ca/~wfb/publications/C-2014-SIGCHI-Latency.pdf —— 已讀原文：商用觸控/手寫端到端延遲 65–120 ms；拖曳時可分辨約 1 vs 2 ms，塗寫（scribbling）時約 7 vs 40 ms。
  - Deber, Jota, Forlines, Wigdor. How Much Faster is Fast Enough? User Perception of Latency & Latency Improvements in Direct and Indirect Touch. CHI 2015（Honorable Mention；DOI 10.1145/2702123.2702300）—— JND 數字據 TriboTouch 論文轉引（原文未能開啟）：直接拖曳 11 ms；間接拖曳 55 ms、直接點擊 69 ms、間接點擊 96 ms。另一實驗研究「使用者能否察覺延遲的部分改善」。
  - Forch, Franke, Rauh, Krems. Are 100 ms Fast Enough? Characterizing Latency Perception Thresholds in Mouse-Based Interaction. EPCE 2017（LNCS 10276, pp. 45–56）| https://research.uni-luebeck.de/en/publications/are-100-ms-fast-enough-characterizing-latency-perception-threshol/ —— 簡單滑鼠互動的感知門檻約 60 ms。
  - Dan Luu, Keyboard latency | https://danluu.com/keyboard-latency/ —— 「按鍵開始移動 → USB 封包」15 ms（Apple Magic Keyboard，USB）到 60 ms（Logitech K360）；Computer latency: 1977–2017 | https://danluu.com/input-lag/ —— 「按鍵 → 終端機字元出現在螢幕」：Apple IIe 30 ms、2014 MacBook Pro 100 ms、現代機器多在 70–200 ms；更新率 24→165 Hz 可省約 90 ms。
- 問題: 每多一個處理步驟，使用者何時會察覺？
- 核心結論: (1) 直接操作（手指/筆拖曳）可察覺到個位數 ms；(2) 間接輸入（鍵盤、滑鼠、觸控板）門檻高得多：約 55–96 ms（Deber，轉引）、約 60 ms（Forch）；(3) 現代電腦的「按鍵→畫面」基線已有 30–100+ ms，IME 增加的延遲是疊加在這個基線上；(4) 0.1 s 是「即時感」的經典上限，1 s 是「不打斷思路」上限。
- 侷限: 觸控/筆研究為直接操作，與鍵盤打字不同；Deber 數字為轉引；本次未找到「打字時視覺回饋延遲對打字表現影響」的專門研究（搜尋額度用盡，未能確認）——此為報告中需補的缺口。
- 對我們輸入法的啟示: IME 自身增加的回顯延遲應壓在「一個 60 Hz 畫格（16.7 ms）」以內且遠低於間接輸入 JND（~55 ms）；LLM 只能在非同步路徑上，不能擋住回顯。
- 標籤: #HCI #latency-perception #JND #budget

---

## 延遲預算建議

> 依據：Nielsen/Miller 0.1 s 即時上限；間接輸入 JND ≈ 55–96 ms（Deber 2015，轉引）與滑鼠 ≈ 60 ms（Forch 2017）；系統基線（鍵盤 15–60 ms + OS/顯示 → 總計數十至 100 ms，Dan Luu）；MacBook Air 為 60 Hz 顯示（一格 16.7 ms，評估）。打字節奏（評估，以 WPM 定義 5 字元/字換算）：40 WPM ≈ 300 ms/鍵、100 WPM ≈ 120 ms/鍵——每鍵工作若超過下一鍵到來時間就會排隊。

| 互動事件 | 使用者看到什麼 | 建議預算（IME 自身增加，P95） | 理由 | 可放入的 LLM 工作 |
|---|---|---|---|---|
| A. 按鍵回顯（注音符號/preedit 更新） | 注音或英文字母立即出現在組字區 | ≤ 5 ms（硬上限 16 ms＝不超過 1 畫格） | 疊加在 30–100 ms 系統基線上；間接輸入 JND ≈ 55 ms，餘裕必須留給系統 | 無。LLM 絕不在此路徑；只做 KV 增量 prefill 的「排程」（非同步） |
| B. 候選字/詞列表更新（注音選字） | 候選窗內容與排序 | ≤ 30 ms（含 LLM 重排；目標與回顯同一或下一畫格） | 落在 1–2 畫格內，低於 JND；使用者正在看候選窗 | 一次 forward 對 top-K 候選（K≤8–16、每個 ≤4 token）做 tree/batch 打分；超時即用傳統引擎排序先顯示、之後不再重排（避免閃爍） |
| C. 自動校正「套用」於 commit 時（空白/標點/Enter/選字確認） | 剛確認的字被替換 | ≤ 50 ms（從 commit 鍵到替換完成） | 超過 ~55–100 ms 使用者已把注意力移到下一個字，替換會被感知為「文字自己跳動」 | 只允許「打分/驗證」：以原句為 draft 一次 forward；不得在此做長 decode |
| D. 校正「建議」（底線/浮動提示，不自動套用） | 錯字底線、建議氣泡 | 100–300 ms（在停頓或詞/句邊界後） | 非直接回應按鍵，可在 0.1–1 s 區間；但須穩定（低 erasure） | 可做短 decode（≤10–20 token，利用 PLD/IAD）或整句 rescoring |
| E. 整句/整段改寫（使用者主動觸發） | 改寫結果 | ≤ 1 s 出第一個結果 | Nielsen 1 s 思路不中斷 | 完整生成（可用較大模型或 Apple 系統模型） |
| 背景：左文 KV 維護 | 不可見 | 每鍵 ≤ 5–10 ms GPU 時間；空閒時批次補算 | 不可與 A/B 爭搶 GPU（需佇列與取消） | 增量 prefill、截斷回滾、state 快照 |

- 取消（cancellation）規則：新按鍵到來時，任何尚未完成且結果將過時的 B/C/D 工作立即取消；只保留「最新一次」的請求（debounce 對 D 類可設 150–300 ms）。
- 穩定性規則（借自 re-translation 文獻）：同一位置的建議在使用者未再編輯該處前不得改變超過 1 次；以 erasure 次數作為品質指標。
- 量測建議：以「按鍵事件時間戳 → IME 呼叫 setMarkedText/候選窗更新完成」量 IME 自身延遲，並以高速攝影機（Dan Luu 方法）量端到端；報告 P50/P95/P99 而非平均。

---

## 第七部分：最新小型開源模型（截至 2026-10 可確認者）

> 架構對「退格回滾」的影響：純 attention（含 GQA、滑動視窗）→ KV 可 O(1) 截斷；hybrid（conv/SSM/linear attention）→ 遞迴狀態無法部分回滾，需自行存 per-token 狀態快照（mlx-lm 的 ArraysCache 不可 trim；llama.cpp 需 context checkpoints）。

| 模型 | 釋出（可確認者） | 參數 | 架構 | 回滾難度 | 中文 | 授權 | MLX / GGUF | 備註 |
|---|---|---|---|---|---|---|---|---|
| Qwen3-0.6B | 2025-04（Qwen3 tech report arXiv 2505.09388） | 0.6B（非 embedding 0.44B） | 純 attention，28 層，GQA 16Q/8KV，32K ctx | 低（trim offset） | 強（官方 100+ 語言） | Apache 2.0 | mlx-community 4-bit 有；GGUF 有 | thinking/非 thinking 雙模式；Apple M5 文章與 Core AI 文件都以 0.6B/1.7B 為範例；M4 Max MLX 432–556 tok/s |
| Qwen3-1.7B | 2025-04 | 1.7B | 純 attention | 低 | 強 | Apache 2.0 | 有/有 | Apple 官方 MLX 基準使用（bf16 4.40 GB） |
| Qwen3.5-0.8B / 2B | 2026-02/03（卡片引用 2026-02；次級來源稱 2B 為 2026-03） | 0.8B / 2B | **hybrid**：0.8B 為 24 層＝6×(3×Gated DeltaNet + 1×Gated Attention)，262K ctx，多模態 | 高（DeltaNet 遞迴狀態；llama.cpp 有 Qwen3.5 每輪全量重算的 issue） | 強（201 語言） | Apache 2.0 | GGUF（unsloth）有；官方卡未提 MLX（社群版未能確認） | 卡片警告 0.8B 較易陷入思考迴圈；M4 Max llama.cpp TTFT 22 ms / mlx-swift 36 ms |
| Gemma 3 270M | 2025-08-14 | 270M（170M embedding + 100M transformer） | 純 attention（Gemma 3 local/global 交錯，細節未於本次確認），256K 詞表 | 低（短上下文時） | 有（大詞表；繁中品質需自評） | Gemma 條款（未於本次確認） | MLX、llama.cpp 皆官方列出 | 官方定位「為特定任務微調」；有 QAT INT4；Pixel 9 Pro 25 段對話 0.75% 電量 |
| Gemma 3 1B / Gemma 3n | 2025（未於本次確認日期） | 1B / E2B、E4B | 純 attention（3n 有 per-layer embedding） | 低 | 有 | Gemma 條款（未能確認） | 有/有 | 未於本次詳查 |
| Gemma 4 E2B | 2026（tech report arXiv 2607.02770；次級來源稱 2026-04-02 發布） | 2.3B effective（含 PLE 共 5.1B） | 純 attention：滑動視窗 512 與 global 交錯，35 層，128K ctx，文字+影像+音訊 | 低–中（SWA 層在 llama.cpp 需 checkpoint） | 有（35+ 語言開箱） | Apache 2.0 | 有/有 | 對每鍵校正偏大；Gemma 4 無 270M/1B 版本（卡片列 E2B、E4B、12B、26B A4B、31B） |
| LFM2-350M / 700M / 1.2B | tech report 2025-11-28（arXiv 2511.23404） | 354M / 700M / 1.2B | **hybrid**：16 層＝10 gated short-conv（kernel 3）+ 6 GQA，32K ctx，詞表 65,536 | 中（conv 狀態極小，可每 token 快照；但需自己實作） | 有（中文為「additional base support」） | LFM Open License v1.0 | GGUF 有；LFM2.5 卡連結 MLX 8-bit | S25 上 LFM2-700M decode 104 vs Qwen3-0.6B 77 tok/s（1K ctx）；作者建議用於窄任務微調 |
| LFM2.5-350M | 2026（卡片引用 2026 部落格） | 350M | 同 LFM2（10 conv + 6 GQA） | 中 | 有 | lfm1.0 | GGUF/ONNX/MLX/OpenVINO | 預訓練 28T tokens；AMD CPU decode 313 tok/s；記憶體 <1 GB |
| Granite 4.0 H 350M / H 1B（及非 hybrid 350M/1B） | 2025-10-28 | ~340M / ~1B | H 版 **hybrid**：H-350M 為 4 attention + 28 Mamba2 層，NoPE；另有純 transformer 版（RoPE） | H 版高；dense 版低 | 有（12 語言含中文） | Apache 2.0 | 社群 GGUF；MLX 可跑（M4 Max MLX 521.7 tok/s，H-350M 4-bit） | 選 dense 版可避開回滾問題 |
| SmolLM2 135M/360M/1.7B；SmolLM3 3B | SmolLM2 2024；SmolLM3 2025 | — | 純 attention | 低 | SmolLM2 以英文為主；SmolLM3 官方 6 語言（en/fr/es/de/it/pt），中文未能確認 | Apache 2.0 | 有/有 | 只適合英文子系統 |
| MobileLLM-Pro / MobileLLM-R1 / MobileLLM-Flash | Pro 2025-10；R1 2025；Flash 為 ACL 2026 Industry 論文 | Pro ~1.08B；Flash 350M/650M/1.4B | Pro：local-global attention，128K；Flash：latency-guided NAS + attention skipping | 低 | 未能確認 | Pro/R1：FAIR NC（非商用）；Flash 權重授權未能確認 | 未能確認 | Flash 宣稱行動 CPU 上 prefill 1.8×、decode 1.6×；授權限制商用 |
| Llama 3.2 1B | 2024-09（未於本次確認） | 1.24B | 純 attention GQA，128K 詞表 | 低 | 官方支援語言不含中文（未於本次確認） | Llama 3.2 Community License | 有/有 | M4（10c）MLX 4-bit：TG ~96–104 tok/s、PP ~1.1–1.3K tok/s @1k ctx |
| MiniCPM5-2B（及 MiniCPM5-1B） | 次級來源稱 2026-09-07（卡片未載日期） | 2.52B（非 embedding 1.98B） | 純 attention（LlamaForCausalLM），42 層，GQA 16Q/2KV，131K ctx | 低 | 英文＋中文 | Apache 2.0 | GGUF、MLX 4-bit、GPTQ、LiteRT-LM | 附 DSpark draft 模型供 speculative decoding；2B 對每鍵偏大 |

- （評估）就「每鍵校正」而言最實際的候選：Qwen3-0.6B（純 attention、中文強、Apache、MLX/Core AI 生態最成熟）＞ Gemma 3 270M（極小、易微調，但中文需驗證）＞ LFM2-350M/700M（速度佳但 hybrid 回滾需自建、授權需審）。Qwen3.5-0.8B 品質可能更好但 hybrid 回滾與 llama.cpp 快取問題需工程投入。


---

## 執行環境實測數字總表（僅收錄本次實際開啟頁面中的數字）

> 注意：除 llama.cpp #4167 與 oMLX 外，多數小模型數字來自 M4 Max／iPhone，而非 M4 MacBook Air；跨來源比較僅供量級參考。「—」＝來源未提供。

| 來源 | 晶片 | 執行環境 | 模型 | 量化 | Prefill tok/s（或 TTFT） | Decode tok/s | 備註 |
|---|---|---|---|---|---|---|---|
| llama.cpp #4167 | M4（10 核 GPU，120 GB/s） | llama.cpp Metal（2023-11 build） | Llama 2 7B | F16 | 230.18 | 7.43 | pp512 / tg128 |
| llama.cpp #4167 | M4（10 核 GPU） | 同上 | Llama 2 7B | Q8_0 | 223.64 | 13.54 | |
| llama.cpp #4167 | M4（10 核 GPU） | 同上 | Llama 2 7B | Q4_0 | 221.29 | 24.11 | |
| llama.cpp #4167 | M2（10 核 GPU，100 GB/s） | 同上 | Llama 2 7B | Q4_0 | 179.57 | 21.91 | 對照 |
| llama.cpp #4167 | M4 Pro（20 核 GPU，273 GB/s） | 同上 | Llama 2 7B | Q4_0 | 439.78 | 50.74 | 對照 |
| oMLX 社群（n8bzsl21） | M4（10c）16 GB | oMLX v0.2.7（MLX），macOS 26.2 | Llama-3.2-1B-Instruct | 4-bit | 1,337 @1k；1,312 @4k（TTFT 767 ms @1k） | 95.8 @1k；72.2 @4k | 2026-03-11 |
| oMLX 社群（rar2w39b） | M4（10c）24 GB | oMLX v0.2.6 | Llama-3.2-1B-Instruct | 4-bit | 1,135 @1k（TTFT 903 ms） | 97.6 @1k；70.5 @4k；55.5 @8k | 2026-03-10 |
| oMLX 社群（k566m707） | M4（10c）24 GB | oMLX v0.2.6 | Llama-3.2-1B-Instruct | 4-bit | 1,171 @1k | 104.5 @1k；75.5 @4k | 2026-03-10 |
| apple-silicon-llm-bench | M4 Max | Core AI | Qwen3-0.6B | artifact 335 MB（位元未註明） | 9,396 | 484 | 官方配方矩陣 |
| apple-silicon-llm-bench | M4 Max | MLX | Qwen3-0.6B | 4-bit | 9,366 | 432（另一協定 432.3；short-chat 555.9） | |
| apple-silicon-llm-bench | M4 Max | mlx-swift | Qwen2.5-0.5B | Q4 | TTFT 21 ms | 531.1 | short-chat |
| apple-silicon-llm-bench | M4 Max | llama.cpp | Qwen2.5-0.5B | Q4_K_M | TTFT 22 ms | 297.1 | |
| apple-silicon-llm-bench | M4 Max | coreml-llm（ANE） | Qwen2.5-0.5B | FP16 | TTFT 171 ms | 181.2 | ANE 路徑 TTFT 高 |
| apple-silicon-llm-bench | M4 Max | mlx-swift / llama.cpp | Qwen3.5-0.8B（hybrid） | Q4 / Q4_K_M | TTFT 36 / 22 ms | 421.1 / 201.1 | |
| apple-silicon-llm-bench | M4 Max | MLX / llama.cpp | Granite-4.0-H 350M（hybrid） | 4-bit / Q4_K_M | — | 521.7 / 282.5 | 2026-09 |
| apple-silicon-llm-bench | iPhone 17 Pro | MLX / Core AI GPU / Core AI ANE | Qwen3-0.6B | Q4 / INT4 / 4-bit pal. | — | 178.8 / 193.3（cold）/ 116.9 | |
| Grammarly 工程部落格 | M2 | MLX | ~1B Llama 架構（自訓） | 4-bit | — | ~210 | 2025 |
| Apple ML Research（Core ML Llama 3.1） | M1 Max | Core ML（GPU，stateful KV） | Llama-3.1-8B-Instruct | Int4（block 32） | prompt TTFT 51.91 ms | 33.67 | ctx 2048 |
| Apple ML Research（AFM 2024） | iPhone 15 Pro (A17 Pro) | Apple 內部 | AFM 裝置端 ~3B | 3.7 bpw palettization | ≈0.6 ms／prompt token（≈1,700 tok/s，換算） | 30 | 未用 speculation |
| Apple ML Research（M5 vs M4） | M4 vs M5 MBP 24 GB | MLX | Qwen3-1.7B bf16 等 | bf16/4-bit | M5 為 M4 的 3.3–4.1× | M5 為 M4 的 1.19–1.27× | 只給倍率 |
| LFM2 tech report（非 Apple 參考） | AMD Ryzen AI 9 HX 370 CPU | — | LFM2-700M vs Qwen3-0.6B | — | 4,214 vs 3,594 @1K | 134.6 vs 116.0 @1K | 架構速度比較用 |

---

## 設計啟示

1. **把 LLM 從「生成器」改成「打分器/驗證器」**：M4 上 decode 受 120 GB/s 頻寬限制、prefill 受算力限制（Apple 官方說法與 llama.cpp 數據一致）；對 <1B 模型，一次 forward 驗證 16–64 個 token 的成本約等於 1–2 次 decode。因此：候選排序用 tree/batch 打分；整句校正用「原句即 draft」的 aggressive decoding（SAD/IAD/PLD/LLMA/CopySpec 一脈）；只有分歧處才 decode。
2. **分層 pipeline（借自 two-pass ASR）**：
   - L0 每鍵、同步、≤5 ms：傳統注音引擎（詞庫＋輕量 LM）負責回顯與候選生成；LLM 不在此路徑。
   - L1 每鍵、非同步、≤30 ms：LLM 對 top-K 候選打分（共享左文 KV）；逾時即放棄，絕不晚到後再重排。
   - L2 commit 邊界、≤50 ms 套用／100–300 ms 建議：以已確認句為 draft 的驗證式校正；可先用 edit tagger（GECToR/EdiT5 類，EdiT5 在 TPU 上 P95 2.5 ms）篩可疑位置。
   - L3 使用者觸發、≤1 s：整段改寫（可用較大模型或 Apple 系統模型）。
3. **輸出格式自適應**：先輸出 1 個「需改/不需改」判斷 token（多數句子無錯即 1 次 forward 結束）；需改時才輸出「位置＋替換」或整句（借 AdaEdit 2026 的自適應格式思想、Seq2Edits 的「步數∝編輯數」）。
4. **KV 管理採精確前綴語意**：指令前綴與已確認左文常駐（Prompt Cache/APC 思想；Foundation Models 文件亦明言任何位置改變即從該點重算）；退格＝trim（純 attention 為 O(1)）；游標移回句中＝截斷到編輯點並重算後綴；不要只 patch 被改字的 KV（2026 研究顯示這樣無效）。
5. **模型選擇偏好純 attention**：hybrid（LFM2、Qwen3.5、Granite-H）在 mlx-lm 中 ArraysCache 不可 trim、llama.cpp 需 checkpoint，仍在修 bug；若選 hybrid，自建 per-token 狀態快照環形緩衝（LFM2 conv kernel=3，狀態極小）。tree attention 一次打分多分支也只適用純 attention。
6. **前綴感知訓練＋蒸餾**：仿 Pixel 6 Gboard——用大模型標註未完成句（前綴）並蒸餾到小模型；否則模型會「改」未打完的句子。中文需加入注音同音/近音錯誤（此屬主題 A/B）。
7. **量化取捨**：<1B 模型 decode 已在數 ms 內，4-bit 的延遲收益有限、品質風險較高（SLMQuant）；建議 8-bit 或 4-bit＋8-bit embedding/lm_head，並以 DWQ/QAT 在任務資料上做。
8. **執行環境**：MLX（Swift）或 Core AI（macOS 27）跑 GPU；llama.cpp 作對照；ANE 只適合常駐低功耗的小 encoder（TTFT 過高不適合 LLM 每鍵）；用 mx.compile/AOT 降低固定開銷，注意變長輸入的重編譯（shapeless 有風險），可把候選長度 pad 到少數幾種形狀。
9. **穩定性與取消**：以 erasure 計量閃爍，建議一經顯示不輕易變更（masking/biasing）；新按鍵到來即取消過時工作，只保留最新請求。
10. **Apple Foundation Models 定位**：系統模型（AFM ~3B）沒有 logprob/打分 API，語言支援含繁中但受地區限制，版本隨 OS 改變 → 不適合每鍵校正；2026 起可用 `CoreAILanguageModel` 把自訓小模型接上同一 session API，但 session 介面拿不到 logits，打分需直接用 Core AI 推論函式（需驗證）。

## 未解問題／待自測
- M4 Air（8 或 10 核 GPU）上 0.3–0.6B 模型的「固定開銷地板」：一次 forward（1 token 與 32 token）各需多少 ms？（決定 L1 預算是否可行）
- 驗證 k 個 token 的成本曲線（k=1…64）在 MLX vs llama.cpp vs Core AI 的差異。
- 持續打字 30 分鐘的功耗與 P95 漂移（無風扇降頻）；未找到公開量測。
- 「打字時視覺回饋延遲」的專門 HCI 研究（本次未找到，搜尋額度用盡）。
- Deber 2015 的 JND 原文確認（目前為轉引）；Card/Moran/Newell 100 ms 週期原文確認。
- 輸入法程序（InputMethodKit）能否使用 FoundationModels／Core AI 的權限與沙盒限制（未能確認）。
- Qwen3.5 等 hybrid 小模型在 MLX 中的實際 state 快照/還原成本。
