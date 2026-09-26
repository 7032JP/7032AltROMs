# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
# ============================================================
# Makefile — 代替 ROM 群のビルド
#
# `make` で配布 ROM 15 本を build/ 配下に生成します。
# ツールチェイン名・出力先・対象 ROM は config.mk 側で設定します。
#
# 内訳は、MIT ライセンス・ソース公開の代替 ROM 12 本 (config.mk の TARGETS) と、
# データ ROM 3 本 (同 DATA_TARGETS) です。
#
#   FM-7 系:
#     boot_bas.rom :   512 B  ($FE00-$FFFF   BASIC モード起動ブート)
#     boot_dos.rom :   512 B  ($FE00-$FFFF   ディスク起動ブート)
#     subsys_c.rom : 10240 B  ($D800-$FFFF   サブシステム。先頭 2KB は ANK フォント)
#   FM77AV 系:
#     initiate.rom :  8192 B  (メイン CPU $6000-$7FFF イニシエート、上位世代向け)
#     initbase.rom :  8192 B  (同上の中位世代 (20 系) 向け変種。機種識別域のみ相違)
#     initav1.rom  :  8192 B  (同上の第 1 世代向け変種。AV ブートイメージ機構を持たない)
#     initex.rom   :  8192 B  (同上の拡張世代 (EX/SX 系) 向け変種。機種識別域のみ相違)
#     subsys_a.rom :  8192 B  (サブ CPU $E000-$FFFF Type-A の位置に置く ROM)
#     subsys_b.rom :  8192 B  (サブ CPU $E000-$FFFF Type-B の位置に置く ROM)
#     subsyscg.rom :  8192 B  (CG/キャラクタジェネレータ、ANK フォントデータ)
#     extsub.rom   : 49152 B  (AV40EX/SX 拡張サブシステム。24KB イメージ x 2 セット)
#   BASIC インタプリタ本体:
#     7tbasic3.rom : 31744 B  (メイン CPU $8000-$FBFF   7T-BASIC 3.1)
#   データ ROM (コードを持たない):
#     kanji.rom    : 131072 B (16x16 漢字フォント 第一水準。4096 枠 x 32 B)
#     kanji2.rom   : 131072 B (16x16 漢字フォント 第二水準。4096 枠 x 32 B)
#     dicrom.rom   : 262144 B (辞書 ROM の枠。全域 0x00。日本語入力は非対応)
# ============================================================

include config.mk

# --- BUILD の外部上書き禁止 (最優先・すべての参照より前) ---
#   出力先・削除先である BUILD を外部 (コマンドライン / 環境変数) から上書きできると、
#   次の危険が生じる:
#     - 値に $(shell ...) を仕込むと、make の変数展開段階で任意コマンドが実行される。
#     - 値にバッククォート・; & | 等を仕込むと、$(BUILD) を使うレシピのシェルで実行される。
#     - 空値なら $(BUILD)/foo が /foo となりルート直下へ出力・削除する。
#     - .git や src / roms 等の既存ディレクトリを指せば、clean が追跡内容を削除し得る。
#   これらはいずれも「BUILD を外部から差し替えられる」ことに起因する。そこで出力先は
#   config.mk (リポジトリ内・信頼できる設定ファイル、origin=file) でのみ設定可とし、
#   コマンドライン・環境変数からの上書きは、最初の参照より前に一律で拒否する。
#   config.mk を編集すれば出力先を変えられる。通常の `make` は
#   BUILD を指定しないため影響を受けない。
#   本ブロックは config.mk の直後・BUILD のどの参照 (target-specific := を含む)
#   よりも前に置く。target-specific := は解釈時に全ターゲット分が展開されるため、
#   ここより後ろに置くと素通りする。
ifneq (,$(filter command line environment,$(origin BUILD)))
  $(error [NG] BUILD はコマンドライン・環境変数から設定できません。出力先を変える場合は config.mk を編集してください。外部からの上書きは、出力先・削除先の乗っ取りを防ぐため拒否します)
endif

.PHONY: all clean verify verify-fonts verify-kanji verify-release deploy-roms \
        devtest-subsys check-build-path check-build-guard version \
        $(ALL_TARGETS)

# デフォルト: 配布 ROM をすべてビルド (MIT セット + データ ROM)
all: $(addprefix $(BUILD)/,$(addsuffix .rom,$(ALL_TARGETS)))

