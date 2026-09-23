#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Naomitsu.Tsugiiwa
# ============================================================
# copy_roms.sh — ビルド済みの配布 ROM をローカルの roms/ へ用意する
#
# 役割:
#   build/ に生成済みの配布 ROM 一式 (MIT ライセンスの代替 ROM 12 本 +
#   データ ROM 3 本) を、リポジトリ直下の roms/ へコピーして、検証・配置用に
#   一か所へ集約します。
#
#   対象 ROM の一覧と期待サイズは、このスクリプト内には持ちません。
#   ビルド設定 config.mk の TARGETS / DATA_TARGETS / SIZE_<name> を唯一の
#   情報源として読み取ります。ROM を追加・削除するときに直すのは config.mk
#   だけです (一覧を二重に持つと片方だけ古くなる事故が起きるため)。
#
# 検査:
#   1. 各 ROM が build/ に存在すること。
#   2. コピー後のサイズが config.mk の SIZE_<name> と一致すること。
#   3. build 産物と roms/ がバイト一致すること。
#   4. コピー後の roms/*.rom の集合が、config.mk の TARGETS + DATA_TARGETS から
#      導いた期待集合と完全に一致すること (余分な .rom が残っていても失敗する)。
#   いずれかが不成立ならスクリプトは失敗し、SHA256SUMS は更新しません。
#
# 注意:
#   - build/ は .gitignore 済みです。roms/ に置くのは、本プロジェクトがソースから
#     ビルドした上記の ROM だけです。
#   - roms/7tbasic3.rom (BASIC インタプリタ本体) も他の代替 ROM と
#     同じく MIT ライセンス・ソース公開です。ソースは src/7tbasic3/ にあります。
#     詳細は LICENSE を参照してください。
#   - データ ROM 3 本 (roms/kanji.rom / roms/kanji2.rom / roms/dicrom.rom) は
#     コードを持たないデータです。漢字フォント ROM 2 本の字形は、パブリック
#     ドメイン宣言つきで配布されている第三者のビットマップフォント (fonts/) に
#     由来します。詳細は docs/LEGAL.md §9 を参照してください。
#
# 使い方:
#   1. 先に `make` でビルドしておく (build/*.rom が生成されます)。
#   2. このスクリプトを実行する:
#        bash scripts/copy_roms.sh
#   3. roms/ に配布 ROM 一式がそろいます。
# ============================================================
set -euo pipefail

# リポジトリ直下 (このスクリプトの 1 つ上)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
DEST_DIR="${ROOT}/roms"
CONFIG_MK="${ROOT}/config.mk"

if [ ! -f "${CONFIG_MK}" ]; then
  printf "[NG]   ビルド設定が見つかりません: %s\n" "${CONFIG_MK}"
  exit 1
fi

# --- config.mk から対象 ROM 一覧を取得 (唯一の情報源) ---
#   MIT セット (TARGETS) とデータ ROM (DATA_TARGETS) の 2 行を読み、連結して
#   配布 ROM の全一覧とする。どちらか一方でも読めなければ失敗させる。
TARGETS="$(sed -n 's/^[[:space:]]*TARGETS[[:space:]]*=[[:space:]]*//p' "${CONFIG_MK}" | head -1)"
if [ -z "${TARGETS}" ]; then
  printf "[NG]   %s から TARGETS を読み取れません。\n" "${CONFIG_MK}"
  exit 1
fi
DATA_TARGETS="$(sed -n 's/^[[:space:]]*DATA_TARGETS[[:space:]]*=[[:space:]]*//p' "${CONFIG_MK}" | head -1)"
if [ -z "${DATA_TARGETS}" ]; then
  printf "[NG]   %s から DATA_TARGETS を読み取れません。\n" "${CONFIG_MK}"
  exit 1
fi
ALL_TARGETS="${TARGETS} ${DATA_TARGETS}"

# --- config.mk から期待サイズを取得 ---
# SIZE_<name> = <bytes>
size_of() {
  sed -n "s/^[[:space:]]*SIZE_$1[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p" "${CONFIG_MK}" | head -1
}

mkdir -p "${DEST_DIR}"

# stat はGNU(-c)とBSD(-f%z)で差異があるため両対応
filesize() {
  stat -c %s "$1" 2>/dev/null || stat -f%z "$1"
}

