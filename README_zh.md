# `ascend_prl` — 全球首个昇腾 NPU 加密货币矿工

[English](README.md) | **中文**

一个从零编写、面向 [Pearl](https://arxiv.org/abs/2504.09971) **有用工作量证明（Proof-of-**Useful**-Work）** 币的矿工，
目標平台為華為昇騰 910B 與 910C。

> **免责声明：** 仅供学习研究之用。请仅在你拥有授权的硬件上运行。

此版由 Cube 執行 INT8 矩陣乘法，CPU 完成 Pearl 後處理。請從此原始碼重新編譯。

## 快速开始

1. **创建 Pearl 钱包。** 使用[官方钱包](https://github.com/pearl-research-labs/pearl/releases/tag/pearl-wallet-v2.0.0)
   并按其说明操作。你会得到一个形如下面的地址：
   `prl1p2skcz8kxn03p3j2hzaz4j687ewan8deju7lgvpswux9hkgavcz5s6v5p83`。
2. **选择矿池。** 任何标准 `stratum` 协议的 Pearl 矿池都应可用。本项目在
   [Kryptex](https://pool.kryptex.com/prl) 上测试。注意：不同矿池接受的 M、N、K 形状不同（见[性能与形状](#性能与形状)）。
3. **編譯並執行此原始碼版本。** 依[編譯](#编译)步驟建置，再執行
   `ASCEND_RT_VISIBLE_DEVICES=0 WALLET=prl1... POOL=pool.host PORT=7000 ./scripts/launch.sh`。

## 与其他 NPU 任务共存

可以让矿工与其他进程跑在同一批芯片上，并自动「让路」：当有其他进程进入该设备时，
矿工会自动停止，无需手动重启。在启动时设置 **`PRL_COEXIST_GUARD=1`** 即可启用：

```bash
PRL_COEXIST_GUARD=1 ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 ./scripts/launch.sh
# Docker：在 run 命令中加上  -e PRL_COEXIST_GUARD=1 （scripts/run_container.sh 会自动透传）
```

## 编译

依赖：
- CANN 9.0.0 工具链 + Ascend-C 编译器
- 支持 `-march=armv8-a+crypto` 的 `gcc`（用于 NEON 版 BLAKE3）
- `cmake ≥ 3.16`
- BLAKE3 的 C 源码（`blake3-1.x/c` 目录 —— 已随仓库附带，或取自 cargo registry）
- cargo，以及用于 proof FFI 的 Pearl Rust 工作区（`pearl-blake3`、`zk-pow`）——
  请在 `proof-ffi/Cargo.toml` 中把路径改成你本地的检出目录。

```bash
make                     # 固定 rank=128、K=4096；生成全部正式版构建产物
```

编译选项（`make 变量=...`）：

| 选项 | 默认 | 含义 |
| ---- | ------- | ------- |
| `MDIM` | `16384` | 16384 可让矿池的区块证明（block-proof）足够快，避免产生孤块（orphan）。 |

## 性能与形状

固定 rank=128、K=4096。每個 strip 有 128 列 A，每個 B band 有 64 欄。
Cube 以 `[band][fold][B 欄][A 列]` 的順序輸出 32 組 INT32 分段乘積。
同一條 stream 依序執行 Cube 與 D2H。預設 `MDIM=16384` 每個 strip 回傳
256 MiB，完整無命中掃描回傳 32 GiB。`PIPE_DEPTH=4` 的輸出緩衝占用
1 GiB Host pinned memory 與 256 MiB Device GM。可用 `make PIPE_DEPTH=2`
同時重編 kernel 與 miner，將輸出 pinned memory 降至 512 MiB。

## GZIP v2 + 自定义份额难度（Kryptex）

通过 Pearl stratum gzip 协议，提交份额的网络流量可降低约 **80–100 倍**
（[协议说明](https://gist.github.com/maxmalysh/eaaf4332dbc5ca99d0a78f24a733fffe)）：

1. `mining.authorize` 发送 `"type":"v2"`。矿池在应答中回显 `"type":"v2"` 后，矿工提交
   `base64(gzip(bincode(plain_proof)))`，而不是约 136 KB 的明文 base64 证明（压缩后约 15 KB）。
2. 矩阵 A/B **始终零填充**（仅前 8 字节写入 seed 戳），证明可被 gzip 高效压缩。
   旧版随机填充与对应环境变量开关已彻底移除。

**自定义份额难度**（[说明](https://gist.github.com/maxmalysh/f4c29f532e650a114b317e234f1393fb)）：
把 CLI 第 6 个参数（password）设为 `d=<N>`。例如高算力矿机目标约每 30 秒一个份额：

```bash
./ascend_prl_kryptex 0 worker pool.kryptex.com 7000 prl1...你的钱包 'd=349245965'
```

password 为空 → 使用矿池默认（`d=2097152` ≈ 9000 TH）。公式：
`d = 算力_H/s × 目标份额间隔秒数 / 4_294_967_296`。

| 环境变量 | 含义 |
| --- | --- |
| `PRL_NOGZIP` | 强制 v1（不发 `"type":"v2"`，明文证明） |
| `PRL_GZIP` | 不等 authorize 应答就假定启用 gzip |
| `PRL_GZIP_FIELD` | 覆盖 submit 字段名（默认仍为 `plain_proof`） |

## 矿池兼容性

目前已测试过的矿池：

| 矿池 | 备注 |
| --- | --- |
| `kryptex` | 推荐；支持 gzip v2 + 自定义 `d=` 难度 |

只要是讲标准 stratum 协议的新矿池，大概率走 `kryptex` 路径就能用；如果不行，请附上一段抓包
开一个 issue，通常只需要对握手/解析做一点小调整。我会按需添加矿池支持。

## 贡献

欢迎提交 PR / 提需求（例如支持某个非标准协议的矿池）！

目前矿工仅在 910B4 上测试过。如果你愿意把设备共享给我，我可以适配到其他 NPU。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。按**「现状」提供，不作任何担保**。

<!-- discovery: 昇腾 910B / Ascend 910B4 NPU Pearl 挖矿 miner; 用闲置 NPU 挖 Pearl (PRL) 币;
     world-first Ascend NPU cryptocurrency miner; Huawei Atlas 800/300 idle NPU mining. -->

### Host CPU 优化

- 固定 zero-seed：原始 A/B 以 `calloc` 初始化，此后每轮只覆盖前 8 bytes seed；
  A noise 直接从零基底生成，最后以 int8 wrap 补回 seed stamp。原始矩阵仍完整保留给
  Merkle hash 和 proof，因此这里只消除 noise 阶段的 A 全量读取。
  内部 `*_reuse` API 要求 buffer 初次清零、后续只由生成器写入；普通 API 每次先清零。
- BLAKE3：首次大 hash 懒初始化持久 pthread pool，之后复用；无空闲 worker 时在调用线程
  计算，不阻塞等待可用 worker。prep 的 A/B root 顺序提交，同一个 hash 可使用整个 prep
  额度。OpenMP 与 BLAKE3 是先后执行的阶段。正常退出时先排空 prep/proof，再关闭 pool。
- reuse-B：每代 B 的首次 proof 懒建立 B Merkle tree，之后只提取需要的 multileaf proof。
  cache 只保留一棵 B tree，会增加常驻内存；A tree 仍逐次建立。job / PRL_BREUSE 更新 B
  前先排空 proof、清除 cache、递增 generation，退出时也清除。cache 同时核对 pointer、
  generation、key、n、k；非 reuse-B 路径不启用 cache。

`PRL_CPU_THREADS` 受程序 CPU affinity 限制。`PRL_PREP_THREADS` 設定原始準備端額度，
由矩陣準備與 OpenMP PoW 共用。`PRL_POW_THREADS` 調整 PoW 份額，準備階段至少保留
一條執行緒。`PRL_PROOF_THREADS` 控制獨立 proof 額度。總額度最多兩條執行緒時，
準備、PoW 與 proof 分階段執行。

直接运行二进制时，建议在启动前设置 `OMP_WAIT_POLICY=PASSIVE GOMP_SPINCOUNT=0`；
`launch.sh` 已自动设置，避免空闲 OpenMP worker 自旋。miner 内也设置这两个变量，但
部分 OpenMP runtime 可能在 main 前读取环境，故启动环境最可靠。

相同 seed/key/dimension 的零填充数据、noise 公式和 proof 序列化格式保持不变。
ARM64 正式路径固定使用 rank-128 NEON TBL；可移植的非 ARM64 路径也仅支持 rank 128。
不再提供旧版随机填充，也不再有 rank 运行参数或编译选项。

## CPU transcript 與 PoW

CPU 對每個分段乘積做 32 位元累加，逐 fold 將 64 個 B 欄做 XOR reduction。
在每組 64 列 A 內，再把間隔 32 的兩列配對，將結果寫入
`transcript[fold % 16] = rotl32(word, 13) ^ folded`。
每個 tile 的 16 個字以 little-endian 組成 64 bytes，交給 CPU 做 keyed BLAKE3，
再以 little-endian 256-bit 比對 target。命中時回傳 `hi * nbands + band`，
供原有 proof 路徑解碼。多個候選命中時沿用原掃描優先順序。

Cube→Vector GM ring、producer/consumer flags 與 device key/target 上傳已移除。

## A 噪聲 BLAKE3 4-way NEON（ARM64）

`src/prep.c` 的 A 行噪聲產生（`compute_an128_fused_stamp`、
`compute_an128_fused_raw`）固定為 rank=128；每列原本依序計算 4 個
64-byte Keyed BLAKE3 digest。現在透過 `src/prl_blake3_neon4.h` 的
4 個 32-bit NEON lanes 同時計算 4 個獨立 BLAKE3 root。

- 訊息位元組不變：`LE32(draw+1) || 28*0 || seed[32]`。
- 7 rounds、KEYED_HASH/CHUNK_START/CHUNK_END/ROOT flags、輸出前 32 bytes 不變。
- `noise_digest_store`、rank-128 TBL、A seed stamp、原始 A/commit hash、B 路徑不變。
- 非 aarch64 平台保留原有可攜式 A 路徑；沒有新增執行期開關。
- 本變更不代表整體礦工 TH/s 實測提升；效益須在目標 910B 主機量測。
