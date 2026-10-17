# 主題 B：中文拼寫糾錯（CSC）與神經／LLM 注音、拼音輸入法 — 研究筆記

> 撰寫日期：2026-10-17（以網路搜尋查證；未能查證者標「未能確認」）
> 範圍：CSC 研究脈絡、神經/LLM 拼音與注音輸入法、注音打字錯誤特性、資料集、≤3B 繁中小模型
> 原則：「論文主張」與「我的評估」分開寫；連結皆為實際開啟或於搜尋結果中看到且標題相符者。

---

## 進度紀錄
- [x] 1. CSC 研究（BERT 系背景 → LEMON/CSCD-NS → ReLM → C-LLM → 免訓練 LLM → 2025–2026 新作）（含 LREC 2026 LLM-IME benchmark 卡）
- [x] 2. 神經/LLM 拼音、注音輸入法（含產業短卡、注音學術與 2026 社群專案）
- [x] 3. 注音打字錯誤特性（含大千鍵位分析、語音合併研究、READIN 量化）
- [x] 4. 資料集表
- [x] 5. 小模型表（含 tokenizer 效率說明）
- [x] 設計啟示

> 卡片數：完整論文卡 17 張（第 1 部分 12 張，含 LREC 2026 LLM-IME benchmark；第 2 部分 5 張：Chen&Lee、Jia&Zhao、PinyinGPT、PERT、GeneInput）＋ 短卡 10 餘張（BERT 系一覽表、ChineseErrorCorrector、LLM 評估、PhonoThink、Moon IME、Open Vocab、Real-time IME、Proofread、產業、注音學術與 2026 社群專案）＋ 資料集表 ＋ 小模型表。

---

## 第 1 部分：中文拼寫糾錯（CSC）研究

### 1.0 背景：BERT 系 CSC（2020–2022）一覽（簡卡）

| 方法 | 出處 | 連結（已核對） | 一句話核心 | 對 IME 的意義 |
|---|---|---|---|---|
| Soft-Masked BERT | ACL 2020（Zhang et al., 字節跳動）| https://aclanthology.org/2020.acl-main.82/ | 偵測網路產生錯誤機率，以「軟遮罩」把可疑字的 embedding 往 [MASK] 推，再交給 BERT 修正 | 「先偵測再修正」的兩段式結構，適合 IME：偵測器極輕量、可常駐 |
| SpellGCN | ACL 2020（Cheng et al., 螞蟻金服）| https://aclanthology.org/2020.acl-main.81/ （arXiv 2004.14166）| 以 GCN 把字音/字形相似圖轉成字元分類器，接在 BERT 上 | 混淆集（confusion set）可直接改成「注音鍵盤相鄰鍵／近音」圖 |
| PLOME | ACL 2021（Liu et al., 騰訊）| https://aclanthology.org/2021.acl-long.233/ | 預訓練時用混淆集字取代 [MASK]，並加「拼音預測」目標；GRU 編碼拼音與筆畫 | 「發音預測」輔助目標可改為注音符號序列 |
| REALISE (ReaLiSe) | Findings of ACL 2021（Xu et al.）| https://aclanthology.org/2021.findings-acl.64/ （arXiv 2105.12306）| 語意＋字音（拼音）＋字形（圖像）三模態，selective mixing 融合 | 多模態對 IME 價值有限（IME 的錯誤幾乎全是音近） |
| SCOPE | EMNLP 2022（Li et al., 中科大）| https://aclanthology.org/2022.emnlp-main.287/ （arXiv 2210.10996）| 共享 encoder + 兩個 decoder：糾錯 + 細粒度發音成分（聲母/韻母/聲調）預測；推論時迭代修正 | 聲母/韻母/聲調分解 ≈ 注音的「聲/介/韻/調」四槽位，可直接沿用 |
| MacBERT4CSC | 開源（pycorrector, shibing624）| https://huggingface.co/shibing624/macbert4csc-base-chinese ；說明：https://github.com/shibing624/pycorrector/blob/master/examples/macbert/README.md | hfl/chinese-macbert-base + SIGHAN + Wang271K 微調；Apache-2.0 | 業界最常用 baseline；只能做等長替換（無法處理多字/漏字）。pycorrector 表列 SIGHAN-2015 = 0.8314（專案自報，GPU） |

- 共同侷限（我的評估）：這一代模型多以 SIGHAN（簡轉繁/學習者錯誤）+ Wang271K（合成 ASR/OCR 錯）訓練，LEMON 論文指出它們「過擬合錯誤模型、欠擬合語言模型」，對母語者 IME 錯誤分佈泛化差。
- 標籤: #CSC #BERT #background

---

### Rethinking Masked Language Modeling for Chinese Spelling Correction（LEMON）
- 出處: ACL 2023（long）| 連結: https://arxiv.org/abs/2305.17721 ；https://aclanthology.org/2023.acl-long.600/ ；資料: https://github.com/gingasan/lemon
- 問題: 為什麼微調 BERT 做 CSC 在新領域、新錯誤模式上泛化很差？
- 核心想法: 把 CSC 視為「語言模型 × 錯誤模型」的聯合決策（noisy channel 觀點）；發現微調 BERT 會過擬合錯誤模型（記住訓練集的錯字對）而欠擬合語言模型。解法極簡：微調時隨機遮罩 20% 的「非錯誤」token，強迫模型持續練語言模型能力，且與架構無關。
- 結果: 論文主張在 SIGHAN、ECSpell、LEMON 上達 SOTA（摘要層級，未逐一核對表格數字）。LEMON = 7 個領域（GAM 遊戲、ENC 百科、COT 合約、MEC 醫療、CAR 汽車、NOV 小說、NEW 新聞），論文 PDF 寫「超過 22,000 例、7,627 個編輯對」、自然錯誤（非人工插入）。
- 為什麼重要: 首次把「錯誤模型 vs. 語言模型」拆開分析，奠定後續 ReLM、LLM 免訓練方法的理論基礎；LEMON 成為跨領域泛化的標準測試集。
- 侷限: LEMON 為簡體中文、僅限學術用途（repo 聲明不可商用）；錯誤來源並非 IME 打字過程。
- 對我們輸入法的啟示: 若要訓練小型糾錯器，應把「語言模型」與「注音錯誤模型」解耦（LM 用大量正確繁中文本，錯誤模型用鍵盤/音近統計），而非只用錯字對微調；微調時加非錯誤 token 遮罩是零成本技巧。
- 標籤: #CSC #generalization #noisy-channel #benchmark

### CSCD-NS: a Chinese Spelling Check Dataset for Native Speakers（含 CSCD-IME / LCSTS-IME-2M）
- 出處: ACL 2024（long，pp.146–159；arXiv 首版 2022-11）| 連結: https://arxiv.org/abs/2211.08788 ；https://aclanthology.org/2024.acl-long.10/ ；資料: https://github.com/nghuyong/cscd-ns
- 問題: 既有 CSC 資料集（SIGHAN）來自外籍學習者，不能代表母語者「用拼音輸入法打字」產生的錯誤。
- 核心想法: 從 LCSTS（微博官方媒體貼文）篩出 40K 句人工標註（30K/5K/5K）；並以「模擬輸入法打字過程」生成約 200 萬句偽資料：抽樣錯誤數 → 抽樣字級/詞級 → 選 token → 抽樣音的類型（同音/近音/不同音）→ 產生新拼音 → 把「前文 + 拼音」餵給 Google IME 取候選 → 若第一候選是原詞就取第 2/3 候選 → 用 n-gram LM 困惑度過濾。
- 結果: 詞級錯誤佔 49.4%（SIGHAN 平均 23.3%）；>97% 錯誤為同音或近音，不同音僅 2.2%。字級糾正 F1：BERT 65.75、Soft-Masked BERT 67.62、PLOME 66.81；加 IME 偽資料預訓練後約 75.6–76.6；生成式模型較差：Baichuan2-7B 54.63（加偽資料 63.80）、Baichuan2-13B 59.60、ChatGPT 52.63、GPT-4 54.57。
- 為什麼重要: 這是目前最接近「IME 打字錯誤」的公開 CSC 資料與生成流程；證明 IME 模擬偽資料能大幅提升模型（+~10 F1）。
- 侷限: 作者自承：來源僅一個社群平台；IME 模擬未涵蓋簡拼、九宮格等習慣；的/地/得 等虛詞錯誤仍難。我的評估：全為簡體 + 全拼拼音，**沒有注音、沒有鍵盤相鄰鍵錯誤（typo）**——它模擬的是「選錯候選」而不是「按錯鍵」。授權：論文 CC BY 4.0；資料集授權在 repo，未能確認是否可商用（LCSTS 本身有使用限制）。
- 對我們輸入法的啟示: 直接複製其「IME 模擬」流程來造繁中注音偽資料：用 McBopomofo/libchewing 當 IME，以注音錯誤模型（相鄰鍵、聲調、ㄣ/ㄥ…）擾動注音，再取第 2/3 候選，即可大量產生貼近真實的訓練對。
- 標籤: #CSC #dataset #IME-errors #pseudo-data

### Chinese Spelling Correction as Rephrasing Language Model（ReLM）
- 出處: AAAI 2024（38(17):18662–18670）| 連結: https://arxiv.org/abs/2308.08796 ；https://ojs.aaai.org/index.php/AAAI/article/view/29829
- 問題: 序列標註式（逐字對應）CSC 太依賴錯字本身，學到的是「錯字→正字」的捷徑。
- 核心想法: 在原句後接 [SEP] 與 n 個 [MASK] 槽位，讓 BERT「改寫整句」填入目標字（P(yi|X, m1..mn)），而非逐字標註；輔以對非錯誤字 30% 遮罩的 MLM 目標，避免抄捷徑。仍是 bert-base-chinese（~110M）非自回歸，一次前向即得結果。
- 結果: ECSpell 微調 F1：LAW 91.2 / MED 82.4 / ODW 83.6；LEMON zero-shot 平均 51.2（前 SOTA 49.7）；SIGHAN-15 zero-shot 57.0。
- 為什麼重要: 證明「小模型 + 改寫式目標」能比標註式泛化更好，且維持 BERT 等級速度（後續 MSLLM 論文量到 ReLM ≈ 14.1 ms/句）。
- 侷限: 作者未列侷限節；LEMON zero-shot 版需 34M 合成句對、8×A800 訓練 60K 步。我的評估：仍限等長替換，不處理漏字/多字。
- 對我們輸入法的啟示: ReLM 類 ~100M 非自回歸模型是「常駐、每次按鍵都可跑」的理想第一層糾錯器（M4 上 CPU/ANE 估計數 ms 級，**未實測**）。
- 標籤: #CSC #BERT #non-autoregressive #efficiency

