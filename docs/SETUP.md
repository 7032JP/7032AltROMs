# SETUP — 収録 ROM と置き方

収録している ROM 15 本がそれぞれ何をするものかと、手元の FM-7 / FM77AV 系エミュレーターへ置いて使う手順です。ブラウザで試す手順も §3 にあります。何がどこまで動くかは [COMPATIBILITY.md](COMPATIBILITY.md)、BASIC の書き方は [BASIC_REFERENCE.md](BASIC_REFERENCE.md) にあります。

## 1. 収録 ROM 15 本の一覧

| ROM | 何をするか | 対象機 | サイズ | 置くときの名前 |
| --- | --- | --- | ---: | --- |
| 7tbasic3 | **BASIC の本体**（7T-BASIC 3.1）。コマンド・関数を解釈して実行し、カセット入出力を行い、表示や入力はサブ CPU へ頼む | FM-7 / FM77AV 系 | 31744 B | `fbasic30.rom` |
| boot_bas | 電源 ON 直後に動き、**BASIC を立ち上げる** | FM-7 系／FM77AV 系（FM-7 互換モード） | 512 B | `boot_bas.rom` |
| boot_dos | 電源 ON 直後に動き、**ディスクから起動する** | FM-7 系／FM77AV 系（FM-7 互換モード） | 512 B | `boot_dos.rom` |
| subsys_c | サブ CPU 側で**画面表示・キー入力・図形描画**を受け持つ | FM-7 系／FM77AV 系（FM-7 互換モード） | 10240 B | `subsys_c.rom` |
| initiate | 電源 ON 直後に各部を初期化し、**BASIC 起動かディスク起動かを振り分ける** | FM77AV40 | 8192 B | `initiate.rom` |
| initbase | 同上。26 万色モードを備えない構成向けの変種 | FM77AV20 | 8192 B | `initiate.rom` |
| initav1 | 同上。起動イメージの仕組みを持たない世代向けの変種 | FM77AV | 8192 B | `initiate.rom` |
| initex | 同上。拡張世代（EX / SX 系）向けの変種（機種識別域の世代名が異なる） | FM77AV20EX / AV40EX / AV40SX | 8192 B | `initiate.rom` |
| subsys_a | FM77AV 系のサブ CPU 側で、**FM-7 系と同じ表示・キー入力・図形描画のやりとり**を受け持つ（Type-A の位置） | FM77AV 系 | 8192 B | `subsys_a.rom` |
| subsys_b | 同上（Type-B の位置。320×200／4096 色のアナログモードで描く点が subsys_a と異なる） | FM77AV 系 | 8192 B | `subsys_b.rom` |
| subsyscg | 画面に出す**8x8 の英数字・片仮名フォント**を供給する | FM77AV 系 | 8192 B | `subsyscg.rom` |
| extsub | 拡張サブシステムの**所定サイズと ANK フォントを供給する枠**（機能部は未対応） | FM77AV40EX / AV40SX | 49152 B | `extsub.rom` |
| kanji | 漢字を表示するソフトが読み出す**16x16 漢字フォント（第一水準）** | FM-7 系（拡張）／FM77AV 系 | 131072 B | `kanji.rom`（環境により `kanji1.rom` / `kanjia.rom` も） |
| kanji2 | 同上（**第二水準**） | FM77AV40EX / AV40SX、日本語カード相当 | 131072 B | `kanji2.rom` |
| dicrom | 辞書 ROM の**所定サイズを供給する枠**（全域 `0x00`・かな漢字変換は未対応） | FM77AV40EX / AV40SX | 262144 B | `dicrom.rom` |

- ビルド済みのファイルは [../roms/](../roms/) にあります。
- `initiate` / `initbase` / `initav1` / `initex` は同じ枠の機種別変種で、置くのはいずれか 1 本だけです（§2.5）。1 つの環境に置くのは最大 12 本です。
- 上から 12 本（`7tbasic3` 〜 `extsub`）がアセンブラソースを持つ代替 ROM で、ソースは [../src/](../src/) にあります（[BUILD.md](BUILD.md) §4）。データ ROM 3 本（`kanji` 〜 `dicrom`）にアセンブラソースはなく、[../scripts/](../scripts/) のスクリプトが生成します。

## 2. 各 ROM の説明

### 2.1 7tbasic3（7T-BASIC 3.1）

メイン CPU 側で動く BASIC インタプリタ本体です。コマンドと関数の解釈実行、カセット入出力、ディスクの読込と保存を行い、表示・入力・図形描画はサブ CPU へ指示します。

