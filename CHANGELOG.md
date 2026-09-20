# 更新履歴

## v1.0.0 — 2026-09-20

**独立実装の代替 ROM 一式（15 本）を収録しています**

- FM-7 / FM77AV 系のエミュレーター向けの独立実装です（実装の性質は [docs/LEGAL.md](docs/LEGAL.md)）。
- 収録は、起動・表示・グラフィック等を担う土台 ROM 11 本、BASIC インタプリタ本体 `7tbasic3.rom`（7T-BASIC 3.1）、データ ROM 3 本です。データ ROM は 16x16 漢字フォント ROM 2 本と辞書 ROM の枠 1 本で、かな漢字変換（日本語入力）は未対応です。一覧と役割、置き方は [docs/SETUP.md](docs/SETUP.md) にあります。
- BASIC プログラムは予約語の省略形を展開したアスキーテキストとして格納し、保存はアスキー形式（`SAVE ,A` と同じ形式。[docs/BUILD.md](docs/BUILD.md) §5.3）だけです。中間コード（バイナリ）形式で保存された BASIC プログラムは読み込めません。取込み方法は [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) にあります。
- サポートする文法は [docs/BASIC_REFERENCE.md](docs/BASIC_REFERENCE.md) にあります。
- アセンブラソースを持つ代替 ROM 12 本は MIT ライセンス・ソース公開です。ビルド済みバイナリを [roms/](roms/) に、ソースコードを [src/](src/) に同梱しています。漢字フォントの字形の出所と権利区分は [docs/LEGAL.md](docs/LEGAL.md) §9 と [LICENSE-FONT.md](LICENSE-FONT.md) にあります。