### C-LLM: Learn to Check Chinese Spelling Errors Character by Character
- 出處: EMNLP 2024 main（pp.5944–5957）| 連結: https://arxiv.org/abs/2406.16536 ；https://aclanthology.org/2024.emnlp-main.340/ ；程式: github.com/ktlKTL/C-LLM
- 問題: LLM 做 CSC 時為何常違反「等長」「音近」限制？
- 核心想法: 根源在 tokenizer：LLM 的 BPE 把多字詞併成一個 token，模型難以做字元對齊。作者刪除 Qwen1.5 詞表中多字中文 token 與對應 merge 規則（新詞表 ≈ 原詞表 89.2%），使每個漢字 = 1 token；再以 ~2B token 持續預訓練（LoRA）+ 在 CSCD-NS 訓練集 + Wang271K 上 SFT，任務變成「以複製為主、少量替換」。
- 結果: CSCD-NS 字級糾正 F1：C-LLM-14B 73.80、7B 71.64，對比 SCOPE 71.70、GPT-4 54.41；所有測試集平均 C-LLM-14B 57.49 vs SCOPE 46.70 vs 原 tokenizer SFT-14B 54.10 vs GPT-4 52.64。通用場景 +2.1%、垂直領域 +12%。
- 為什麼重要: 首次明確指出「tokenization 粒度」是 LLM 做 CSC 的關鍵瓶頸，對任何用 LLM 做繁中字級糾錯的系統都是必讀。
- 侷限: 作者自承：英文詞未處理；新詞/流行語難（需 RAG）；字級 tokenization 使解碼 token 數 +52%，推論時間 +22%（用 speculative decoding 才壓到此程度）。主結果為 7B/14B，對 ≤3B 規模效果「未能確認」（論文有 0.5B–4B 縮放研究但我未讀到數字）。
- 對我們輸入法的啟示: 若用小型 LLM 在 IME 中做字級打分，需注意 Qwen 等詞表對繁體的切分；可考慮字級 tokenization 或在候選格（lattice）上以「字」為單位打分，避免 token 邊界與注音音節不對齊。
- 標籤: #CSC #LLM #tokenization #fine-tuning

### A Simple yet Effective Training-free Prompt-free Approach to Chinese Spelling Correction Based on Large Language Models
- 出處: EMNLP 2024 main（Zhou et al., 蘇州大學 + 阿里達摩院）| 連結: https://arxiv.org/abs/2410.04027 ；https://aclanthology.org/2024.emnlp-main.966/
- 問題: 能否不微調、不寫 prompt，就讓 LLM 做好 CSC？
- 核心想法: 把 LLM 當「純粹的左到右語言模型」，搭配極簡「失真模型（distortion model）」做 noisy-channel 束搜尋：每步分數 = log P_LLM(next token) + log P_DM(原字→候選字)。失真模型的類別機率取自開發集統計：相同 0.962、同拼音 0.023、近拼音 0.008、形近 0.004、無關 0.003。另加兩個獎勵：長度獎勵（α×(len−1), α=2.5，抵消多字 token 的機率劣勢）與忠實度獎勵（以 LLM 下一字熵 (1+H) 放大失真項，LLM 不確定時更信任原文）。束寬 8（>6 後收益遞減）；用倒排索引只對與輸入字相關的 token 計分。
- 結果: Baichuan2-13B 句級 F1：Sighans 59.1、CSCD-NS 63.2、MCSCSet 66.0、ECSpell 84.5、LEMON 53.2；全面勝過以 34M 合成資料訓練的通用 BERT（47.5/52.0/35.3/57.1/48.0），但仍輸給用領域內標註資料訓練的專門模型（如 ReaLiSe Sighans 69.3）。**小模型很關鍵**：Qwen1.5-0.5B 已達 Sighans 56.3、ECSpell-Odw 84.7；1.8B 達 58.3 / 90.3；作者觀察 Qwen1.5 超過 1.8B 後收益很小、7B 甚至下降。
- 為什麼重要: 這是「LLM 作為 IME 語言模型 + 注音錯誤模型」最直接的藍本：它本質上就是 IME 的 noisy-channel 解碼，只是輸入是字而非注音。
- 侷限: 作者自承：只處理替換；計算成本高（建議量化、剪枝、蒸餾）。執行時間在附錄 F.5，我未讀到具體數字（未能確認）；但後續 MSLLM 論文量到 Baichuan2-7B、束寬 12、batch 1 約 1,276 ms/句（MSLLM 論文第 6.8 節：單張 NVIDIA A100-PCIE-40GB）——對逐鍵即時 IME 太慢，但 0.5B–1.8B 模型 + 短上下文 + 增量 KV cache 可能可行。
- 對我們輸入法的啟示: 最值得移植的設計：(1) 失真模型的類別機率表 → 改成注音版（同注音、聲調錯、相鄰鍵、ㄣ/ㄥ…）；(2) 熵加權忠實度獎勵 → 降低過度修正；(3) 結果顯示 0.5–1.8B 已足夠，正好符合 M4 16GB 預算。
- 標籤: #CSC #LLM #training-free #noisy-channel #decoding

### A Training-free LLM-based Approach to General Chinese Character Error Correction（C2EC）
- 出處: ACL 2025 main（Zhou et al.）| 連結: https://arxiv.org/abs/2502.15266 ；https://aclanthology.org/2025.acl-long.678/
- 問題: CSC 只管「替換」，但真實錯誤還有漏字、多字。
- 核心想法: 定義 C2EC（替換＋漏字＋多字），以 CCTC + LEMON 人工核驗建 benchmark；把上一篇的免訓練方法用 Levenshtein 距離擴展到變長編輯，再加一個 prompt-based LLM 輔助。
- 結果: 摘要主張：14B LLM 不微調即可在 CSC 與 C2EC 上與約 50 倍大的模型相當（具體數字未讀，未能確認）。
- 為什麼重要: IME 情境中「漏打一個音節」「多按一次空白鍵選字」都會產生變長錯誤，此擴展讓 noisy-channel 框架涵蓋它們。
- 侷限: 主結果為 14B，對 IME 端側太大；變長搜尋空間更大、更慢（我的評估）。
- 對我們輸入法的啟示: 變長編輯只需在已送出文字的「後處理糾錯」模式啟用；組字中（composing）的 lattice 解碼本來就以音節對齊，不需要它。
- 標籤: #CSC #LLM #training-free #variable-length

### Mixture of Small and Large Models for Chinese Spelling Check（MSLLM）
- 出處: ACL 2025 main（pp.28298–28311；Qiao, Zhou, Li）| 連結: https://arxiv.org/abs/2506.06887 ；https://aclanthology.org/2025.acl-long.1372/ ；程式（Apache-2.0）: https://github.com/zhqiao-nlp/MSLLM
- 問題: 微調小模型精準但過擬合編輯模式；LLM 流暢但不精準且難微調，如何兼得？
- 核心想法: 束搜尋時把三種分數相加：LLM log 機率 + (1+H_LLM)×(α·log p_失真模型 + β·log p_小模型)，α=0.5、β=0.9；LLM 越不確定（熵高），越倚重小模型（BERT/ReLM，~110M）與失真模型。LLM 不需微調，換領域只要換小模型。
- 結果: 句級 F1（ReLM+LLM）：rSIGHANs 71.8→74.4、CSCD-NS 77.3→79.9、MCSCSet 87.0→92.1、ECSpell 92.3→97.1、LEMON 50.6→61.0（LEMON 平均 +10.4）。**延遲（rSIGHAN15, batch 1, 束寬 12, Baichuan2-7B, 單張 A100-PCIE-40GB）：LLM 單獨 1,276 ms/句；BERT 13.7 ms；ReLM 14.1 ms；ReLM+BC2 1,498 ms**。小 LLM 同樣有效：ReLM+Qwen2.5-0.5B rSIGHAN15 78.4 / ECSpell-Odw 95.7 / LEMON-Nov 45.4；Qwen2.5-1.5B 78.2 / 95.7 / 48.1；同家族中更大不一定更好。
- 為什麼重要: 給出「小模型 + LLM 在解碼期融合」的明確公式與延遲數字；證明 0.5B LLM 就能拿到大部分收益——這是 IME 端側最可行的配方。
- 侷限: 作者自承：主要為 CSC、僅中文、跨語言需重做 token 對齊；與 in-context learning 衝突（chat 模型召回下降）；比 LLM 單獨略慢。我的評估：延遲數字是伺服器 GPU、束寬 12、整句解碼；IME 需要增量化。
- 對我們輸入法的啟示: IME 的「注音→字」本身就是一個小模型（n-gram/HMM 或小型神經 LM）＋錯誤模型的解碼器；可直接套用此公式：IME 原生分數（小模型）+ 0.5B LLM 分數 + 熵自適應權重，LLM 只在熵高、或候選分歧時才被呼叫。
- 標籤: #CSC #LLM #mixture #decoding #latency

### Evolving Chinese Spelling Correction with Corrector-Verifier Collaboration（ACI）
- 出處: EMNLP 2025 main（short, pp.29022–29028；Liu, Wu, Zhao）| 連結: https://aclanthology.org/2025.emnlp-main.1477/
- 問題: BERT 糾錯器有效率但泛化差；LLM 知識廣但字級編輯沒效率。
- 核心想法: Automatic Corrector Iteration：以 LLM 當「驗證者」為 BERT 糾錯器的輸出提供訓練訊號，無需人工標註即可迭代改進糾錯器。
- 結果: 摘要主張逐輪提升、優於既有資料增強、接近人工標註資料效果（頁面無具體數字，未能確認）。
- 為什麼重要: 「線上用小模型、離線用 LLM 教小模型」的範式，正好對應 IME 端側部署。
- 侷限: 驗證者 LLM 仍有偏誤；具體規模未讀到。
- 對我們輸入法的啟示: 可把使用者在 IME 中的真實修正行為（刪字重打、改選候選）匿名化後在本機以 LLM 驗證，再增量微調小糾錯器——一個隱私友善的自我改進迴圈。
- 標籤: #CSC #distillation #self-training


### Benchmarking Large Language Models for Chinese and Japanese IMEs: Phonetic-to-Character Generation and Textual Error Correction
- 出處: LREC 2026（pp.4290–4311；Zou, Lee, Fan, Li；CUNY + Meta）| 連結: https://aclanthology.org/2026.lrec-1.337/ ；程式與資料: https://github.com/junli-cuny/llm-ime-benchmark （網址取自論文 PDF 第 10 節，repo 本身未開啟，未能確認內容）
- 問題: LLM 是否能直接當 IME 的「拼音→漢字」引擎與錯誤修正器？精度與延遲如何取捨？
- 核心想法: 以 zero/few-shot prompt 讓 LLM「端到端」把拼音/羅馬字轉成文字，並修正兩類錯誤：(a) 拼音字串錯（漏鍵、多鍵、相鄰鍵 typo、兩鍵順序顛倒；由 CSCD-IME 句子程式化產生）、(b) 字級同音選錯（用 CSCD-IME 生成器產生）。對照組：macOS 拼音、Pinyin2Hanzi（HMM/DAG）、pycorrector。量 CER、BERTScore、完成時間、TTFT。
- 結果（論文表 3–5，中文）: 句子級拼音→漢字 CER：GPT-4 0.047、Gemini 2.0 Flash 0.051、**macOS 拼音 0.085**、Pinyin2Hanzi DAG 0.555、**Llama3.2-3B（本地，A100）0.746**。拼音錯誤修正 CER：Gemini 2.0 Flash 0.211、GPT-4o 0.713、Llama3.2-3B 1.006。字級修正 CER：GPT-4o 0.235、pycorrector 0.315（0.377 s）、Llama3.2-3B 0.515。macOS 拼音句子轉換 1.728 s（模擬鍵入，含 UI），雲端 LLM TTFT ≈ 0.4–0.7 s。
- 為什麼重要: 2026 年少數直接評估「LLM 當 IME」的同儕審查論文；結論是作者主張的「hybrid IME」（本地快模型 + 雲端大模型）。
- 侷限: 作者自承：全自動評估、無使用者研究；**只測端到端，不測「快速基線產生候選 + LLM 重排序」的管線**；錯誤為合成；本地模型效率在 A100 上量測，不代表消費級/行動裝置；僅簡體/拼音。我的評估：用 prompt 讓 3B 通用模型「生成」整句是最差的用法（CER 0.75），不代表 3B 模型當「打分器」的能力（對照 Zhou et al. 2024：0.5–1.8B 當純 LM 打分效果很好）；且 TPS/延遲含 API 網路因素，對端側設計參考價值有限。
- 對我們輸入法的啟示: **不要讓小 LLM 自由生成轉換結果**；應讓傳統 lattice/詞典 IME 產生候選（它們在短輸入上依然最強、<10 ms），LLM 只做受限打分/重排序——這正是本文沒測、但自承值得比較的管線設計。
- 標籤: #IME #LLM #benchmark #latency #pinyin