- 置くときのファイル名は `fbasic30.rom` です（§4 の ※1）。
- 使える予約語と F-BASIC との違いは [BASIC_REFERENCE.md](BASIC_REFERENCE.md) にあります。

### 2.2 boot_bas / boot_dos

電源 ON 直後にメイン CPU 側で動く 512 B のブート ROM です。`boot_bas` は BASIC モードでの起動、`boot_dos` はディスク起動を担います。

### 2.3 subsys_c

サブ CPU 側で、表示・キー入力・図形描画を受け持つ互換 ROM です。メイン CPU とは共有 RAM のコマンド域を介してやり取りします。コマンドの一覧は [BUILD.md](BUILD.md) §6.2 にあります。先頭 2KB は 8x8 ANK フォントです。

### 2.4 subsys_a / subsys_b / subsyscg

FM77AV 系のサブ CPU 側で使う 3 本です。Type-A / Type-B の位置に置く 2 本は、どちらも FM-7 系と同じコマンドの授受を担います。

- **subsys_a**（Type-A の位置）— FM-7 系と同じ表示・キー入力・図形描画のコマンドを受け持ちます。コマンド処理の本体は subsys_c と共通です。
- **subsys_b**（Type-B の位置）— コマンド処理の本体は subsys_a・subsys_c と共通ですが、画面は 320×200／4096 色のアナログモード（1 画素の色を B・R・G 各 4 ビットで扱う 12 面）で描きます。色の指定形式が異なるコマンドがあり、バイト列は subsys_a と同じではありません（[COMPATIBILITY.md](COMPATIBILITY.md) §1.1）。
- **subsyscg** — 8x8 ANK フォントを 4 バンク分に置いて供給します。リセットベクタは持ちません。フォントは subsys_c と同じものです（[LEGAL.md](LEGAL.md) §8）。

コマンド番号は [BUILD.md](BUILD.md) §5.4 にあります。

### 2.5 initiate / initbase / initav1 / initex（イニシエート ROM とその変種）

FM77AV 系のメイン CPU 側で、電源 ON / リセット直後に最初に実行されます。サウンド・アナログパレット・メモリマッピングレジスタ（MMR）を初期化し、BASIC 起動かディスク起動かを判定して制御を移します。ディスク起動時に使う resident boot イメージを含みます。収録している 4 本は同じ枠の機種別の作り分けです。

| ROM ファイル | AV ブートイメージ | 起動対象ドライブ | 使う機種 |
| --- | --- | --- | --- |
| `initiate.rom` | あり | 0 → 1 → 2 → 3 | FM77AV40（26 万色モードを備える） |
| `initbase.rom` | あり | 0 → 1 → 2 → 3 | FM77AV20（26 万色モードを備えない） |
| `initav1.rom` | なし | **0 のみ** | FM77AV（第 1 世代の基本機） |
| `initex.rom` | あり | 0 → 1 → 2 → 3 | FM77AV20EX / FM77AV40EX / FM77AV40SX |

変種の違い:

1. **機種識別域** — ソフトウェアが利用できる画面モードを判定するために読む 20 バイトの領域です。内容は本プロジェクト独自です（[BUILD.md](BUILD.md) §5.5）。
2. **起動機構** — 第 1 世代は起動用イメージを resident boot 領域へ複写してその場で実行します。中位・上位世代が使う AV ブートイメージの仕組みは持ちません。
3. **起動対象ドライブ** — 第 1 世代のディスク起動はドライブ 0 だけを対象とします。ドライブ 1 以降からの起動は中位世代（AV20 系）以降の起動仕様です。

イメージ内の配置（機種識別域・音色表・AV ブートイメージの位置）は [BUILD.md](BUILD.md) §5.5 にあります。

#### 構成に合わない変種を置くと

- 備えていない画面モードをソフトウェアに提示してしまい、そのモードを選んだ時点で表示が崩れることがあります。
- 起動対象ドライブが機種と食い違います。特に、**基本機（第 1 世代）へ `initbase.rom` を置かないでください。** 名前は似ていますが `initbase.rom` は AV20 系向けで、ドライブ 1 以降からの起動という後の世代の起動仕様を備えます。

#### 機種世代ごとの起動対象ドライブ