# --- 配布 ROM 差替 (build/ → roms/) + SHA256SUMS ---
#   全再ビルドの産物を roms/ へ写し、SHA256SUMS を作り直す。
ROMS_LIST = $(addsuffix .rom,$(ALL_TARGETS))
#   SHA256SUMS の対象は config.mk の TARGETS / DATA_TARGETS から導いた 15 本に限る
#   (roms/*.rom のワイルドカードだと、置き忘れた余分な .rom まで拾ってしまう)。
#   ここでは複写の成否とサイズを確かめる。クリーンビルドとの一致は verify-release が見る。
deploy-roms: all
	@mkdir -p roms
	@set -e; for r in $(ROMS_LIST); do \
	  cp "$(BUILD)/$$r" "roms/$$r"; \
	  sz=$$(wc -c < "roms/$$r"); \
	  printf "[OK]   %-16s <- %-20s %6s B\n" "roms/$$r" "$(BUILD)/$$r" "$$sz"; \
	done
	@printf '%s\n' $(ROMS_LIST) | LC_ALL=C sort | sed 's|^|roms/|' | xargs sha256sum > SHA256SUMS
	@printf "SHA256SUMS 更新 (%s 本)。roms/ = 最新ビルド産物。\n" "$$(wc -l < SHA256SUMS)"

# --- 再現性検証 (roms/ を書き換えずに一致確認) ---
#   第三者が 1 コマンドで再現性を検証するための非上書きターゲット。
#   クリーンビルド産物 (build/) とチェックイン済み roms/ が同一であることを検査し、
#   さらに SHA256SUMS を照合する。roms/ には一切書き込まない。
#   clean と all を前提条件に並べると `make -jN` で両者が同時に走り、生成途中の
#   ROM が消えて失敗する。レシピ内で $(MAKE) を 2 段に分けて順序を確定させる
#   (中の `all` は -jN の並列で走ってよい)。
verify-release:
	@$(MAKE) clean
	@$(MAKE) all verify-fonts verify-kanji devtest-subsys
	@set -e; for r in $(ROMS_LIST); do \
	  cmp "$(BUILD)/$$r" "roms/$$r" && printf "[OK] %s\n" "$$r"; \
	done
	@sha256sum -c SHA256SUMS

# --- 公開サブシステム試験の組立て・BASIC ローダ再生成照合 ---
#   cases/*.s は、共有領域へのコマンド書き込みと結果の観測を自分で行う 6809 の
#   サンプルソース。各ソースを組み立て、追跡済み cases/*.bas の DATA 列が
#   その機械語と一致することを検査する。生成バイナリは build/ のみに置く。
devtest-subsys:
	@$(PYTHON) $(SCRIPTS)/gen_subsys_tests.py --lwasm $(AS) \
	  --build-dir $(BUILD)/devtest/subsys

# --- フォント生成物のハッシュ照合 + 収録範囲の照合 ---
#   scripts/genfont.py / genfont16.py の生成物について、fonts/SHA256SUMS との照合と、
#   docs/LEGAL.md §8.1 の 2 表との照合 (scripts/check_font_repertoire.py) を行う。
FONT_SUMS = $(FONTS)/SHA256SUMS
FONT_BINS = font.bin font16.bin
verify-fonts: $(BUILD)/font.bin $(BUILD)/font16.bin
	@set -e; for f in $(FONT_BINS); do \
	  exp=$$(grep -E "[[:space:]]\**build/$$f\$$" $(FONT_SUMS) \
	         | grep -oE '^[0-9a-f]{64}' | head -n 1); \
	  if [ -z "$$exp" ]; then \
	    printf "[NG] %s に build/%s の行がありません\n" "$(FONT_SUMS)" "$$f"; exit 1; \
	  fi; \
	  act=$$(sha256sum "$(BUILD)/$$f" | cut -d' ' -f1); \
	  if [ "$$exp" = "$$act" ]; then \
	    printf "[OK] %-14s %s\n" "$$f" "$$act"; \
	  else \
	    printf "[NG] %s: %s=%s 算出=%s\n" "$$f" "$(FONT_SUMS)" "$$exp" "$$act"; exit 1; \
	  fi; \
	done
	@$(PYTHON) $(SCRIPTS)/check_font_repertoire.py docs/LEGAL.md \
	  $(BUILD)/font.bin $(BUILD)/font16.bin