### CEC-Zero: Zero-Supervision Character Error Correction with Self-Generated Rewards
- 出處: AAAI 2026（poster，依 arXiv comments）| 連結: https://arxiv.org/abs/2512.23971
- 問題: 監督式 CSC 依賴昂貴標註且對新錯誤泛化差。
- 核心想法: 零監督強化學習：從乾淨文本自動造錯 → LLM 產生多個候選 → 以語意相似度 + 候選一致性的「群集共識」獎勵打分 → PPO 最佳化；作者宣稱獎勵無偏且收斂有理論保證。
- 結果: 作者主張在 9 個 benchmark 上比監督式基線高 10–13 F1、比強 LLM 微調高 5–8 F1（模型大小頁面未載，未能確認）。
- 為什麼重要: 顯示 2026 年 CSC 趨勢轉向 RL + 自生成資料，可降低對繁中標註資料的依賴。
- 侷限: 模型規模、延遲未知；RL 訓練成本高（我的評估）。
- 對我們輸入法的啟示: 繁中注音錯誤缺乏標註資料，可用「乾淨繁中語料 + 注音錯誤模型造錯 + 自生成獎勵」訓練端側小模型，免人工標註。
- 標籤: #CSC #RL #zero-supervision #2026

### CSRP: Chain-of-Thought Reasoning for Chinese Text Correction via Reinforcement Learning with Efficiency-Aware Rewards
- 出處: ACL 2026 main（依 arXiv comments 與 ACL Anthology 2026.acl-long.1733）| 連結: https://arxiv.org/abs/2606.00020 ；https://aclanthology.org/2026.acl-long.1733/
- 問題: LLM 做中文文本糾錯會過度修正，SFT 不直接最佳化精確率。
- 核心想法: 三階段：5.9M 平衡樣本持續預訓練 → CoT SFT（顯式錯誤推理）→ GRPO + 「效率感知獎勵」懲罰不必要的編輯。
- 結果: NaCGEC F0.5 50.99、P 57.17；CSCD 拼寫糾錯 F1 59.61（比 GPT-4 高 5.20）；RL 階段約 +8% 相對增益。搜尋結果片段顯示表中標為「CSRP (4B)」（未逐字核對，未能完全確認）。
- 為什麼重要: 「懲罰不必要編輯」的獎勵設計直接對應 IME 最怕的過度修正。
- 侷限: CoT 推理在 IME 即時路徑中太慢（我的評估）；主要針對語法錯誤。
- 對我們輸入法的啟示: CoT 不適合即時路徑，但「效率感知獎勵」（編輯越少越好）可用來 RL 微調我們的小糾錯模型，壓低誤改率。
- 標籤: #CGEC #CSC #RL #over-correction #2026

### GUIDE: Generative Unsupervised Chinese Query Correction via Phonetic and Visual Shared-ID Encoding
- 出處: EMNLP 2026 Industry Track（依 arXiv comments；快手 Kuaishou 團隊推測，未能確認）| 連結: https://arxiv.org/abs/2608.25343
- 問題: 搜尋查詢糾錯需無監督、可隨流行語快速更新，且 LM 常把短查詢「過度修正」成常見詞。
- 核心想法: 「先混淆再釐清」：把音近/形近字映射到共享 ID（同一混淆鄰域），encoder-decoder 從共享 ID 序列重建原查詢；以時間衰減、查詢頻率加權目標適應新詞。修正被限制在可信混淆鄰域內。
- 結果: QSpell 250K 與 KwaiSearch 上優於基線，線上 A/B 改善（頁面無具體數字，未能確認）。
- 為什麼重要: 「共享 ID」本質上是把字投影到發音等價類——與 IME 的「注音音節 → 字」完全同構。
- 侷限: 面向短查詢、簡體；模型規模未載。
- 對我們輸入法的啟示: 對注音 IME 而言，「共享 ID」天然就是注音音節（含或不含聲調）；可訓練一個「注音音節序列 → 字」的小 encoder-decoder/LM，並用頻率+時間衰減持續學新詞。
- 標籤: #CSC #query-correction #phonetic #unsupervised #2026

### ChineseErrorCorrector3-4B / pycorrector LLM 系列（模型短卡）
- 連結: 論文 https://arxiv.org/abs/2511.17562 （Tian & Zhou, 2025-11）；模型 https://huggingface.co/twnlp/ChineseErrorCorrector3-4B （Apache-2.0，基於 Qwen3-4B）；https://huggingface.co/shibing624/chinese-text-correction-1.5b （Apache-2.0，基於 Qwen2.5-1.5B-Instruct，約 108K 筆訓練資料）
- 是什麼: 開源、可直接下載的「LLM 生成式」中文拼寫＋語法糾錯模型（處理多字、漏字、錯字、語序、語法）。
- 關鍵數字（模型卡自報，F1）: Qwen3-4B-CTC 平均 0.8521（SIGHAN-2015 0.6340 / EC-LAW 0.9360 / MCSC 0.9864），QPS 5；Qwen2.5-1.5B-CTC 平均 0.6802（SIGHAN-2015 只有 0.3032 / EC-LAW 0.7846 / MCSC 0.9529），QPS 6（Tesla V100 32GB）；對照 MacBERT4CSC 在 SIGHAN-2015 為 0.8314。
- 與我們的關聯: 最接近「拿來就能在 Mac 上跑的 ≤4B 中文糾錯 LLM」；可作為後處理糾錯的 baseline 與蒸餾教師。
- 侷限: 皆以簡體資料訓練，模型卡未提繁體（未能確認繁中表現）；生成式整句重寫 ≈ 每句數百 ms 等級（V100 上 QPS 5–6），不適合每次按鍵呼叫；1.5B 在 SIGHAN-2015 上遠輸 BERT 系，顯示小生成模型做 CSC 不一定划算。
- 標籤: #CSC #LLM #open-model #baseline

### LLM 做 CSC 的評估與常見失誤（綜合短卡）
- 代表文獻: Eval-GCSC（arXiv 2311.08219, Li et al. 2023）https://arxiv.org/abs/2311.08219 ；C-LLM（見上）；CSCD-NS（見上）；survey: Chinese Spelling Correction: A Comprehensive Survey of Progress, Challenges, and Opportunities（arXiv 2502.11508, Liu et al., 2025-02）https://arxiv.org/abs/2502.11508
- 重點: (1) **過度修正**：ChatGPT 類模型會把通順但非最佳的表達也改掉，違反最小編輯原則；(2) **長度不一致**：生成式模型常增刪字，傳統 CSC 指標（要求等長）直接判錯；Eval-GCSC 主張以詞級+語意相似度放寬後，ChatGPT 與 token 分類模型相當；(3) **tokenization**：多字 token 使字級對齊困難（C-LLM）；(4) 數字證據：CSCD-NS 上 GPT-4 字級 F1 54.57 vs BERT 65.75（CSCD-NS 論文）；C-LLM 報告 GPT-4 句級 F1 54.13 vs BERT 63.67（取自搜尋摘要，未逐字核對）。
- 對我們的意義: IME 的糾錯必須「受限」：限定等長或音節對齊、限定候選來自注音混淆集、對不確定的修正只提示不自動改。
- 標籤: #CSC #LLM #evaluation #over-correction

### PhonoThink: Improving Large Language Models' Reasoning on Chinese Phonological Ambiguities（短卡）
- 出處: EMNLP 2025 main（pp.19007–19022）| 連結: https://aclanthology.org/2025.emnlp-main.961/
- 重點: 以三個音韻子任務資料集 + 合成逐步推理鏈做 SFT，再以 RL 穩定推理，提升 LLM 對中文同音/音近歧義的處理（拼寫糾錯、語音轉文字等）；作者主張 base model 可接近大得多的模型（頁面無數字與模型大小，未能確認）。
- 對我們的意義: 說明通用 LLM 對「拼音/注音層級」的知識薄弱，需要專門的音韻資料微調；我們的 LM 若要理解注音輸入，最好在訓練時加入「注音 ↔ 字」配對資料。
- 標籤: #LLM #phonology #reasoning


---

## 第 2 部分：神經／LLM 拼音與注音輸入法

> 另見第 1 部分「Benchmarking LLMs for Chinese and Japanese IMEs（LREC 2026）」卡。

### A New Statistical Approach to Chinese Pinyin Input（Chen & Lee）
- 出處: ACL 2000（pp.241–247；Microsoft Research China）| 連結: https://aclanthology.org/P00-1031/
- 問題: 句子式拼音輸入在真實使用者「打錯鍵」時錯誤會連鎖放大，如何容錯？
- 核心想法: 經典 noisy-channel：argmax_H Pr(P|H)·Pr(H)^α，Pr(H) 為 trigram LM，Pr(P|H) 為「打字模型（typing model）」——以 HMM 建模每個按鍵的替換/插入/刪除，狀態綁定成 27 個（a–z + 未知），參數由真實打字資料訓練；另有英文拼字模型支援免切換（modeless）中英輸入。
- 結果（PDF 內文）: 收集 100 位使用者、每人約 8 小時打字資料；真實輸入中約 4.6% 漢字的拼音打錯，使整句轉換錯誤率由完美輸入的 6.82% 暴增至 20.84%（約 3 倍）；加入打字模型（LM 權重 0.5）後轉換錯誤率約降 30%。引用心理學統計：打字錯誤中 43% 為同列相鄰鍵替換、15% 為同欄相鄰鍵、10% 為左右手鏡像鍵。
- 為什麼重要: 26 年前就證明「鍵入錯誤模型 + 語言模型聯合解碼」是 IME 容錯的正解，且 α（LM 權重）可依使用者熟練度調整。
- 侷限: n-gram LM、拼音 26 鍵；資料未公開。我的評估：心理學的鍵位錯誤比例來自英文打字，對注音 4 列 41 鍵（含數字列）需重估。
- 對我們輸入法的啟示: 我們的注音版「打字模型」應同樣以按鍵為單位（41 個注音/聲調鍵），並學每位使用者的錯誤分佈與 α；這是 LLM 時代仍然必要的「通道模型」。
- 標籤: #IME #noisy-channel #typing-model #classic

### A Joint Graph Model for Pinyin-to-Chinese Conversion with Typo Correction（Jia & Zhao）
- 出處: ACL 2014（pp.1512–1523；上海交大）| 連結: https://aclanthology.org/P14-1142/
- 問題: 拼音切分、打錯字修正、拼音→漢字轉換若分開做，錯誤會級聯放大。
- 核心想法: 基於兩種解碼演算法的等價性，把「拼音切分＋typo 修正＋轉換」放進同一張圖，用 K-shortest paths（K=20）全域最佳化；音節候選以 Levenshtein 距離 <2 篩選。
- 結果（PDF 表 3，10K 句）: 無錯輸入（0-P）時基線句正確率 48.87%；**2% 鍵入錯誤率時基線句正確率崩到 11.12%、Google Input Tools 11.08%，聯合模型回到 40.69%**；5% 錯誤率時 3.32% → 27.86%。MIU 準確率 2% 錯誤時 50.47% → 75.55%。速度：Pentium E6700 上 >2000 字/分。另引 Zheng et al. 2011 估計真實拼音打錯率約 1.51%（5 位母語者、2000 句、11,968 詞）。
- 為什麼重要: 給出最清楚的量化證據——**即使只有 2% 的按鍵錯誤，不容錯的整句 IME 句正確率會掉到原本的約 1/4**；聯合解碼可收回大部分損失。
- 侷限: 錯誤為隨機模擬（非真實分佈）；n-gram LM。
- 對我們輸入法的啟示: 注音 typo 修正必須在「組字期的候選格」內與轉換聯合解碼，而不是轉換完再修；且 2% 按鍵錯誤率是合理的設計基準。
- 標籤: #IME #typo-correction #lattice #joint-decoding