| 機種世代 | 置く起動 ROM | 起動対象ドライブ | ドライブ 1 以降に起動ディスクを入れたとき |
| --- | --- | :---: | --- |
| FM-7 系（BASIC モード） | `boot_bas.rom` | 0 のみ | 起動しません（BASIC が立ち上がります） |
| FM-7 系（ディスクモード） | `boot_dos.rom` | 0 のみ | 起動しません |
| FM77AV（第 1 世代の基本機） | `initav1.rom` | **0 のみ** | 起動しません（BASIC が立ち上がります） |
| FM77AV20 | `initbase.rom` | 0 → 1 → 2 → 3 | 起動します |
| FM77AV40 | `initiate.rom` | 0 → 1 → 2 → 3 | 起動します |
| FM77AV20EX / AV40EX / AV40SX | `initex.rom` | 0 → 1 → 2 → 3 | 起動します |

BASIC モードで起動できるディスクが無ければ BASIC へ移ります。FM-7 のディスクモードは、起動セクタを読めるまで待ち続けます。FM77AV20 以降のドライブ探索では、全ドライブから起動できなければモーターを止めて BASIC へ移ります。

### 2.6 extsub

FM77AV40EX / FM77AV40SX の構成で使う拡張サブシステム ROM です。この構成では、拡張サブシステムの ROM イメージが所定の長さ（49152 B）で存在しないと初期化が成立しません。収録しているのはフォント部（8x8 と 8x16 の ANK フォント）だけで、拡張サブシステムのプログラム本体は未対応です。イメージ内の配置は [BUILD.md](BUILD.md) §5.6 にあります。

### 2.7 kanji / kanji2（16x16 漢字フォント ROM）

漢字を表示するソフトウェアが読み出す 16x16 のビットマップフォントです。1 グリフ 32 B で、131072 B に 4096 枠を持ちます。`kanji` が JIS 第一水準、`kanji2` が第二水準で、構造とサイズは同じです。収録している文字・字形の由来・空白になる範囲は [LEGAL.md](LEGAL.md) §9 にあります。置くときの名前は環境によって違います（§4 の ※3〜※5）。

### 2.8 dicrom（辞書 ROM の枠）

FM77AV40EX / FM77AV40SX の構成は、所定の長さ（262144 B）の辞書 ROM ファイルがあることを前提に初期化を進めます。本 ROM はその所定サイズを供給する枠で、全域 `0x00` です。変換辞書の内容を持たず、**かな漢字変換（日本語入力）は未対応です。**

## 3. ブラウザで試す（インストール不要）

実機も手元のエミュレーターも要りません。ブラウザで動く実行環境が同じ互換 ROM セットを同梱しているので、ROM ファイルを用意しなくても動きます。