rc=0
count=0
for name in ${ALL_TARGETS}; do
  expect="$(size_of "${name}")"
  if [ -z "${expect}" ]; then
    printf "[NG]   %-16s config.mk に SIZE_%s の定義がありません\n" "${name}.rom" "${name}"
    rc=1
    continue
  fi
  src="${BUILD_DIR}/${name}.rom"
  dst="${DEST_DIR}/${name}.rom"

  if [ ! -f "${src}" ]; then
    printf "[MISS] %s が見つかりません。先に make でビルドしてください。\n" "${src}"
    rc=1
    continue
  fi

  cp "${src}" "${dst}"
  sz="$(filesize "${dst}")"
  # サイズに加え build 産物と roms/ のバイト一致を検査する。
  if [ "${sz}" != "${expect}" ]; then
    printf "[NG]   %-16s サイズ %s B (期待 %s B)\n" "${name}.rom" "${sz}" "${expect}"
    rc=1
  elif ! cmp -s "${src}" "${dst}"; then
    printf "[NG]   %-16s build と roms が不一致\n" "${name}.rom"
    rc=1
  else
    printf "[OK]   %-16s -> %s  (%s B)\n" "${name}.rom" "${dst}" "${sz}"
    count=$((count + 1))
  fi
done

# --- コピー後の集合一致検査 ---
#   期待集合 (config.mk の TARGETS + DATA_TARGETS) と実際の roms/*.rom を突き合わせる。
#   欠落だけでなく「期待集合に無い .rom が残っている」場合も失敗させる。
#   この expected_set が、集合検査と後段の SHA256SUMS 生成で共有する唯一の
#   一覧である (config.mk の TARGETS + DATA_TARGETS 由来 + LC_ALL=C で整列)。
expected_set="$(for name in ${ALL_TARGETS}; do printf '%s.rom\n' "${name}"; done | LC_ALL=C sort)"
actual_set="$(cd "${DEST_DIR}" && ls -1 *.rom 2>/dev/null | LC_ALL=C sort || true)"

if [ "${expected_set}" != "${actual_set}" ]; then
  printf "\n[NG]   roms/ の内容が config.mk の TARGETS + DATA_TARGETS と一致しません。\n"
  # 一時ファイルは追跡ディレクトリ (roms/) ではなく mktemp の作業先に置く。
  tmp_dir="$(mktemp -d)"
  trap 'rm -rf "${tmp_dir}"' EXIT
  printf "%s\n" "${expected_set}" > "${tmp_dir}/expected"
  printf "%s\n" "${actual_set}"   > "${tmp_dir}/actual"
  while read -r line; do
    case "${line}" in
      '<'*) printf "  [欠落] %s : roms/ にありません\n" "${line#< }" ;;
      '>'*) printf "  [余分] %s : 対象一覧にありません (古い世代の残骸の可能性)\n" "${line#> }" ;;
    esac
  done < <(diff "${tmp_dir}/expected" "${tmp_dir}/actual" | grep -E '^[<>]' || true)
  rm -rf "${tmp_dir}"
  trap - EXIT
  rc=1
fi

if [ "${rc}" -eq 0 ]; then
  # SHA256SUMS を再生成 (公開追跡ファイル。第三者検証用)。
  #   対象と行順は Makefile の deploy-roms と同一経路に揃える。すなわち
  #   config.mk の TARGETS + DATA_TARGETS 由来の一覧を LC_ALL=C で整列し、roms/ を前置して
  #   sha256sum へ渡す。シェルのグロブ (roms/*.rom) は展開順が実行環境の
  #   照合順序に依存するため用いない (経路差で行順が揺れると、内容が同じでも
  #   差分が出てリグレッションの誤検知を招く)。
  ( cd "${ROOT}" && printf '%s\n' "${expected_set}" | sed 's|^|roms/|' | xargs sha256sum > SHA256SUMS )
  printf "\n配布 ROM %s 本を %s に用意し、SHA256SUMS を更新しました。\n" "${count}" "${DEST_DIR}"
else
  printf "\n一部の ROM を用意できませんでした。上記の表示を確認してください。\n"
fi
exit "${rc}"
