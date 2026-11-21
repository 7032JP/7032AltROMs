# COMPATIBILITY — 実機との違い

この ROM 一式は、参考書籍（[LEGAL.md](LEGAL.md) §6.1）を基に、起動・画面表示・キー入力・カセット読込と F-BASIC の標準的な文法・動作を実装しています。実機（機種付属の ROM を載せた FM-7 / FM77AV）と違う点を、影響の大きい順に §1 にまとめます。機能ごとの対応表は [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) にあります。

## 1. 制限事項の一覧

| # | 項目 | 実機での動き | この ROM での動き | 影響と回避策 |
| --: | --- | --- | --- | --- |
| 1 | BASIC プログラムの形式 | 中間コード形式とアスキー形式を読み書きします | アスキー形式だけです。中間コード形式は `Bad Data In File` です | **市販ソフトの多くを含む既存のテープは中間コード（バイナリ）形式で保存されており、この BASIC では読み込めません。** 回避策: 機種付属の ROM が必要です（入手と取り扱いは [LEGAL.md §1](LEGAL.md#1-ライセンスと利用者の責任) のとおり、ご自身の責任で、権利上適切な方法で）。自作のプログラムの取り込み方は §3 |
| 2 | ROM 内のアドレスの直接呼出し | ROM の内部のルーチンや表も使えます | 使えるのは [BUILD.md](BUILD.md) §5.2 の固定エントリポイントだけです | ROM の内部を直接使うソフトは動きません。回避策: 機種付属の ROM が必要です（入手と取り扱いは [LEGAL.md §1](LEGAL.md#1-ライセンスと利用者の責任) のとおり、ご自身の責任で、権利上適切な方法で） |
| 3 | ディスクのファイル入出力 | `OPEN` などでデータファイルを読み書きします | ディスクを指す `OPEN` は `Device Unavailable` です。`NAME` / `DSKINI` / `DSKF` もありません | データファイルを使うプログラムは動きません。プログラムの読込み・保存、機械語の読込み、一覧とディスク起動は使えます |
| 4 | かな漢字変換・拡張サブシステム | 日本語入力などを使えます | 辞書 ROM と拡張サブシステム ROM は枠だけです | 日本語入力はできません。拡張サブシステムを使うソフトは動きません |
| 5 | FM77AV 系の画面モード | 640×400・26 万色・4096 色を使えます | 640×400 と 26 万色は未対応、4096 色は一部のコマンドだけです。BASIC の文は 8 色の画面だけです | これらの画面モードに頼るソフトは正しく動きません |
| 6 | 周辺装置 | プリンタ・RS-232C・タッチペン・バブルカセットを使えます | 文は受理して何もしないか、`Device Unavailable` です | 印字・通信・ペン入力はできません |
| 7 | 日付 | — | この ROM では `DATE$` は自動で進みません | — |
| 8 | 細部 | — | ジッタの大きいテープイメージは読み誤ることがあります。倍精度の超越関数は単精度で計算します。文字列リテラルの代入でも文字列領域を使います | 文字列の多いプログラムは `CLEAR n` で文字列領域を広げます（既定 300 バイト） |

## 2. 対象機種

FM-7 と FM77AV 系列（FM77AV 基本機 / AV20 / AV20EX / AV40 / AV40EX / AV40SX）が対象です。AV でない FM-77 系は対象に含めません。どの機種構成でも起動と BASIC は動きます。FM77AV40 / AV40EX / AV40SX の 26 万色モードは未対応です（[COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §4）。

## 3. BASIC プログラムの取り込み方

`SAVE` / `LOAD` / `MERGE` が扱うのはアスキー形式（`SAVE ,A` と同じ形式）だけです（[BUILD.md](BUILD.md) §5.3）。

1. 自作のプログラムは、実機で `SAVE "名前",A` で保存し直し、お使いの実行環境が対応するテープイメージにすれば読み込めます。
2. リストを打ち直し、この ROM の `SAVE` で保存することもできます。

機械語ファイルは、この ROM の `SAVEM` と `LOADM` が対になる形式です。

## 4. 詳しい対応表

| 知りたいこと | 文書 |
| --- | --- |
| 機種の仕組み（起動・サブシステム・画面モード）ごとの対応 | [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §1 |
| BASIC の機能ごとの対応 | [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §2 |
| 予約語ごとの対応 | [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §3、[BASIC_REFERENCE.md](BASIC_REFERENCE.md) |
| 機種構成ごとの対応 | [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §4 |
| データ ROM とフォントの収録内容 | [COMPATIBILITY_DETAILS.md](COMPATIBILITY_DETAILS.md) §5 |