### Moon IME: Neural-based Chinese Pinyin Aided Input Method with Customizable Association（短卡）
- 出處: ACL 2018 System Demonstrations（pp.140–145；上海交大 Huang, Li, Zhang, Zhao）| 連結: https://aclanthology.org/P18-4024/
- 重點: 以注意力式 NMT 做拼音→字轉換，結合資訊檢索提供可自訂的「聯想」；Windows TSF 實作。
- 對我們的意義: 早期「神經 P2C 實際做成 IME」的範例；聯想（下一詞預測）可沿用同一 LM。
- 標籤: #IME #neural #demo

### Open Vocabulary Learning for Neural Chinese Pinyin IME（短卡）
- 出處: ACL 2019（pp.1584–1594；Zhang, Huang, Zhao）| 連結: https://aclanthology.org/P19-1154/ ；arXiv 1811.04352 ；程式: cooelf/OpenIME（取自 Anthology 頁，未開啟）
- 重點: 神經 P2C + 線上更新詞彙（取樣機制）以學習使用者新詞；作者主張 CPU 解碼時間最多降 50%、精度幾乎不降，並在標準語料與真實使用者輸入歷史上勝過商用 IME。
- 對我們的意義: 「個人詞庫線上學習」必須與神經模型共存；詞彙層（lexicon）應獨立於模型權重，才能即時加入新詞。
- 標籤: #IME #neural #open-vocabulary #personalization

### Exploring and Adapting Chinese GPT to Pinyin Input Method（PinyinGPT）
- 出處: ACL 2022（pp.1899–1909；Minghuan Tan, Yong Dai 等 8 位作者；機構未逐一核對）| 連結: https://arxiv.org/abs/2203.00249 ；https://aclanthology.org/2022.acl-long.133/ ；程式: https://github.com/VisualJoyce/Transformers4IME
- 問題: 預訓練中文 GPT 能否直接當拼音輸入法？縮寫拼音（只打聲母）怎麼辦？
- 核心想法: 字級 12 層 GPT（GPT-2 small 配置）凍結即可做全拼轉換：以拼音限制候選字、逐字左到右生成。縮寫拼音時歧義爆炸，故提出 PinyinGPT-Concat（把拼音序列以 [SEP] 接在上下文後並與目標字位置對齊）與「拼音受限訓練」（softmax 只在同拼音的字之間計算，專門學分辨同音字）。另建 WD 資料集：15 個新聞領域、270K 例。
- 結果: 全拼（PD，無上下文）P@1：GPT 73.15 vs Google IME 70.90 vs On-P2C 71.30；縮寫拼音（WD）P@1 僅 26.90→27.75、P@5 35.56→40.66（GPT → 全參數微調 PinyinGPT-Concat）。延遲：6 層模型約 94 ms vs 12 層約 142–145 ms（某長度配置），但 6 層在縮寫拼音 P@5 由 41.51 掉到 32.70。
- 為什麼重要: 證明「凍結的通用 LM + 讀音限制」就能做到商用水準的轉換；「同音字之間的 softmax」是專為 IME 設計的訓練目標。
- 侷限: 作者自承縮寫拼音仍弱；我的評估：延遲為 GPU 量測、整段解碼；簡體、無打錯鍵情境。
- 對我們輸入法的啟示: 注音 IME 的 LLM 打分應採「讀音受限」：每個位置只比較同注音（或注音混淆鄰域）的字，等同 PinyinGPT 的 pinyin-constrained softmax——計算量小、且天然避免 LLM 亂改。若要微調，用「同音字對比損失」最對症。
- 標籤: #IME #GPT #pinyin #constrained-decoding

### PERT: A New Solution to Pinyin to Character Conversion Task
- 出處: arXiv 2205.11737（2022-05；華為諾亞方舟實驗室；未見正式出版）| 連結: https://arxiv.org/abs/2205.11737
- 問題: n-gram/RNN 的 P2C 容量不足；能否用只吃拼音的雙向 Transformer 做 P2C？
- 核心想法: PERT = 只以拼音為輸入的 BERT 式雙向 encoder，直接在大量「拼音→字」標註對上訓練（非預訓練-微調）。再以馬可夫框架與 n-gram 結合：n-gram 當轉移機率 P(c_n|c_<n)、PERT 當發射機率 P(c_n|y_1..n)，Viterbi 解碼；外接詞典處理 OOD。
- 結果: 新聞測試集字級/句級準確率：bigram 85.10/34.26、LSTM 89.71/47.87、PERT-base（91.1M）96.59/73.31；PERT-tiny（1.3M）+bigram 字級 91.28、PERT-base+bigram 96.99。延遲（ms/token）：bigram 1.80（CPU）、PERT-tiny 0.34、PERT-base 1.71（GPU）。作者建議雲端用 PERT-base、邊緣裝置用 PERT+n-gram。訓練語料約 2.6M 篇新聞、約 24 億字。
- 為什麼重要: 業界（華為）給出「小型雙向 encoder 當 IME 發射模型 + n-gram/LM 當轉移模型」的可部署配方與參數-精度曲線（1.3M–91M）。
- 侷限: 未正式發表；簡體、無 typo；我的評估：雙向 encoder 需看到整段拼音，逐鍵增量時要重算或用滑動視窗。
- 對我們輸入法的啟示: 「注音 PERT」（注音序列 → 字的雙向小 encoder，數 M 到數十 M 參數）可作為組字期的主力打分器，LLM 只做跨句/長距離的轉移打分——這正是 2026 社群專案 SlothLM-E / McBopomofoLM 採用的路線（見下）。
- 標籤: #IME #encoder #pinyin #hybrid #edge

### Generative Input: Towards Next-Generation Input Methods Paradigm（GeneInput，訊飛）
- 出處: arXiv 2311.01166（2023-11；科大訊飛研究院 + 中科大；未見正式出版）| 連結: https://arxiv.org/abs/2311.01166
- 問題: 既有神經 IME 假設拼音正確，無法處理真實的雜訊鍵入，也無法從使用者回饋學習。
- 核心想法: 以訊飛自研 2.6B「星火」GPT 類模型直接吃原始按鍵序列（26 鍵或 9 鍵），先預測拼音切分（pyseg）再生成漢字；以對齊限制剔除無法對回輸入的路徑；以三個月真實使用者「選了哪個候選」自動產生獎勵標籤訓練 reward model（二元：選中=正、未選=負），再用 PPO-ptx 做 RLHF。
- 結果: 全鍵入→字（FK2C）P@1：PD 88.4（PinyinGPT 73.2、On-P2C 71.3、Google IME 70.9）；自建 XF 26 鍵完美拼音 94.2 vs Google 88.0；**含雜訊的 26 鍵輸入 75.2 vs Google IME 55.2；雜訊 9 鍵 46.2 vs 7.2**。拿掉 pyseg 掉約 6 點。以 prompt 讓 GPT-4 做 FK2C 僅 18.1（ChatGPT 12.3）。
- 為什麼重要: 目前最強的「LLM 直接吃鍵入序列、內建容錯」產業證據；也證明 prompt 式通用 LLM 做轉換很差、必須專門訓練。
- 侷限: 無延遲數字（作者提到日呼叫上億次、計畫縮小到可在手機執行）；簡體拼音；2.6B 對逐鍵即時在 M4 上偏重（我的評估）。
- 對我們輸入法的啟示: (1) 以「使用者是否選中候選」作為免標註的偏好訊號，可在本機做個人化；(2) 「先預測切分再轉換」對注音同樣適用（注音天然以聲調鍵/空白分音節，但漏打聲調時需切分推斷）。
- 標籤: #IME #LLM #industry #noisy-input #RLHF

### Real-time Neural-based Input Method（短卡，日文）
- 出處: arXiv 1810.09309（2018；Yao, Shu, Li, Ohtsuki, Nakayama）| 連結: https://arxiv.org/abs/1810.09309
- 重點: LSTM LM 用於日文轉換與下一詞預測；指出瓶頸在大詞表 softmax，提出「incremental selective softmax」（只算候選子集、詞表更新時補算），softmax 加速約兩個數量級，在一般 CPU 上達即時；模型壓縮 92% 不掉精度（作者主張）。
- 對我們的意義: IME 打分只需要「候選字子集」的機率，不需要全詞表 softmax——對 15 萬詞表的 Qwen/Gemma 更是關鍵優化（只取候選 token 的 logits）。
- 標籤: #IME #efficiency #softmax

### Proofread: Fixes All Errors with One Tap（Gboard，短卡）
- 出處: ACL 2024 System Demonstrations（pp.286–293；Google）| 連結: https://aclanthology.org/2024.acl-demos.27/
- 重點: Gboard 句/段落級一鍵校正；PaLM2-XS 經 SFT（rewrite → proofread 依序訓練最好）+ RL；人工金標集「good ratio」85.56%；部署於 Pixel 8，模型在雲端 TPU v5 推論，以量化、bucket inference、文本切段、speculative decoding 降延遲（無具體數字）。
- 對我們的意義: 業界大廠的「整句 LLM 校正」仍走伺服器端、且是「使用者按鈕觸發」而非逐鍵自動——佐證「即時路徑用小模型、整句重寫用手動觸發」的分層設計。
- 標籤: #industry #proofread #English #server-side

### 商用輸入法的大模型化：訊飛、搜狗、微信、百度、Apple（產業短卡；多為新聞稿，技術細節多未公開）
- 訊飛輸入法 14.0（2024-10）：「訊飛星火端側輸入大模型」——**在手機本地算力運行**，用於 AI 增強聯想、整句預測、整句聯想（拼音與語音），離線可用；參數量未公開；新聞未提糾錯。來源：https://www.ithome.com/0/805/308.htm 。15.0（2025-11）主打個人化 AI 鍵盤（依新聞搜尋摘要，未逐頁核對）。
- 搜狗輸入法 20.0（2026-01）：宣稱「全面 AI 化」；自研「AI 打字大模型」結合上下文理解提升候選（十億級詞庫）；語音基於騰訊混元；未說明端側或雲端、未提打字糾錯。來源：https://news.aibase.com/zh/news/24988
- 微信輸入法（WeType，騰訊）：2024-06 首個接入混元大模型的 AI 版本，2025-01 4.0；App 介紹稱內建 DeepSeek 與混元、提供「智能拼寫」（具體機制未公開，未能確認）。來源：https://www.ifanr.com/1590165
- 百度輸入法：2023 年接入文心大模型的「超會寫」為改寫/生成功能，非糾錯（依搜尋摘要）；未找到打字糾錯技術資料。
- Apple：iOS 17（2023）起鍵盤自動修正改用裝置端 transformer 語言模型（官方對英文的描述）；**對中文/注音是否使用同類模型，未找到公開資料（未能確認）**。來源：https://9to5mac.com/2023/06/05/ios-17-iphone-autocorrect/
- 我的評估: 2024–2026 商用 IME 的「大模型」主要用在聯想、整句預測、語音、改寫；**沒有任何一家公開說明其「打字糾錯」如何在端側用 LLM 實作或量化效果**。唯一有數字的是訊飛 GeneInput 論文（上卡）。
- 標籤: #industry #IME #on-device