# --- 16x16 漢字フォント ROM のハッシュ照合 + 独自字形の到達検査 ---
#   同梱の入力 BDF (字形の出所) とそのライセンス文は fonts/SHA256SUMS の記載と、
#   scripts/genkanji.py の生成物は SHA256SUMS (配布 ROM 15 本の記載) と突き合わせる。
#   入力側まで照合するのは、字形の出所が記載どおりであること (別のフォントに
#   差し替わっていないこと) を毎回機械的に確かめるためである。
#   続けて scripts/check_kanji_overrides.py が、独自字形を持つ 107 符号位置を
#   機械列挙し、それぞれがどの ROM のどの枠へ到達するか、その枠の 32 byte が
#   差し替え字形表と一致するかを全数で確かめる (docs/LEGAL.md §9.2)。
#   あわせて、同梱 BDF が当該 107 符号位置の字形を持たない改変した BDF であること
#   (scripts/strip_bdf_chars.py の出力であること) も毎回確かめる。
#   文書と生成物が食い違ったまま配布されることを防ぐ (verify-release から毎回必ず呼ばれる)。
verify-kanji: $(BUILD)/kanji.rom $(BUILD)/kanji2.rom
	@set -e; grep -E "[[:space:]]\**$(FONTS)/" $(FONT_SUMS) | sha256sum -c - \
	  || { printf "[NG] %s の記載と同梱フォントの実測が一致しません\n" "$(FONT_SUMS)"; exit 1; }
	@set -e; for f in kanji.rom kanji2.rom; do \
	  exp=$$(grep -E "[[:space:]]\**roms/$$f\$$" SHA256SUMS \
	         | grep -oE '^[0-9a-f]{64}' | head -n 1); \
	  if [ -z "$$exp" ]; then \
	    printf "[NG] SHA256SUMS に roms/%s の行がありません\n" "$$f"; exit 1; \
	  fi; \
	  act=$$(sha256sum "$(BUILD)/$$f" | cut -d' ' -f1); \
	  if [ "$$exp" = "$$act" ]; then \
	    printf "[OK] %-28s %s\n" "build/$$f" "$$act"; \
	  else \
	    printf "[NG] build/%s: SHA256SUMS=%s 算出=%s\n" "$$f" "$$exp" "$$act"; exit 1; \
	  fi; \
	done
	@$(PYTHON) $(SCRIPTS)/check_kanji_overrides.py $(KANJI_BDF) \
	  $(BUILD)/kanji.rom $(BUILD)/kanji2.rom

# --- BUILD パスの安全ガード (削除を伴うターゲットの前段) ---
#   BUILD は config.mk でのみ設定できる (コマンドライン・環境変数からの上書きは
#   冒頭のガードが一律拒否する)。それでも config.mk の設定ミスで危険なパスを
#   指す可能性に備え、削除の前に必ず次を確かめる防御的な第 2 の関門である。
#     空 / "." / ".." / "/" / リポジトリ直下そのもの / その親 / リポジトリ外 /
#     .git を含むパス / 追跡ファイル (を含むディレクトリ)
#   のいずれでもないこと。1 つでも当てはまれば削除せずに失敗する。値はレシピの
#   シェル行へ文字列展開せず環境変数 (BUILD_IN) として渡し、クォートした
#   "$$BUILD_IN" 越しにのみ参照する。まず英数字と . _ + - / 以外の文字を含む値を
#   パス解決より前に拒否する。
check-build-path: export BUILD_IN := $(BUILD)
check-build-path:
	@set -e; \
	b="$$BUILD_IN"; \
	if [ -z "$$b" ]; then echo "[NG] BUILD が空です。削除を中止します。"; exit 1; fi; \
	case "$$b" in \
	  *[!A-Za-z0-9._+/-]*) \
	    echo "[NG] BUILD=\"$$b\" に使えない文字があります (英数字 . _ + - / のみ)。削除を中止します。"; \
	    exit 1;; \
	esac; \
	case "$$b" in \
	  .|..|/) echo "[NG] BUILD=\"$$b\" は削除対象にできません。"; exit 1;; \
	esac; \
	n=$$(basename "$$b"); d=$$(dirname "$$b"); \
	case "$$n" in \
	  .|..) echo "[NG] BUILD の末尾が \"$$n\" です。削除を中止します。"; exit 1;; \
	esac; \
	if [ ! -d "$$d" ]; then \
	  echo "[NG] BUILD の親ディレクトリ \"$$d\" がありません。削除を中止します。"; exit 1; fi; \
	root=$$(pwd -P); parent=$$(dirname "$$root"); \
	abs=$$(cd "$$d" && pwd -P); \
	case "$$abs" in */) abs="$$abs$$n";; *) abs="$$abs/$$n";; esac; \
	if [ "$$abs" = "/" ] || [ "$$abs" = "$$root" ] || [ "$$abs" = "$$parent" ]; then \
	  echo "[NG] BUILD=$$abs はリポジトリ直下またはその親です。削除を中止します。"; exit 1; fi; \
	case "$$abs/" in \
	  "$$root"/*) : ;; \
	  *) echo "[NG] BUILD=$$abs はリポジトリ ($$root) の外です。削除を中止します。"; exit 1;; \
	esac; \
	rel="$${abs#$$root/}"; \
	case "/$$rel/" in \
	  */.git/*|*/.git) echo "[NG] BUILD=$$abs は .git を含みます。削除を中止します。"; exit 1;; \
	esac; \
	if [ -e "$$abs" ] && git -C "$$root" ls-files --error-unmatch -- "$$rel" >/dev/null 2>&1; then \
	  echo "[NG] BUILD=$$abs は追跡対象のファイルです。削除を中止します。"; exit 1; fi; \
	if [ -d "$$abs" ] && [ -n "$$(git -C "$$root" ls-files -- "$$rel" 2>/dev/null | head -n 1)" ]; then \
	  echo "[NG] BUILD=$$abs は追跡ファイルを含むディレクトリです。削除を中止します。"; exit 1; fi; \
	printf "[OK] BUILD=%s (削除してよいパス)\n" "$$abs"