1. ブラウザで動く実行環境 [WebM7](https://7032jp.github.io/WebM7/) を開きます。同じ互換 ROM セットを同梱しているので、開くだけで BASIC が動きます。
2. 画面にタイトル 2 行（1 行目は青い帯の上の `7T-BASIC version 3.1`、2 行目は著作権行）と空きバイト数の行に続いて `Ready` が出れば BASIC が動いています。打ち込んで動く例は §6 にあります。
3. この配布物の ROM（別の世代）を試すときは、[../roms/](../roms/) の ROM ファイルを手元に置き（GitHub の「Code」→「Download ZIP」でリポジトリをまるごと取得して展開します。Git を使える方は clone でもかまいません）、ROM Files パネルで対象の行を「自分の ROM」にして指定します。まず試すなら機種は FM-7 が簡単で、使うのは `7tbasic3.rom` / `boot_bas.rom` / `boot_dos.rom` / `subsys_c.rom` の 4 ファイルです。ファイル名の対応（`7tbasic3.rom` を `fbasic30.rom` として指定する等）と、FM77AV 系で要るファイルは §4 の表と同じです。

## 4. 機種別コピー表

手元のエミュレーターに置くときは、次の順に進めます。

1. [../roms/](../roms/) の `.rom` ファイルを入手します。リポジトリをまるごと clone してもかまいません。
2. 下の表で、お使いの機種の列を上から下へたどります。機種が分からないときは、エミュレーターの設定画面で選んでいる機種名を見てください。
3. エミュレーターの ROM 配置先（ROM ファイルを読み込むフォルダ）へ、表のとおりの名前でコピーします。**一部のファイルは、コピーするときにファイル名を変えます。ここが一番間違えやすいところです。** 漢字 ROM は、同じファイルを複数の名前でコピーすることがあります。配置先の場所と配置名は環境ごとに違うので、お使いの環境のマニュアルもあわせて見てください。

ROM ファイルだけを他の方へ渡すときも、`LICENSE` / `LICENSE-MIT.md` / `LICENSE-FONT.md` を一緒に渡してください。 / When passing ROM files on to someone else, include `LICENSE`, `LICENSE-MIT.md`, and `LICENSE-FONT.md` with them.

- 表の**行**は、この配布物に入っている収録ファイル（`roms/` のファイル名）です。
- 表の**列**は機種です。お使いの機種の列を、上から下へ縦に見てください。
- **セル**（太字）は、コピーして置くときのファイル名です。行と違う名前が書かれていたら、その名前に変えて置きます。名前が 2 つ以上書かれていたら、同じファイルをその数だけコピーして、それぞれの名前で置きます。**—** はその機種では置きません。

| 収録ファイル（roms/） | FM-7 系 | FM77AV | FM77AV20 | FM77AV40 | FM77AV20EX | FM77AV40EX・FM77AV40SX |
| --- | --- | --- | --- | --- | --- | --- |
| 7tbasic3.rom | **fbasic30.rom**（※1） | **fbasic30.rom**（※1） | **fbasic30.rom**（※1） | **fbasic30.rom**（※1） | **fbasic30.rom**（※1） | **fbasic30.rom**（※1） |
| boot_bas.rom | **boot_bas.rom** | **boot_bas.rom** | **boot_bas.rom** | **boot_bas.rom** | **boot_bas.rom** | **boot_bas.rom** |
| boot_dos.rom | **boot_dos.rom** | **boot_dos.rom** | **boot_dos.rom** | **boot_dos.rom** | **boot_dos.rom** | **boot_dos.rom** |
| subsys_c.rom | **subsys_c.rom** | **subsys_c.rom** | **subsys_c.rom** | **subsys_c.rom** | **subsys_c.rom** | **subsys_c.rom** |
| initiate.rom（※2） | — | — | — | **initiate.rom** | — | — |
| initbase.rom（※2） | — | — | **initiate.rom** | — | — | — |
| initav1.rom（※2） | — | **initiate.rom** | — | — | — | — |
| initex.rom（※2） | — | — | — | — | **initiate.rom** | **initiate.rom** |
| subsys_a.rom | — | **subsys_a.rom** | **subsys_a.rom** | **subsys_a.rom** | **subsys_a.rom** | **subsys_a.rom** |
| subsys_b.rom | — | **subsys_b.rom** | **subsys_b.rom** | **subsys_b.rom** | **subsys_b.rom** | **subsys_b.rom** |
| subsyscg.rom | — | **subsyscg.rom** | **subsyscg.rom** | **subsyscg.rom** | **subsyscg.rom** | **subsyscg.rom** |
| extsub.rom | — | — | — | — | — | **extsub.rom** |
| kanji.rom | **kanji.rom**（※3・※4） | **kanji.rom**<br>**kanji1.rom**<br>**kanjia.rom**（※3） | **kanji.rom**<br>**kanji1.rom**<br>**kanjia.rom**（※3） | **kanji.rom**<br>**kanji1.rom**<br>**kanjia.rom**（※3） | **kanji.rom**<br>**kanji1.rom**<br>**kanjia.rom**（※3） | **kanji.rom**<br>**kanji1.rom**<br>**kanjia.rom**（※3） |
| kanji2.rom | — | **kanji2.rom**（※5） | **kanji2.rom**（※5） | **kanji2.rom**（※5） | **kanji2.rom**（※5） | **kanji2.rom** |
| dicrom.rom | — | — | — | — | — | **dicrom.rom** |

- **※1 多くのエミュレーターは、BASIC ROM を `fbasic30.rom` というファイル名で読みます。** `7tbasic3.rom` をコピーして `fbasic30.rom` に名前を変えて置いてください。別の名前を使う環境では、その環境のマニュアルに書かれた BASIC ROM のファイル名にします。名前が合っていないと BASIC が立ち上がりません。
- **※2 `initiate.rom` / `initbase.rom` / `initav1.rom` / `initex.rom` は同じ枠の機種別の作り分けです。** お使いの機種の列で **initiate.rom** と書かれている行の 1 本だけをコピーし、`initiate.rom` に名前を変えて置きます。違いは §2.5 にあります。
- **※3 漢字 ROM（第一水準）は、環境によって読みにいくファイル名が違います。** `kanji.rom` のほかに `kanji1.rom` / `kanjia.rom` の名前で読む環境があるので、同じ `kanji.rom` を 3 つの名前でコピーして置くのが確実です。
- **※4 FM-7 系で漢字 ROM は拡張（漢字 ROM カード相当）の位置づけ**です。備えない構成では置きません。置く場合は、環境側の設定で漢字 ROM を有効にする必要があることがあります。FM77AV 系では通常この設定によらず第一水準を読めます。
- **※5 第二水準（`kanji2.rom`）は FM77AV40EX / FM77AV40SX の構成で置きます。** それ以外の機種では、日本語カード相当を備える構成でのみ使われます。
- **※6 FM77AV 系の列でも `boot_bas.rom` / `boot_dos.rom` / `subsys_c.rom` を置きます。** FM77AV 系は FM-7 互換モードでこのファイルを使います。

収録している 15 本のうち、1 つの環境で使うのは種類として最大 12 本です（イニシエート ROM の変種は 1 本だけ置きます。漢字 ROM の別名コピーは数えません）。

### 4.1 データ ROM 3 本を置く機種構成

| 機種構成 | kanji.rom | kanji2.rom | dicrom.rom |
| --- | :---: | :---: | :---: |
| FM-7 系（漢字 ROM を備えない構成） | — | — | — |
| FM-7 系（漢字 ROM カード相当の拡張を備える構成） | 置く | — | — |
| FM77AV / FM77AV20 / FM77AV40 / FM77AV20EX | 置く | 日本語カード相当の構成でのみ置く | — |
| FM77AV40EX / FM77AV40SX | 置く | 置く | 置く |

## 5. コピーの例（`ROMDIR` に配置先を入れて実行）

各コードブロックの先頭で、`ROMDIR` にお使いの環境の ROM 配置先を入れてから実行してください。書き換えるのはその 1 行だけで、あとはそのまま貼り付けて実行できます。漢字 ROM の行は ※3〜※5 に従います。

**FM-7 系**

```sh
ROMDIR=/path/to/rom                          # ← お使いの環境の ROM 配置先に書き換える

cp roms/7tbasic3.rom "$ROMDIR/fbasic30.rom"  # ← ファイル名を変えて置く (※1)
cp roms/boot_bas.rom "$ROMDIR/boot_bas.rom"
cp roms/boot_dos.rom "$ROMDIR/boot_dos.rom"
cp roms/subsys_c.rom "$ROMDIR/subsys_c.rom"
cp roms/kanji.rom    "$ROMDIR/kanji.rom"     # 漢字 ROM カード相当を使う場合のみ (※4)
```

**FM77AV 系**（上の 4 ファイルに続けて。※6）

イニシエート ROM は、機種に合わせて次の表から 1 本を選びます。**配置名はどれを選んでも `initiate.rom` です。**

| 機種 | コピーするファイル |
| --- | --- |
| FM77AV（基本機・第 1 世代） | `roms/initav1.rom` |
| FM77AV20 | `roms/initbase.rom` |
| FM77AV40 | `roms/initiate.rom` |
| FM77AV20EX / AV40EX / AV40SX | `roms/initex.rom` |

```sh
ROMDIR=/path/to/rom                          # ← お使いの環境の ROM 配置先に書き換える
INITROM=roms/initiate.rom                    # ← 上の表から機種に合う 1 本に書き換える (※2)

cp "$INITROM"        "$ROMDIR/initiate.rom"  # ← 配置名はどれを選んでも initiate.rom (※2)
cp roms/subsys_a.rom "$ROMDIR/subsys_a.rom"
cp roms/subsys_b.rom "$ROMDIR/subsys_b.rom"
cp roms/subsyscg.rom "$ROMDIR/subsyscg.rom"
cp roms/kanji.rom    "$ROMDIR/kanji.rom"
cp roms/kanji.rom    "$ROMDIR/kanji1.rom"    # ← 同じ内容を別の配置名でも配置 (※3)
cp roms/kanji.rom    "$ROMDIR/kanjia.rom"    # ← 同じ内容を別の配置名でも配置 (※3)
cp roms/kanji2.rom   "$ROMDIR/kanji2.rom"    # ← 第二水準を読む構成のみ (※5)
```

**FM77AV40EX / FM77AV40SX** では、さらに次の 2 ファイルを足します。

```sh
ROMDIR=/path/to/rom                          # ← お使いの環境の ROM 配置先に書き換える

cp roms/extsub.rom   "$ROMDIR/extsub.rom"
cp roms/dicrom.rom   "$ROMDIR/dicrom.rom"
```

## 6. 起動してみる

- BASIC モードで電源を入れると、起動できるディスクが無ければ BASIC が立ち上がり、画面に `Ready` が出ます。
- ディスクから起動したいときは、ドライブ 0 に起動ディスクを入れてから電源を入れます。起動できないときの動作は機種と起動モードで異なります（§2.5）。
- ドライブ 1 以降のディスクから起動できるかどうかは機種によって違います（§2.5）。

うまく立ち上がらないときの確認点:

1. BASIC ROM の配置名が、お使いの環境が読む名前（多くは `fbasic30.rom`。※1）になっているか。
2. `initiate.rom` に、機種に合った 1 本をコピーしたか（※2）。
3. コピー先のフォルダが、環境の ROM 配置先と合っているか。

ROM ファイルを差し替えられない環境では、この方法は使えません。

### 6.1 打ち込んで動く例

行番号なしで打てばその場で実行し、行番号を付けて打てばプログラムとして覚えます。

**画面に文字を出す**

```basic
10 FOR I=1 TO 10
20 PRINT I;" FM-7"
30 NEXT I
RUN
```

**図形を描く**

```basic
10 SCREEN 7,7
20 CLS
30 FOR R=10 TO 90 STEP 10
40 CIRCLE (320,100),R,7
50 NEXT R
60 LINE (0,0)-(639,199),PSET,7,B
RUN
```

**音を鳴らす**

```basic
10 PLAY "T120O4L4CDEFGAB","O3L2CEG"
RUN
```

- 打ち間違えた行は、同じ行番号で打ち直せば置き換わります。行番号だけを打って改行すると、その行は消えます。
- `LIST` で今のプログラムを表示し、`NEW` で全部消し、`SAVE` で保存し、`LOAD` で読み直します。
- 雑誌や書籍に載っている FM-7 用の BASIC プログラムも、対応している文法の範囲で打ち込んで動かせます。使えない予約語と保存形式の違いは [COMPATIBILITY.md](COMPATIBILITY.md) にあります。

## 7. 用語

| 用語 | 意味 |
| --- | --- |
| 配置先 | エミュレーターが ROM ファイルを読み込むフォルダ。場所は環境ごとに違います。§5 のコピー例では `ROMDIR` に入れて使います。 |
| 配置名 | 配置先に置くときのファイル名。エミュレーターはこの名前でファイルを探します。 |
| 変種 | 同じ役割の ROM を、機種の世代に合わせて作り分けたもの。置くのはその中の 1 本だけです。 |
| ドライブ 0 | 1 台目のフロッピーディスクドライブ。以降 1・2・3 と数えます。 |
| BASIC インタプリタ | BASIC で書いたプログラムを、1 行ずつ読んで実行するプログラム。この配布物では `7tbasic3.rom` がそれです。 |
| メイン CPU | 本体の中心となる演算装置。BASIC を実行する側です。 |
| サブ CPU | 画面表示やキー入力を担当する、もう 1 つの演算装置。 |
| サブシステム | サブ CPU 側の仕組みと、そこで動く ROM のまとまり。 |
| イニシエート ROM | FM77AV 系で、電源を入れた直後に最初に動く ROM。各部を初期化し、BASIC 起動かディスク起動かを振り分けます。 |
| ブート ROM | FM-7 系で、電源を入れた直後の起動を担う小さな ROM。 |
| CG（キャラクタジェネレータ） | 文字の形（フォント）を供給する仕組み。 |
| ANK | 英字・数字・片仮名などの 1 バイト文字の文字種のこと。記号や罫線パーツも含みます。 |
| 第一水準・第二水準 | JIS が定めた漢字の 2 つのまとまり。よく使う漢字が第一水準、それ以外が第二水準です。 |
| 枠（ROM の枠） | 所定のサイズだけを用意して、中身の機能は持たせていないファイルのこと。漢字フォント ROM の中で字形 1 文字ぶんを置く場所も「枠」と呼びます（[LEGAL.md](LEGAL.md) §11）。 |
| resident boot | ディスク起動のときに、ディスクの先頭を読み込んで実行するための常駐部分。 |
| 機種識別域 | ソフトウェアが機種構成を見分けるために読む、ROM 内の短いデータ領域。 |
| 共有領域 | メイン CPU とサブ CPU が、コマンドとその答えをやりとりするために共有するメモリ。 |