### 注音（Bopomofo）專屬研究與 2026 開源社群專案
**A. 學術（台灣碩士論文為主；未找到中研院/台大/清大以神經 LM 做注音轉換或注音 typo 的期刊/會議論文——搜尋多次皆無，標為「未找到」，非「不存在」）**
- 高佩君〈注音輸入錯誤更正機制之研究〉（國立暨南國際大學資工碩士，2018，指導：黃光璿）https://www.airitilibrary.com/Article/Detail/U0020-1708201814001600 ——對「不合法注音序列」依序嘗試相鄰注音符號換位、大千鍵盤相鄰鍵替換，找最可能的合法序列；不行再嘗試拆成兩個音節並查詞庫。SIGHAN 語料模擬實驗：換位修正平均 71.70%、按鍵替換 49.20%、混合 59.96%；若排除「修正後仍是合法別字」的情況，按鍵替換升到 84.04%。→ 顯示**單靠「合法性」偵測會漏掉「打錯鍵卻剛好合法」的錯誤**，必須有語言模型。
- 廖偉超〈注音輸入法的錯誤更正與選字預測之研究〉（暨大資工碩士，2021，指導：黃光璿）https://www.airitilibrary.com/Article/Detail/U0020-1608202111381600 ——指出微軟注音、自然輸入法對不合法注音序列沒有錯誤更正機制；以注音序列編輯距離改成字典中最近的合法序列；例：大千相鄰鍵把「ㄍㄨㄟˇ」打成「ㄍㄨㄛˇ」。
- 李啟維〈基於隱藏式馬可夫模型的中文改錯〉（台大資工碩士，2017，指導：張智星、張俊盛）https://tdr.lib.ntu.edu.tw/bitstream/123456789/2721/1/ntu-106-1.pdf ——用聯合報改稿記錄建第一個「專業編輯」錯字測試集；noisy channel + LM，beam search 優於 HMM；指出「注音輸入法是繁體中文常見輸入方式，相關錯誤屬於大宗」，並列出注音近音規則（見第 3 部分）。
- Sub-Character Tokenization for Chinese Pretrained Language Models（TACL 2023, 11:469–487；Si et al.，清華+華為）https://aclanthology.org/2023.tacl-1.28/ ——以字音（含注音轉寫）或字形把字轉成子字元序列再做子詞切分；同音字得到相同切分，因此對同音錯字天然穩健，序列也更短。→ 我們的 LM 可加入「注音 token」通道。
- CKIP 錯字偵測系統（中研院）https://ckip.iis.sinica.edu.tw/service/typo/ ——搜尋摘要稱偵錯率 78%；頁面開啟時回傳 502，**未能確認**。

**B. 2026 開源社群：Apple Silicon 上的「注音 + 小模型」IME（皆為自報數字、非同儕審查）**
- 善解 shanjie（Apache-2.0；v0.4.7 2026-10-17；需 macOS 26、Apple silicon）https://github.com/Nanako0129/shanjie ——詞庫 + bigram + 詞性回退做整句轉換，背景以約 14.4M 參數的「字級小 LM（M）」重排**前 8 名整句候選**，不生成新字。作者實驗：本機 Qwen3-1.7B（4-bit）在保留集 87.2% vs 84.4%（p=0.081，未視為顯著勝出），且**逐候選打分 p95 510–640 ms，超過其 300 ms 門檻而被放棄**；Apple 裝置端模型反而由 83.5% 降到 75.4%。打字容錯（fuzzy）列為未來工作。
- McBopomofoLM（小麥注音 fork；程式 MIT、模型 Apache-2.0）https://github.com/workfunction/McBopomofoLM ——兩個 Core ML 模型跑在 ANE：每打完一個音節跑 ~25M 雙向 encoder（SlothE-T）為各位置打分；僅在多候選競爭時跑 ~60M decoder（Qwen3.5 架構）於 encoder 前 3 名中依上下文選字、且只在有把握時改字；音節不合法後接空白時會建議修正。Common Voice zh-TW 1,500 句：字準確率 95.25%→96.55%、整句 70.4%→79.1%。資源：常駐 100–200 MB 記憶體、30 分鐘正常打字約耗 40 秒 CPU、首次編譯 1–3 分鐘、磁碟 ~1.4 GB。
- SlothLM-E（Luigi/slothlm-e-12m-zhuyin；Apache-2.0）https://huggingface.co/Luigi/slothlm-e-12m-zhuyin ——注音音節序列 → 繁中字的非自回歸雙向 encoder（12 層、d=256；頁面參數量 11.6M/3.9M 前後不一致），每位置限定為該讀音的合法字；以 c4-chinese-zhtw 約 110 萬句、用 g2pW 標台灣讀音訓練。句正確率：同音難例 159 句 82%（libchewing 71%）、日常「免選字」230 句 72%（作者自承先前 84–86% 有資料洩漏而更正）、無聲調 159 句 79%。
- 創音輸入法 TronIn（免費、閉源推測；Apple Silicon、macOS 13+）https://tronin.1tron.ai/ ——中英免切換大千注音；Viterbi + 詞頻 + bigram；**支援模糊注音：ㄓㄗ、ㄔㄘ、ㄣㄥ、ㄢㄤ、ㄋㄌ、ㄈㄏ，並可依聲調重排打亂順序的介音/韻母**，標準讀音優先。
- 小麥注音 McBopomofo（MIT；3.1.1，2026-09-02）https://mcbopomofo.openvanilla.org/ ——「簡化的 n-gram 模型 + 最大概似估計」選字；官方頁未提容錯或 AI。
- 我的評估: 2026 年社群已收斂到「**詞庫/格點產生候選 → 數十 M 參數的注音條件 encoder 打分 → （必要時）小 decoder 在前 k 名中裁決**」的分層架構；1.7B 級 LLM 在 M 系列上「逐候選打分」的延遲（p95 > 500 ms）是被實測否決的關鍵原因。這與學術上 PERT（encoder+n-gram）、MSLLM（小模型+LLM 熵加權）的結論一致。
- 標籤: #Zhuyin #IME #Taiwan #on-device #community


---

## 第 3 部分：台灣使用者注音打字的錯誤特性

> 重要聲明：**我沒有找到任何公開的「注音鍵入錯誤」大規模統計**（多次中英文搜尋：注音 打字 錯誤 類型 統計、Zhuyin typo、Bopomofo error 等）。以下分成「已查證的事實」與「我的推論」兩類；推論需以實際使用者記錄（opt-in keystroke logging）驗證。

### 3.1 大千（標準）鍵位（已核對：vChewing 手冊 https://vchewing.github.io/manual/arranges.html ）
| 列 | 鍵位 → 符號 |
|---|---|
| 數字列 | 1ㄅ 2ㄉ 3ˇ 4ˋ 5ㄓ 6ˊ 7˙ 8ㄚ 9ㄞ 0ㄢ -ㄦ |
| 上列 | qㄆ wㄊ eㄍ rㄐ tㄔ yㄗ uㄧ iㄛ oㄟ pㄣ |
| 中列 | aㄇ sㄋ dㄎ fㄑ gㄕ hㄘ jㄨ kㄜ lㄠ ;ㄤ |
| 下列 | zㄈ xㄌ cㄏ vㄒ bㄖ nㄙ mㄩ ,ㄝ .ㄡ /ㄥ |
| 一聲 | 空白鍵 |

- 已查證的設計特性：同一「直排」發音部位相同（ㄅㄆㄇㄈ 唇音、ㄉㄊㄋㄌ 舌尖、ㄍㄎㄏ 舌根、ㄐㄑㄒ 舌面、ㄓㄔㄕㄖ 捲舌、ㄗㄘㄙ 舌尖前、ㄧㄨㄩ 介音、ㄚㄛㄜㄝ、ㄞㄟㄠㄡ、ㄢㄣㄤㄥ）；聲調鍵在最上排、離手指最遠（vChewing 手冊與維基百科皆提及）。
- 已查證：電腦注音輸入法以「聲母/介音/韻母/聲調」固定槽位組字，**音節內的符號順序可以打亂**（例：先打 ㄧ 再打 ㄋ 再打 ˇ 仍得「你」，只有聲調須最後）——見旺宏科學獎報告 SA22-160（王冠侖）https://www.mxeduc.org.tw/scienceaward/history/projectDoc/22th/doc/SA22-160_final.pdf 。創音輸入法也宣稱可「依聲調重排打亂順序的介音/韻母」。

### 3.2 由鍵位推得的「動作型（motor）」錯誤（我的分析，依上表 + 標準 ANSI 鍵盤錯位推算）
1. **垂直相鄰 = 同發音部位**：因大千「直排同部位」，上下誤觸會變成「近音」錯誤，如 ㄋ(s)↔ㄌ(x)、ㄎ(d)↔ㄏ(c)、ㄑ(f)↔ㄒ(v)、ㄕ(g)↔ㄖ(b)、ㄘ(h)↔ㄙ(n)、ㄧ(u)↔ㄨ(j)、ㄨ(j)↔ㄩ(m)、ㄢ(0)↔ㄣ(p)、ㄣ(p)↔ㄤ(;)、ㄤ(;)↔ㄥ(/)、ㄠ(l)↔ㄡ(.)、ㄜ(k)↔ㄝ(,)。→ **在大千鍵盤上「按錯鍵」與「音近」高度重疊**，失真模型不能只用語音相似度，必須同時建模鍵距。
2. **水平相鄰 = 跨部位**：同列左右誤觸，如 ㄔ(t)↔ㄗ(y)、ㄕ(g)↔ㄘ(h)、ㄖ(b)↔ㄙ(n)（捲舌↔平舌但不同送氣/類別）、ㄟ(o)↔ㄣ(p)、ㄛ(i)↔ㄟ(o)、ㄚ(8)↔ㄞ(9)↔ㄢ(0)。PTT 使用者經驗例：手機注音把 ㄘ 點成相鄰的 ㄕ（https://www.ptt.cc/bbs/iOS/M.1676021830.A.4C9.html ，個人經驗，非統計）。廖偉超（2021）例：ㄍㄨㄟˇ→ㄍㄨㄛˇ（ㄟ/ㄛ 相鄰）。
3. **聲調鍵互觸**：ˇ(3)↔ˋ(4)、ˊ(6)↔˙(7) 相鄰；ˋ(4)、ˊ(6) 又夾著 ㄓ(5)——**誤觸 ㄓ 會開啟新音節造成切分錯誤**，這是注音特有的錯誤型態。
4. **槽位覆寫造成「合法但錯」**：因組字是固定槽位，音節內誤按同類鍵會「覆寫」而非產生非法序列（例：想打 ㄋ 卻按到 ㄌ，結果是合法的 ㄌㄧˇ）。高佩君（2018）的實驗正好量到：按鍵替換修正若不排除「修正後仍是合法別字」只有 49.2%，排除後 84.0%——**大量鍵入錯誤無法靠合法性偵測，只能靠語言模型**。
5. **漏打/多打聲調或空白**：一聲以空白鍵表示，漏打會使兩音節黏在一起（如 ㄊㄧㄢ ㄑㄧˋ → 被解讀為一個非法或錯誤音節），多打則切斷詞。此類錯誤需要「音節切分」層級的修正（對應 GeneInput 的 pyseg）。
6. **使用者明確提到但「鍵位上不相鄰」的配對**：ㄣ(p)/ㄥ(/)（同欄隔一列）、ㄓ(5)/ㄗ(y)、ㄔ(t)/ㄘ(h)、ㄕ(g)/ㄙ(n)、ㄈ(z)/ㄏ(c)（中隔 ㄌ）、ㄛ(i)/ㄡ(.) —— **都不是相鄰鍵**，因此它們主要是「認知/發音型」錯誤，而非手指滑動（我的推論）。