# 個別ターゲット (make boot_bas など)
boot_bas: $(BUILD)/boot_bas.rom
boot_dos: $(BUILD)/boot_dos.rom
subsys_c: $(BUILD)/subsys_c.rom
initiate: $(BUILD)/initiate.rom
initbase: $(BUILD)/initbase.rom
initav1: $(BUILD)/initav1.rom
initex: $(BUILD)/initex.rom
subsys_a: $(BUILD)/subsys_a.rom
subsys_b: $(BUILD)/subsys_b.rom
subsyscg: $(BUILD)/subsyscg.rom
extsub: $(BUILD)/extsub.rom
7tbasic3: $(BUILD)/7tbasic3.rom
kanji: $(BUILD)/kanji.rom
kanji2: $(BUILD)/kanji2.rom
dicrom: $(BUILD)/dicrom.rom

# --- FM-7: boot_bas.rom (BASIC モード起動ブート) ---
$(BUILD)/boot_bas.rom: $(SRC)/boot_bas/boot.s | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/boot_bas.lst $<

# --- FM-7: boot_dos.rom (ディスク起動ブート) ---
$(BUILD)/boot_dos.rom: $(SRC)/boot_dos/boot.s | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/boot_dos.lst $<

# --- FM-7: subsys_c.rom (サブシステム ROM。先頭にフォントを INCLUDEBIN) ---
#   $D800-$DFFF はフォントデータ専用 (font.bin 全体 2KB)。実行コードを
#   この範囲へ置いてはならない (subsys.s 冒頭の設計制約コメント参照)。
$(BUILD)/subsys_c.rom: $(SRC)/subsys_c/subsys.s $(BUILD)/font.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/subsys_c.lst -I$(BUILD) $(SRC)/subsys_c/subsys.s

# --- FM77AV: boot_init.rom (initiate に埋め込む resident boot イメージ、512 B) ---
#   $FE00 基底の resident boot イメージ。ディスク起動時に FDC サービスを供給する。
#   単独では配布せず、initiate.rom へ INCLUDEBIN で取り込む中間生成物。
$(BUILD)/boot_init.rom: $(SRC)/boot_init/boot.s | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/boot_init.lst $<

# --- FM77AV: avboot.bin (initiate に埋め込む AV ブートイメージ、640 B) ---
#   RAM 上の AVBOOT_ORG (本プロジェクトが選んだ位置) 基底の AV ブートイメージ。起動モード判定 + メディア存在プリフライト +
#   BASIC フォールバック + FDC サービス表を供給する中間生成物。
$(BUILD)/avboot.bin: $(SRC)/initiate/avboot.s | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/avboot.lst $(SRC)/initiate/avboot.s

