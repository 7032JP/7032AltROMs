# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
# ============================================================
# config.mk — ビルド設定 (= 唯一の設定ファイル)
#
# ここの値を書き換えれば Makefile 側の参照が追従します。
#
# ただし BUILD だけは例外です。ソースの一部が中間生成物 (ANK フォント等) を
# `INCLUDEBIN "../build/..."` と既定の出力先の名前で直接参照しているため、
# BUILD を既定の `build` 以外へ変えたビルドはその参照を解決できません。
# 再現性 (make verify-release のバイト一致) を保証するのは既定値のままの
# ビルドです。出力先を分けたいときはリポジトリごと複製してください。
# ============================================================

# 6809 アセンブラ (ツールチェイン)
#   推奨: lwtools の lwasm。
AS     = lwasm

# フォント生成等のビルド補助に使う Python 3
PYTHON = python3

# 出力先ディレクトリ (.gitignore 済み。make で再生成)
BUILD  = build

# ソースルート
SRC    = src

# フォント生成スクリプト
SCRIPTS = scripts

# 配布物 (本リポジトリ一式) のバージョン
#   **BASIC インタプリタ本体のバージョン (7T-BASIC 3.1) とは別の採番である。**
#   配布物のバージョンは独立して採番する。
#   7T-BASIC のバージョンは 3.1 のままで、こことは連動しない (src/7tbasic3/ 側が持つ)。
#   公開時のタグ名は "v$(DIST_VERSION)" を想定する。
DIST_VERSION = 1.0.1

# 第三者のビットマップフォント (パブリックドメイン宣言つき) の同梱先
#   16x16 漢字フォント ROM の字形の入力に用います (docs/LEGAL.md §9)。
FONTS = fonts

# ビルド対象の代替 ROM 一覧 (すべて MIT ライセンス・ソース公開)
#   FM-7 系:
#     boot_bas : BASIC モード起動ブート ROM           (512 B)
#     boot_dos : ディスク起動ブート ROM               (512 B)
#     subsys_c : サブシステム ROM                     (10240 B)
#   FM77AV 系:
#     initiate : メイン CPU イニシエート ROM (上位世代) (8192 B)
#     initbase : 同上の中位世代 (20 系) 向け変種       (8192 B)
#     initav1  : 同上の第 1 世代向け変種               (8192 B)
#     initex   : 同上の拡張世代 (EX/SX 系) 向け変種    (8192 B)
#     subsys_a : サブシステム ROM (Type-A の位置)       (8192 B)
#     subsys_b : サブシステム ROM (Type-B の位置。subsys_a と同じソースを Type-B 向け条件で組む。描画先が異なりバイト列は異なる) (8192 B)
#     subsyscg : CG (キャラクタジェネレータ) ROM      (8192 B)
#     extsub   : 拡張サブシステム ROM (AV40EX/SX 向け) (49152 B)
#   BASIC インタプリタ本体:
#     7tbasic3 : 7T-BASIC 3.1 (BASIC インタプリタ本体) (31744 B)
TARGETS = boot_bas boot_dos subsys_c initiate initbase initav1 initex subsys_a subsys_b subsyscg extsub 7tbasic3

# データ ROM 一覧 (コードを持たないデータ ROM。上の TARGETS とは由来の区分が違う)
#   kanji  : 16x16 漢字フォント ROM 第一水準 (131072 B)
#   kanji2 : 16x16 漢字フォント ROM 第二水準 (131072 B)
#            上の 2 本は、パブリックドメイン宣言つきで配布されている第三者の
#            ビットマップフォント (fonts/) の字形を、本プロジェクトの配置規則で
#            並べ替えたものです。字形の出所と権利区分は docs/LEGAL.md §9 に
#            記載します (配置規則の実装と生成スクリプトは MIT)。
#   dicrom : 辞書 ROM (262144 B)。所定サイズを供給する枠 (全域 0x00)。
#            かな漢字変換 (日本語入力) は非対応です (docs/COMPATIBILITY.md)。
DATA_TARGETS = kanji kanji2 dicrom

# 配布 ROM の全一覧 (roms/ へ集約し SHA256SUMS の対象となるもの)
ALL_TARGETS = $(TARGETS) $(DATA_TARGETS)

# 各 ROM の出力サイズ (byte)
SIZE_boot_bas = 512
SIZE_boot_dos = 512
SIZE_subsys_c = 10240
SIZE_initiate = 8192
SIZE_initbase = 8192
SIZE_initav1  = 8192
SIZE_initex   = 8192
SIZE_subsys_a = 8192
SIZE_subsys_b = 8192
SIZE_subsyscg = 8192
SIZE_extsub   = 49152
SIZE_7tbasic3 = 31744
SIZE_kanji    = 131072
SIZE_kanji2   = 131072
SIZE_dicrom   = 262144

# 16x16 漢字フォント ROM の字形入力 (同梱の BDF。docs/LEGAL.md §9)
KANJI_BDF = $(FONTS)/shinonome/shnmk16.bdf