### 3.3 「認知/發音型」錯誤（有語言學研究支持）
- **ㄣ/ㄥ（前後鼻音）合併**：台灣華語的鼻音韻尾合併在 /in/–/iŋ/、/ən/–/əŋ/ 最明顯，/an/–/aŋ/ 較少；且有地區、性別、世代差異（例：(ing) 的 [in] 變體由南部、男性領先；(eng) 合併變體由北部領先）。來源：NTNU〈The social implications of syllable-final nasal mergers in Taiwan Mandarin〉https://scholar.lib.ntnu.edu.tw/en/publications/the-social-implications-of-syllable-final-nasal-mergers-in-taiwan-3/ ；LabPhon 17 Chiu et al. https://labphon.org/sites/default/files/previous_conferences/LP17/abstracts/LabPhon_17_paper_80.pdf ；公視報導超音波研究 https://news.pts.org.tw/article/766585 （依搜尋摘要）。→ 推論：ㄧㄣ/ㄧㄥ（因/英、心/星、民/名）、ㄣ/ㄥ（真/爭、身/生）是最高風險配對，ㄢ/ㄤ 次之。
- **捲舌/平舌（ㄓㄔㄕ ↔ ㄗㄘㄙ）合併**：歸因於台灣閩南語缺乏捲舌對立；ICPhS 2019 研究 331 位、120 個地區的說話者，都會區合併較少；但 PACLIC 2023 聲學研究發現對立大致仍保留——研究結論不一。來源：https://assta.org/proceedings/ICPhS2019Microsite/pdf/full-paper_958.pdf ；https://tobias-lib.ub.uni-tuebingen.de/xmlui/handle/10900/121302 。
- **ㄈ/ㄏ**：台大碩論（李啟維 2017）的近音規則表列入 ㄈ/ㄏ（例：飛機/灰雞）、ㄋ/ㄌ（泥巴/籬笆）、ㄓ/ㄗ（知識/姿勢）、ㄔ/ㄘ（拆/猜）、ㄧ/ㄩ（夜光/月光）、ㄢ/ㄤ（麵攤/麵湯）、ㄜ/ㄦ（惡人/二人）、ㄣ/ㄥ（出身/出生）；**未找到 ㄈ/ㄏ 的量化研究**（搜尋結果亦如此表示）。
- **聲調**：三聲變調（你好 → 實際讀 ㄋㄧˊ ㄏㄠˇ）、「一」「不」變調、輕聲（的 ㄉㄜ˙）都可能讓使用者依「實際讀音」而非「字典讀音」打聲調——我的推論，未找到統計；設計上應允許「聲調寬容」（tone-insensitive 候選 + 降權）。
- **ㄛ/ㄡ、ㄛ/ㄨㄛ**：未找到研究。我的推論：屬於「標準讀音知識」問題（如 ㄅㄛ 波 vs ㄉㄨㄛ 多、台灣與大陸審音差異），應由詞典多讀音 + LM 處理，而非鍵距模型。

### 3.4 「選字型」錯誤（同音字選錯）與量化證據
- **READIN（ACL 2023，CC BY 4.0）**：三位標註者各用微軟/QQ/搜狗拼音輸入法重打句子、限速 40 字/分、**禁止事後修正**，鍵盤雜訊的字元錯誤率平均 17.4–18.8%（最差 25–27.5%）；錯誤來源：全拼+選錯 39.3%、全拼+打錯 29.8%、簡拼+選錯 16.7%、簡拼+打錯 14.3% → **約 56% 是「選錯候選」、約 44% 是「打錯鍵」**。來源：https://arxiv.org/abs/2302.07324 ；資料 https://github.com/thunlp/READIN 。（簡體/拼音，但這是最接近「真實未修正 IME 雜訊」的公開量測。）
- **CSCD-NS**：母語者（微博）錯誤 >97% 為同音或近音，詞級錯誤 49.4%；的/地/得 仍是 BERT 類模型的難點（見第 1 部分）。
- **聯合報改稿記錄（李啟維 2017）**：專業編輯改最多的是體例/異體（劃→畫、週→周、佈→布、佔→占、份→分、污→汙、臺→台），而〈再, 在〉是「互換比例接近相等」的高頻易混對——說明**繁中的「錯字」很大一部分其實是用字規範/風格**，糾錯器必須可設定（例如是否統一「台/臺」）。
- **Chen & Lee 2000（拼音）**：真實使用者約 4.6% 的字打錯拼音，使轉換錯誤率 ×3；Jia & Zhao 2014 引用的估計：每鍵約 1.5% 錯誤率；2% 錯誤率時不容錯 IME 句正確率約剩 1/4。
- **常見易混字（台灣教學常見，僅為例，未找到頻率統計）**：在/再、的/得/地、做/作、以/已、他/她/它、那/哪、帳/賬、分/份、象/像。→ 這類錯誤**只有上下文 LM 能修**，且多在「已上屏」之後才被發現。
- **注音特有的「忘了切換輸入法」**：在英數模式下打出 "rup wu0 wu0 fu4cl3"（今天天氣好）。旺宏科學獎報告（王冠侖，SA22-160）以 PTT 語料訓練 GRU/LSTM/Transformer/mT5 與 Viterbi，Viterbi 準確率 0.94、BLEU4 88.3，均高於 Google 輸入工具（高中生科展，非同儕審查）。→ 我們的 IME 可在英文模式偵測「看起來像大千鍵序列」的字串並提示轉換。

### 3.5 對錯誤模型的需求（彙整）
| 層級 | 錯誤型態 | 建模方式 | 主要證據 |
|---|---|---|---|
| 按鍵 | 相鄰鍵替換（垂直=同部位、水平=跨部位）、聲調鍵互觸、誤觸 ㄓ、漏/多空白 | 鍵距加權的替換/插入/刪除機率（Chen & Lee 式 HMM 打字模型，個人化） | Chen & Lee 2000；Jia & Zhao 2014；高佩君 2018 |
| 音節 | ㄣ/ㄥ、ㄧㄣ/ㄧㄥ、捲舌/平舌、ㄈ/ㄏ、ㄋ/ㄌ、聲調變調 | 「模糊注音」等價類 + 可學習權重（依使用者口音調整） | 台灣華語語音學研究；TronIn 的 fuzzy 設定 |
| 字/詞 | 同音字選錯（在/再、的/得/地…）、詞級錯 | 上下文 LM（LLM）在同音/近音候選中打分 | READIN（56% 選字錯）；CSCD-NS（>97% 音近） |
| 規範 | 台/臺、劃/畫、異體字 | 使用者可設定的規範表（不屬「錯誤」） | 聯合報改稿記錄 |


---

## 第 4 部分：資料集與語料（CSC / IME / 繁中）

> 「授權」欄：只寫我實際看到的條款；看不到者標「未載明/未能確認」。**沒有任何一個公開資料集是「繁中 + 注音鍵入錯誤」**——這是本專案最大的資料缺口。

| 名稱 | 出處 / 年 | 字體 | 規模 | 錯誤來源 | 授權（已見） | 連結 | 對我們 IME 的用途 |
|---|---|---|---|---|---|---|---|
| SIGHAN 2013 CSC | SIGHAN-7（Wu, Liu, Lee）2013 | 繁體 | 測試 1,000 句/966 錯；訓練 350 句/324 錯（取自 arXiv 1911.10750 的二手統計，未見原文） | **母語者作文**（SIGHAN 2015 概述明載「2013 資料來自母語者作文」） | NTNU 網站提供；2013 條款未逐頁核對 | https://aclanthology.org/W13-4406/ （已核對：Wu, Liu, Lee；SIGHAN-7 pp.35–42）| 唯一「繁體 + 母語者」的經典測試集，可當繁中後處理糾錯的小型評測 |
| SIGHAN 2014 CSC | CLP-2014（Yu et al.）| 繁體 | 未逐頁核對 | 外籍學習者（CFL）作文 | 同上（學術用途）| https://preview.aclanthology.org/fix_video/W14-6820.pdf | 錯誤分佈不像母語者 IME 錯誤，僅作參考 |
| SIGHAN 2015 CSC | SIGHAN-8（Tseng et al.）2015 | 繁體 | 訓練 970 篇作文/3,143 錯；測試 1,100 段（一半無錯）| TOCFL（在台灣舉辦的華語文能力測驗）作文，CFL 學習者 | **僅限學術、不得商用、不得轉散布**（NTNU 下載頁條款）| https://aclanthology.org/W15-3106/ ；https://web.ntnu.edu.tw/~samtseng/NLPTEA_Shared_Tasks_Dataset/sighan8csc.html | 常見 benchmark；多數論文將其轉簡體使用。商用產品不能拿來訓練 |
| Wang271K | EMNLP 2018（Wang et al.）| 簡體 | 271,329 句 / 381,962 錯 | 合成：OCR（形近）+ ASR（音近）| repo 未載明 | https://aclanthology.org/D18-1273/ ；https://github.com/wdimmy/Automatic-Corpus-Generation | BERT 系 CSC 標準訓練料；轉繁後可當預訓練，但非 IME 錯誤 |
| CSCD-NS（CSCD-IME）| ACL 2024（Yong Hu, Fandong Meng, Jie Zhou；arXiv 2022）| 簡體 | 40K 句（30K/5K/5K）| 微博母語者真實錯誤（多為拼音 IME 造成）；詞級 49.4% | repo 為 MIT；資料檔授權未另載明；來源 LCSTS 有其條款（未能確認可否商用）| https://github.com/nghuyong/cscd-ns | **最接近 IME 錯誤**的真實資料；其「IME 模擬造錯」流程是我們造注音資料的範本 |
| LCSTS-IME-2M | 同上 | 簡體 | 約 200 萬句偽資料 | 以 Google IME 模擬拼音打字、取第 2/3 候選 | 同上 | 同上 | 預訓練用偽資料 |
| LEMON | ACL 2023（Wu et al.）| 簡體 | >22,000 例、7,627 編輯對、7 領域 | 自然錯誤 | **僅限學術、不可商用**（repo 聲明）| https://github.com/gingasan/lemon | 跨領域泛化評測 |
| ECSpell | Lv et al.（arXiv 2203.10929）| 簡體 | 法律/醫療/公文：訓練 1,960/3,000/1,728、測試各 500 | 人工構造錯誤 | 未能確認 | https://arxiv.org/abs/2203.10929 | 領域評測 |
| MCSCSet | CIKM 2022（Jiang et al.）| 簡體 | 約 20 萬醫療查詢 | 騰訊醫典真實查詢、醫療專家標註；附醫療混淆集 | 未能確認 | https://arxiv.org/abs/2210.11720 | 領域評測 |
| C2EC benchmark | ACL 2025（Zhou et al.）| 簡體 | CCTC + LEMON 人工核驗 | 替換＋漏字＋多字 | 未能確認 | https://arxiv.org/abs/2502.15266 | 變長錯誤評測 |
| READIN | ACL 2023（Si et al.）| 簡體 | 4 任務（AFQMC、CMRC2018、CSpider、WMT21）的雜訊版 | **真人用微軟/QQ/搜狗拼音重打、禁止修正**；另有方言口音語音 | **CC BY 4.0** | https://arxiv.org/abs/2302.07324 ；https://github.com/thunlp/READIN | 真實 IME 雜訊統計（CER ~17–19%、56% 選字錯）；可直接參考其收集協定做繁中注音版 |
| LREC 2026 Chinese Error Corpus | Zou et al. 2026 | 簡體 | 由 CSCD-IME 句子產生（每句 0–5 錯）| 程式化拼音錯：漏鍵、多鍵、相鄰鍵、兩鍵顛倒；字級錯 | 未能確認（repo 未開啟）| https://github.com/junli-cuny/llm-ime-benchmark （取自論文）| 「拼音鍵入錯誤」產生腳本可改寫成大千注音版 |
| TMMLU+ | Tam et al. 2024（arXiv 2403.01858）| 繁體 | 66 科目 | —（知識評測）| arXiv 頁未載資料授權 | https://huggingface.co/datasets/ikala/tmmluplus | 評估小模型繁中知識；作者發現 tokenizer fertility 與分數強相關 |
| c4-chinese-zhtw | erhwenkuo / jed351（HF）| 繁體 | 2,967,556 列、約 8.66 GB parquet | Common Crawl 2023-14 清洗 | 無明確授權，須遵守 Common Crawl 使用條款 | https://huggingface.co/datasets/erhwenkuo/c4-chinese-zhtw | 繁中 LM 預訓練/蒸餾語料；SlothLM-E 用它 + g2pW 產生「注音↔字」配對 |
| g2pW | INTERSPEECH 2022（玉山金控 + 高師大）| — | 多音字消歧模型，可輸出注音（例：銀行 → ㄧㄣ2 ㄏㄤ2）| — | Apache-2.0（PyPI metadata；LICENSE 檔未開啟）| https://arxiv.org/abs/2203.10430 | **把任意繁中文本自動轉成台灣讀音注音**→ 產生無限「注音→字」訓練對與造錯來源 |
| libchewing 詞庫 | 新酷音 | 繁體 | tsi/word 詞庫 | — | LGPL-2.1-or-later（Arch/openSUSE 套件標示）| https://archlinux.org/packages/extra/x86_64/libchewing/ | 注音詞典與詞頻；LGPL 對閉源產品需留意 |
| McBopomofo 詞庫 | 小麥注音 | 繁體 | — | — | MIT（程式；資料來源見其 textpool 頁，未核對）| https://mcbopomofo.openvanilla.org/ | 注音詞典 + n-gram 基線 |
| 教育部《重編國語辭典修訂本》 | 教育部 | 繁體 | 標準讀音 | — | **未能確認**（搜尋未找到官方授權頁）| — | 台灣標準讀音的權威來源；使用前須確認授權 |
| Common Voice zh-TW | Mozilla | 繁體 | — | 語音轉寫句 | 未逐頁核對（Common Voice 一般為 CC0）| — | McBopomofoLM 以其 1,500 句做注音轉換評測（把句子轉注音再轉回）|
| 聯合報改稿記錄 | 李啟維 2017 碩論 | 繁體 | 數千句測試集 | 專業編輯改稿 | 非公開（論文描述）| https://tdr.lib.ntu.edu.tw/bitstream/123456789/2721/1/ntu-106-1.pdf | 證明繁中「錯字」含大量規範/異體修改 |