# --- FM77AV: initiate.rom (メイン CPU イニシエート ROM) ---
#   boot_bas.rom / boot_init.rom / avboot.bin (中間生成物) を INCLUDEBIN で
#   取り込むため依存に含める。
$(BUILD)/initiate.rom: $(SRC)/initiate/initiate.s $(BUILD)/boot_bas.rom $(BUILD)/boot_init.rom $(BUILD)/avboot.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/initiate.lst -I$(BUILD) $(SRC)/initiate/initiate.s

# --- FM77AV (中位世代/20 系): initbase.rom (イニシエート ROM の機種別変種) ---
#   src/initiate/initiate.s を INIT_BASE 条件でアセンブルした変種。機種識別域
#   (file offset $0B09-$0B13) の提示内容だけが initiate.rom と異なる。
#   高色数モードを持たない構成へは本 ROM を initiate.rom として配置する。
$(BUILD)/initbase.rom: $(SRC)/initbase/initbase.s $(SRC)/initiate/initiate.s $(BUILD)/boot_bas.rom $(BUILD)/boot_init.rom $(BUILD)/avboot.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/initbase.lst -I$(BUILD) $(SRC)/initbase/initbase.s

# --- FM77AV (第 1 世代): initav1.rom (イニシエート ROM の機種別変種) ---
#   src/initiate/initiate.s を INIT_AV1 条件でアセンブルした変種。AV ブートイメージ
#   (file offset $1C00) の機構をそもそも持たない世代の構成で、
#   起動用 ROM イメージを resident boot 領域へ複写してその場で実行する。
#   avboot.bin を取り込まないため依存に含めない。
$(BUILD)/initav1.rom: $(SRC)/initav1/initav1.s $(SRC)/initiate/initiate.s $(BUILD)/boot_bas.rom $(BUILD)/boot_init.rom | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/initav1.lst -I$(BUILD) $(SRC)/initav1/initav1.s

# --- FM77AV (拡張世代/EX・SX 系): initex.rom (イニシエート ROM の機種別変種) ---
#   src/initiate/initiate.s を INIT_EXSX 条件でアセンブルした変種。機種識別域の
#   世代名だけが initiate.rom と異なる。8192 B のままであり 16 KB 化は行わない
#   (上位半分はアクセスされないため利得が無い)。
$(BUILD)/initex.rom: $(SRC)/initex/initex.s $(SRC)/initiate/initiate.s $(BUILD)/boot_bas.rom $(BUILD)/boot_init.rom $(BUILD)/avboot.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/initex.lst -I$(BUILD) $(SRC)/initex/initex.s

# --- FM77AV: subsys_a.rom (Type-A の位置に置くサブシステム ROM) ---
#   subsys_c/subsys.s と共用のコマンド処理本体を SUBSYS_AV 条件で AV 向けに
#   アセンブルする ($E000-$FFFF のみ放出。フォントは CG バンク領域から供給されるため非内蔵)。
$(BUILD)/subsys_a.rom: $(SRC)/subsys_a/subsys_a.s $(SRC)/subsys_c/subsys.s $(BUILD)/font.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/subsys_a.lst -I$(BUILD) $(SRC)/subsys_a/subsys_a.s

# --- FM77AV: subsys_b.rom (Type-B の位置に置くサブシステム ROM) ---
#   subsys_a.rom と同じ共用ソースを SUBSYS_AV と SUBSYS_TB の条件でアセンブルする。
$(BUILD)/subsys_b.rom: $(SRC)/subsys_b/subsys_b.s $(SRC)/subsys_c/subsys.s $(BUILD)/font.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/subsys_b.lst -I$(BUILD) $(SRC)/subsys_b/subsys_b.s

# --- FM77AV: subsyscg.rom (CG ROM。ANK フォントを 4 バンク分 INCLUDEBIN) ---
$(BUILD)/subsyscg.rom: $(SRC)/subsyscg/subsyscg.s $(BUILD)/font.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/subsyscg.lst -I$(BUILD) $(SRC)/subsyscg/subsyscg.s

# --- FM77AV40EX/SX: extsub.rom (拡張サブシステム ROM、49152 B) ---
#   24KB の拡張サブシステムイメージ 2 セット (CGRAM 16KB + 実行ページ 8KB) の枠。
#   本バージョンはフォント部 (8x16 ANK / 8x8 ANK) のみを実装し、実行ページに相当する
#   領域はゼロ埋めのプレースホルダ。所定のサイズを供給する枠で、サイズ 49152 B は
#   verify で検査する。
$(BUILD)/extsub.rom: $(SRC)/extsub/extsub.s $(BUILD)/font.bin $(BUILD)/font16.bin | $(BUILD)
	$(AS) -fraw -o $@ -l$(BUILD)/extsub.lst -I$(BUILD) $(SRC)/extsub/extsub.s