**資料策略（我的建議）**：(1) 用 c4-chinese-zhtw / 維基等正確繁中文本 + g2pW 轉注音 → 得到「注音→字」平行資料；(2) 以第 3 部分的注音錯誤模型（鍵距、模糊音、聲調、漏空白）擾動注音、再交給基線 IME（McBopomofo/libchewing）解碼 → 得到「IME 會產生的錯字」偽資料（CSCD-NS 的 IME 模擬法）；(3) 參照 READIN 協定，招募台灣使用者以注音「限速、禁修正」重打句子，做小型真實測試集（數百到數千句即可），這是目前文獻完全沒有的資源。

---

## 第 5 部分：≤3B（及邊界 4B）小模型（截至 2026-10）

> 「繁中能力」欄：只列我看到的數字；多數小模型沒有公開的 TMMLU+ 分數（未能確認）。Tokenizer 效率：**我沒找到任何「繁體中文 chars/token」的公開量測**；唯一可引用的是 Arcee Trinity 技術報告（arXiv 2602.17004）在 C4 中文（簡體為主）上的字/每 token：Llama 3 = 1.29、Qwen 3 = 1.45、GPT-OSS = 1.47、DeepSeek R1 = 1.54、Trinity = 1.38（越高越省）。繁體字在 BPE 詞表中通常比簡體稀有，實際數字可能更低（推論，需實測）。

| 模型 | 參數 | 發布 | 授權 | 架構/詞表 | 繁中/中文證據 | 對 IME 的評估 | 連結 |
|---|---|---|---|---|---|---|---|
| **Qwen3.5-0.8B / 2B**（另有 4B、9B）| 0.8B（含視覺編碼器共 0.9B）/ 2B | 2026-03-02（Qwen GitHub news）| Apache-2.0 | 混合架構：Gated DeltaNet（線性注意力）×3 + Gated Attention ×1 循環；0.8B 為 24 層、d=1024；詞表 248,320；原生 262K context；201 種語言；有 Base 版 | 0.8B C-Eval 46.4（非思考）/ 50.5（思考）；MMMLU 34.1 | **最有潛力的 LM 打分器**：線性注意力狀態大小固定、增量解碼成本低（推論）；McBopomofoLM 的 60M decoder 即採 Qwen3.5 架構。繁中表現與 llama.cpp/MLX 支援度未能確認 | https://huggingface.co/Qwen/Qwen3.5-0.8B ；https://github.com/QwenLM/Qwen3.8 |
| Qwen3-0.6B / 1.7B | 0.6B / 1.7B（1.4B 非 embedding）| 2025（技術報告 arXiv 2505.09388）| Apache-2.0 | 1.7B：28 層、32K context；詞表約 152K | 未找到 TMMLU+；善解作者實測：Qwen3-1.7B（4-bit）重排準確率 87.2% vs 84.4%（p=0.081），**逐候選打分 p95 510–640 ms** | 生態最成熟（MLX/llama.cpp/Core ML 皆常見）；1.7B 在逐鍵路徑太慢，0.6B 可作背景/上屏後校正 | https://huggingface.co/Qwen/Qwen3-1.7B |
| **Gemma 4 E2B**（/E4B）| E2B：2.3B 有效（含 embedding 5.1B）；E4B：4.5B 有效（8B）| 2026-04-02（多個二手來源）| **Apache-2.0**（Gemma 系列首次）| 詞表 262K；128K context；文字+影像+音訊 | MMMLU（IT）E2B 67.4、E4B 76.6；中文支援未在模型卡明列 | 多語能力在同級最強（依 MMMLU），但記憶體/頻寬以 5.1B 計；適合「上屏後整句校正」而非逐鍵 | https://ai.google.dev/gemma/docs/core/model_card_4 ；https://huggingface.co/google/gemma-4-E2B |
| Gemma 3 270M / 1B | 0.3B / 1B | 2025（Gemma 3 報告 2025-03-25；270M 較晚）| Gemma Terms of Use（自訂條款）| 32K context（270M/1B）；訓練含 140+ 語言 | 無繁中數字 | 270M 是官方定位「為特定任務微調」的極小模型（模型卡寫用途為生成/聊天等，未明寫）；可微調成注音條件 LM 但授權較麻煩 | https://huggingface.co/google/gemma-3-270m |
| Gemma 3n E2B / E4B | ~2B（含 PLE ~4.4B）/ ~4B（~6.8B）| 2025-06-26（二手來源）| Gemma Terms of Use | Per-Layer Embeddings | 無繁中數字 | 已被 Gemma 4 E2B（Apache-2.0）取代 | https://docs.api.nvidia.com/nim/reference/google-gemma-3n-e2b-it |
| Llama 3.2 1B / 3B | 1B / 3B（官方命名）| 2024-09-25 | Llama 3.2 Community License | 詞表 128K；C4 中文 1.29 字/token（同 Llama 3）| **官方支援語言不含中文**（8 種：英德法義葡印地西泰）；Breeze 2 論文：Llama-3.2-3B TMMLU+ 35.3；LREC 2026：以 prompt 做拼音→漢字 CER 0.746 | 不建議作為繁中打分器 | https://docs.api.nvidia.com/nim/reference/meta-llama-3_2-3b-instruct |
| MediaTek Breeze 2 3B（Llama-Breeze2-3B-Instruct）| 3B（HF safetensors 顯示 4B，含視覺）| 2025-01-23（論文 arXiv 2501.13921）| Llama 3.2 Community License | 沿用 Llama 3 tokenizer、**未擴充詞表**；有視覺與 function calling | TMMLU+（0-shot）42.7（Llama-3.2-3B 35.3、GPT-4o-mini 38.26、Breeze2-8B 46.6）；天璣 9400 CPU：解碼 3.83 tok/s、prefill 17.07 tok/s、記憶體 6.87 GB | 繁中知識最好的 ≤3B 之一，但 tokenizer 對中文效率低、授權較緊；可當蒸餾教師 | https://arxiv.org/abs/2501.13921 ；https://huggingface.co/MediaTek-Research/Llama-Breeze2-3B-Instruct |
| TAIDE | 8B（Llama-3.1-TAIDE-LX-8B-Chat，2025-06）、12B（Gemma-3-TAIDE-12b-Chat 2025-08；-2602 版 2026-02）| — | TAIDE 自訂社群授權（Llama 3 版；Gemma 3 版未能確認）| — | — | **沒有 ≤3B 版本**；只適合當離線教師/資料生成 | https://huggingface.co/taide |
| Taiwan-ELM 270M / 1.1B | 0.27B / 1.1B | 2024 | Apache-2.0 | 基於 Apple OpenELM；2B 繁中 token + 50 萬指令資料（llama-factory fork）| 未找到 Taiwan-ELM 自身的評測數字 | 少見的「繁中原生極小模型」；可作注音 LM 的初始化候選（tokenizer 細節未能確認）| https://huggingface.co/liswei/Taiwan-ELM-270M-Instruct |
| ChineseErrorCorrector3-4B / chinese-text-correction-1.5b | 4B（Qwen3-4B）/ 1.5B（Qwen2.5-1.5B）| 2025-11 / 2024-10 | Apache-2.0 | 生成式 CSC+CGEC | 見第 1 部分模型短卡（自報 F1；簡體）| 上屏後糾錯 baseline / 蒸餾教師 | https://huggingface.co/twnlp/ChineseErrorCorrector3-4B |
| 社群注音專用極小模型 | SlothLM-E 3.9–11.6M；SlothE-T ~25M + decoder ~60M；善解 M ~14.4M | 2026 | Apache-2.0（各自宣告）| 注音條件、非自回歸或字級 LM | 見第 2 部分 | **逐鍵路徑的實際可行規模**（數十 M、ANE/CPU 上數 ms 級，延遲未公開→需實測）| 見第 2 部分 |

**Tokenizer 對 IME 的具體影響（我的分析）**：
1. IME 打分是「在某位置比較若干候選字」；若候選繁體字在 tokenizer 中是 1 個 token，只需一次前向取 logits；若被拆成 2–3 個 byte token，就要逐 token 展開（成本 ×2–3），且不同候選長度不一，需長度正規化（Zhou et al. 2024 的長度獎勵正是為此）。
2. 多字詞 token（如「我們」為 1 token）會讓「逐字對齊」困難（C-LLM 的核心發現）；IME lattice 以音節為單位，最好用「字級」或「詞典詞級」對齊的打分介面——可在 tokenizer 外加一層 trie，把候選詞映射到 token 序列並快取。
3. **實測建議**：從 c4-chinese-zhtw 抽 10 萬句，量各 tokenizer 的「繁體字/token」與「常用 6,000 繁體字中單 token 覆蓋率」，作為選模型的第一關（TMMLU+ 論文也指出 fertility 與繁中表現強相關）。


---

## 設計啟示

> 本節是我（研究助理）的綜合判斷；每點標出依據的文獻。「論文主張」與「我的推論」分開；延遲數字凡未在 M4 實測者一律標「粗估」。

### A. 一句話結論
**把 LLM 放進「組字期」的 noisy-channel 候選格解碼裡，當「受限的語言模型打分器」（只為候選字/候選路徑打分，不自由生成），並由一個「鍵位感知的注音錯誤模型」提供通道機率；上屏後的 CSC 只作為第二道、以提示（底線）而非自動替換的方式處理跨子句的同音字錯誤。** 不要用 prompt 讓小 LLM 端到端生成轉換結果。

### B. 三種整合方式比較
| 方式 | 能用的證據 | 擅長修的錯 | 延遲 | 過度修正風險 | 文獻證據 |
|---|---|---|---|---|---|
| ① 組字期 lattice 內 noisy-channel 解碼（LM 項含小 LLM）| **原始按鍵 + 注音 + 上文** | 打錯鍵（~44%）、選字錯（~56%）、切分錯 | 每鍵需 <~30 ms（我的目標值）| 低（只在讀音/鍵距鄰域內選）| Chen & Lee 2000（打字模型使錯誤率 −30%）；Jia & Zhao 2014（2% typo 下句正確率 11%→41%）；GeneInput（雜訊 26 鍵 P@1 75.2 vs Google 55.2）；Zhou et al. 2024（0.5–1.8B 純 LM + 失真模型即有效）；MSLLM 2025（小模型 + 0.5B LLM 熵加權）|
| ② 上屏後 CSC（BERT/LLM 改已送出的字）| 只有字（注音需由字反推，按鍵證據已遺失）| 跨子句/長距離同音字（在/再、的/得/地）、詞級錯 | 可放寬到數百 ms–秒（背景或使用者觸發）| 中到高（LLM 會改通順但非錯誤的表達）| CSCD-NS（BERT 65.75 vs GPT-4 54.57）；C-LLM；ReLM；CSRP（效率感知獎勵抑制過改）；LREC 2026 作者自承管線會「讓 LLM 看不到原始注音」|
| ③ 小 LLM 以 prompt 端到端生成 | 注音/拼音字串 | — | 高（TTFT ~0.3–0.4 s 起跳）| 高 | LREC 2026：Llama3.2-3B 拼音→漢字 CER 0.746、拼音修正 CER 1.006；GeneInput：prompt 的 GPT-4 在 FK2C 只有 18.1 |

**哪個在緊延遲預算下最準？** → ①。理由：(a) 錯誤在「按鍵/注音」層就產生，越早用到通道證據越準（Jia & Zhao 的聯合解碼 vs 分段解碼差距巨大）；(b) 在 lattice 中 LLM 只需為 K 條候選路徑的「下一個字」打分，可批次、可快取，計算量遠小於整句生成；(c) 社群實作（McBopomofoLM、善解）與學術（PERT、MSLLM、Zhou et al.）都收斂到「小模型產生/打分 + 大模型在高熵時裁決」。② 仍需要，但只處理 ① 解不了的長距離錯誤，且應保留「注音影子」（見 C-4）讓它仍是 noisy-channel 問題。

### C. 建議架構（四層，延遲預算為我的目標值）
1. **L0 候選格（每鍵，<2 ms，CPU）**：詞典 + n-gram（McBopomofo/libchewing 類）建 lattice；**注音錯誤模型**把每個（可能錯的）音節擴展成少量替代音節（相鄰鍵、模糊音、聲調、切分），附通道對數機率；取 K-best（K≈8–20）。依據：Jia & Zhao（K=20 足夠）、Chen & Lee、TronIn 的模糊注音。
2. **L1 注音條件小模型（每音節，目標 <10 ms，ANE/Core ML）**：10–30M 參數、非自回歸的「注音序列 → 每位置字分佈」encoder（PERT / SlothLM-E / SlothE-T 路線），當發射機率；與 n-gram 轉移以 Viterbi/beam 結合（PERT 的馬可夫框架）。依據：PERT-tiny(1.3M)+bigram 字準 91.3%、PERT-base 96.6%；McBopomofoLM 句準 70.4%→79.1%（自報）。
3. **L2 小 LLM 受限打分（僅在「高熵/前兩名分差小」或使用者停頓時觸發，目標 p95 <50 ms，GPU-MLX 或 ANE-Core ML）**：Qwen3.5-0.8B / Qwen3-0.6B 級、4-bit，只取候選 token 的 logits（selective softmax），上文 KV/狀態快取，K 條路徑批次一次前向；分數融合用 MSLLM 公式：log P_LLM + (1+H_LLM)·(α·log P_通道 + β·log P_L1)，再加 Zhou et al. 的長度獎勵（多 token 字/詞的正規化）；**只在把握度高時才改 L1 的選擇**（McBopomofoLM 的作法）。依據：Zhou et al.（Qwen1.5 超過 1.8B 收益很小）；MSLLM（Qwen2.5-0.5B 已有大部分收益）；善解（1.7B 收益不顯著且 p95 510–640 ms 被否決）。
4. **L3 上屏後校正（背景/使用者觸發，數百 ms 可接受）**：對最近 1–3 句做 CSC；**保留每個已上屏字的「注音影子」（使用者實際按鍵與音節）**，讓校正仍可用通道證據；輸出以底線提示、Tab/點擊接受（iOS 17 式 UI），不靜默替換。模型可用 0.8–2B LLM（MSLLM 式與小模型混合）或蒸餾後的 ReLM 類 ~100M 模型（~14 ms/句於 A100，M4 未實測）。依據：Gboard Proofread 也採「使用者觸發」；CSRP/Zhou et al. 的抑制過改技巧。

### D. 我們需要的注音錯誤模型（規格）
- **結構**：P(按鍵序列 | 意圖音節) × P(意圖音節 | 字)；後者用詞典讀音（含多音字、台灣審音，g2pW 產生訓練資料）；前者分三個子模型相乘/相加：
  1. **按鍵通道**（motor）：大千鍵位幾何的替換（垂直鄰=同發音部位、水平鄰=跨部位）、插入、刪除、聲調鍵互觸（ˇ↔ˋ、ˊ↔˙）、聲調鍵誤觸 ㄓ、漏/多空白（切分）。音節內順序錯誤因槽位組字多半無害，但「槽位覆寫」會產生合法錯音節——必須由 LM 判斷（高佩君 2018 的 49%→84% 落差）。
  2. **音韻通道**（認知/口音）：ㄧㄣ/ㄧㄥ、ㄣ/ㄥ、ㄢ/ㄤ、ㄓㄔㄕ/ㄗㄘㄙ、ㄈ/ㄏ、ㄋ/ㄌ、聲調（三聲變調、一/不變調、輕聲）等價類，每類一個可學權重。
  3. **先驗**：「照打即正確」的機率約 0.96（對照 Zhou et al. 失真模型「相同」= 0.962），每鍵錯誤率初值約 1.5–2%（Jia & Zhao 引用的估計），之後以使用者行為（退格重打、改選候選、接受/拒絕提示）在本機以 EM/計數更新（Chen & Lee 以真實資料訓練 HMM；GeneInput 以「是否選中」當獎勵）。
- **α（LM 權重）個人化**：Chen & Lee 指出 LM 權重可依使用者熟練度調整（不熟練者更多修正）。
- **規範層獨立**：台/臺、劃/畫、異體字屬「規範」，用可設定的對照表，不要讓錯誤模型學成「錯誤」（聯合報改稿記錄的教訓）。

### E. 延遲與資源粗估（未實測，需在 M4 Air 上量測）
- M4 基本款記憶體頻寬約 120 GB/s、Neural Engine 38 TOPS（維基百科/媒體報導，非 Apple 原始規格頁）。Qwen3.5-0.8B 4-bit 權重約 0.4–0.5 GB → 單步前向的記憶體下界約 4 ms；K=8 條路徑各加 1 字可批成一次 8-token 前向，理論上個位數到十幾 ms。**但社群實測（善解，Qwen3-1.7B 4-bit）逐候選 p95 達 510–640 ms**——差距主要來自工程（未快取上文、逐候選重算、未批次），因此「狀態/KV 快取 + 批次 + 只取候選 logits」是成敗關鍵。
- Qwen3.5 的 Gated DeltaNet（24 層中 18 層線性注意力、固定大小狀態）對「沿著候選格分支」特別友善：分支時只需複製固定大小的循環狀態（推論）。已有第三方 Core ML（ANE）與 MLX INT4 移植（https://huggingface.co/leok7v/Qwen3.5-0.8B-coreml ），速度未公開。
- 無風扇 M4 Air：持續 GPU 負載會降頻（一般常識，未查證具體數據）→ L2 只在高熵時觸發、L1 放 ANE、L3 放閒置時段；McBopomofoLM 自報 30 分鐘打字僅耗約 40 秒 CPU、常駐 100–200 MB，可作為資源上限參考。

### F. 評測建議
- 測試集：(1) 繁中「注音重打、禁修正」真實雜訊集（仿 READIN 協定，台灣使用者，含不同口音/年齡）；(2) SIGHAN 2013（繁體母語者，學術用）；(3) 以 g2pW + 注音錯誤模型合成的大型開發集；(4) McBopomofoLM 用的 Common Voice zh-TW 1,500 句轉換測試（可與其自報數字對照）。
- 指標：字錯誤率（CER）、整句正確率、**誤改率（原本對、被改錯）**、使用者需要的退格次數/選字次數、每鍵延遲 p50/p95、能耗。
- 消融：基線 n-gram → +錯誤模型 → +L1 encoder → +L2 LLM（0.6B/0.8B/1.7B/2B 比較）→ +L3；這正好回答 LREC 2026 作者沒做的「管線 vs 端到端」比較。

### G. 未解問題與不確定性
1. **沒有注音鍵入錯誤的公開統計**：所有比例（相鄰鍵 vs 模糊音 vs 選字）都需自行收集；READIN（簡體拼音）的 56%/44% 只能當先驗。
2. **小 LLM 的繁中能力與 tokenizer 效率未量測**：Qwen3.5/Gemma 4 的繁體字單 token 覆蓋率、繁中 TMMLU+ 分數皆未找到。
3. **≥1B LLM 對「句內同音字選擇」的邊際效益可能不大**（善解 p=0.081；Zhou et al. 1.8B 後收益趨緩），真正價值也許在長距離上下文與上屏後校正——需消融驗證。
4. **社群專案數字皆為自報**（McBopomofoLM、SlothLM-E、善解），測試集小、可能有洩漏（SlothLM-E 自己更正過 84–86%→72%）。
5. **授權**：SIGHAN 2015、LEMON 不可商用；libchewing 詞庫 LGPL；教育部辭典授權未確認；Llama 3.2/Breeze 2 授權有限制，Qwen3.5、Gemma 4 為 Apache-2.0 較安全。
6. 中英混打（Topic A 範圍）在注音鍵盤上會與「忘記切換輸入法」錯誤交織（旺宏科展例 "rup wu0 wu0 fu4cl3"），需要與英文模組共同設計偵測。