# --- 7T-BASIC 3.1: 7tbasic3.rom (BASIC インタプリタ本体) ---
#   src/7tbasic3/ から独立実装をアセンブルする。raw 32768 B ($8000-$FFFF) を
#   配布サイズ 31744 B ($8000-$FBFF) へ切詰めて配布 ROM とする。
#   ($FC00-$FFFF は共有 RAM/I/O/ブート ROM/ベクタがオーバーレイし
#    ROM としては参照されないため末尾を切詰める)
7TBASIC3_SRCS = $(wildcard $(SRC)/7tbasic3/common/*.inc $(SRC)/7tbasic3/common/*.s \
                           $(SRC)/7tbasic3/7t31/*.inc $(SRC)/7tbasic3/7t31/*.s)
$(BUILD)/7tbasic3.rom: $(7TBASIC3_SRCS) | $(BUILD)
	$(AS) -fraw -I$(SRC)/7tbasic3/common -I$(SRC)/7tbasic3/7t31 -o $(BUILD)/7tbasic3.raw -l$(BUILD)/7tbasic3.lst $(SRC)/7tbasic3/7t31/7tbasic3.s
	head -c 31744 $(BUILD)/7tbasic3.raw > $@

# --- 8x8 ANK フォント (subsys_c / subsyscg / extsub に組込まれる、2048 B) ---
$(BUILD)/font.bin: $(SCRIPTS)/genfont.py | $(BUILD)
	$(PYTHON) $< $@

# --- 8x16 ANK フォント (extsub に組込まれる、4096 B) ---
#   8x8 フォント (font.bin) を縦 2 倍に引き伸ばして生成する派生物。
$(BUILD)/font16.bin: $(SCRIPTS)/genfont16.py $(BUILD)/font.bin | $(BUILD)
	$(PYTHON) $(SCRIPTS)/genfont16.py $(BUILD)/font.bin $@

# --- 16x16 漢字フォント ROM (kanji.rom / kanji2.rom、各 131072 B) ---
#   同梱の 16 ドット BDF (fonts/。パブリックドメイン宣言つきの第三者フォント)
#   の字形を、本プロジェクトの配置規則で 4096 枠へ並べ替えて 2 本同時に生成する。
#   1〜8 区の 107 符号位置だけは、本プロジェクトが書き起こした独自字形
#   (scripts/kanji_glyph_overrides.py) を組み立て段で用いる。
#   字形の出所・権利区分・配置規則は docs/LEGAL.md §9 を参照。
#   1 回の実行で 2 本を書き出すため、kanji2.rom 側は kanji.rom に従属させる
#   (make -jN で同じスクリプトが二重に走らないようにする)。
$(BUILD)/kanji.rom: $(SCRIPTS)/genkanji.py $(SCRIPTS)/kanji_glyph_overrides.py \
                    $(KANJI_BDF) | $(BUILD)
	$(PYTHON) $(SCRIPTS)/genkanji.py $(KANJI_BDF) \
	  --out-l1 $(BUILD)/kanji.rom --out-l2 $(BUILD)/kanji2.rom

$(BUILD)/kanji2.rom: $(BUILD)/kanji.rom
	@test -f $@

# --- 辞書 ROM の枠 (dicrom.rom、262144 B) ---
#   拡張世代 (EX/SX 系) 向けの所定のサイズを供給する枠。
#   全域 0x00 で、かな漢字変換 (日本語入力) の機能は実装していない
#   (位置づけは extsub.rom と同じ。docs/COMPATIBILITY.md)。
$(BUILD)/dicrom.rom: $(SCRIPTS)/gendicrom.py | $(BUILD)
	$(PYTHON) $(SCRIPTS)/gendicrom.py $@

$(BUILD):
	mkdir -p $@

# --- サイズ検証 (GNU stat -c / BSD stat -f 両対応) ---
verify: all
	@$(foreach t,$(ALL_TARGETS),$(call check_size,$(BUILD)/$(t).rom,$(SIZE_$(t)));)

# check_size,<rom>,<expected-size>
define check_size
sz=$$(stat -c %s "$(1)" 2>/dev/null || stat -f%z "$(1)"); \
if [ "$$sz" = "$(2)" ]; then \
  printf "[OK] %-28s %6d B\n" "$(1)" "$$sz"; \
else \
  printf "[NG] %-28s %6d B (expected %s)\n" "$(1)" "$$sz" "$(2)"; exit 1; \
fi
endef

# --- バージョンの表示 (配布物のバージョンと BASIC のバージョンは別採番である旨を毎回そえる) ---
#   配布物のバージョンは config.mk の DIST_VERSION が唯一の情報源。
#   7T-BASIC のバージョンは BASIC インタプリタ本体が持つバージョンで、DIST_VERSION とは連動しない。
version:
	@printf "配布物のバージョン  : v%s  (タグ名の想定: v%s)\n" "$(DIST_VERSION)" "$(DIST_VERSION)"
	@printf "7T-BASIC のバージョン : 3.1 (BASIC インタプリタ本体のバージョン。配布物のバージョンとは別の採番)\n"

# build/ をまるごと削除 (削除先は check-build-path で必ず検査する)
clean: check-build-path
	rm -rf "$(BUILD)"

#==============================================================================
# check-build-guard: BUILD の外部上書きが常に拒否されることを常設の負例で確かめる。
#
#   BUILD は config.mk でのみ設定でき、コマンドライン・環境変数からの上書きは
#   展開前ガードが一律で拒否する。危険値 (make 関数構文・シェルメタ文字・空値・
#   .git 等の追跡ディレクトリ・安全に見える普通の値) を、コマンドライン経路と
#   `make -e` の環境変数経路の双方で与えても、必ず終了値 != 0 で拒否され、かつ
#   コマンドが実行されない (マーカー非生成) ことを検査する。マーカーは build/ 配下に置く。
#==============================================================================
check-build-guard:
	@set -e; \
	self="$(MAKE)"; mk="Makefile"; ok=0; ng=0; \
	mdir="$(CURDIR)/build"; mkdir -p "$$mdir"; mk_marker="$$mdir/.guard_marker"; \
	payloads='x`touch_MARKER`x x;touch_MARKER x|touch_MARKER x&touch_MARKER x$$(shell_touch_MARKER)x .git src build2 EMPTY'; \
	for p in $$payloads; do \
	  if [ "$$p" = EMPTY ]; then v=""; else \
	    v=$$(printf '%s' "$$p" | sed "s#MARKER#$$mk_marker#g; s#touch_#touch #g; s#shell_touch#shell touch#g"); fi; \
	  rm -f "$$mk_marker"; \
	  if $$self -C "$(CURDIR)" -f "$$mk" all BUILD="$$v" >/dev/null 2>&1; then \
	    echo "[NG] BUILD=\"$$v\" (CLI) が拒否されませんでした"; ng=$$((ng+1)); \
	  elif [ -e "$$mk_marker" ]; then \
	    echo "[NG] BUILD=\"$$v\" (CLI) でコマンドが実行されました"; ng=$$((ng+1)); \
	  else echo "[OK] BUILD 外部上書き (CLI) を拒否・非実行: $$p"; ok=$$((ok+1)); fi; \
	  rm -f "$$mk_marker"; \
	  if BUILD="$$v" $$self -e -C "$(CURDIR)" -f "$$mk" all >/dev/null 2>&1; then \
	    echo "[NG] BUILD=\"$$v\" (env/-e) が拒否されませんでした"; ng=$$((ng+1)); \
	  elif [ -e "$$mk_marker" ]; then \
	    echo "[NG] BUILD=\"$$v\" (env/-e) でコマンドが実行されました"; ng=$$((ng+1)); \
	  else echo "[OK] BUILD 外部上書き (env/-e) を拒否・非実行: $$p"; ok=$$((ok+1)); fi; \
	done; \
	rm -f "$$mk_marker"; \
	if $$self -n -C "$(CURDIR)" -f "$$mk" all >/dev/null 2>&1; then \
	  echo "[OK] BUILD 未指定 (config.mk の既定) は受理"; ok=$$((ok+1)); \
	else echo "[NG] BUILD 未指定でガードが誤作動しました"; ng=$$((ng+1)); fi; \
	echo "check-build-guard: [OK] $$ok / [NG] $$ng"; \
	test "$$ng" -eq 0
