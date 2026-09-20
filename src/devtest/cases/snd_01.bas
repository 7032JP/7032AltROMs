0 REM AltROMs test - MIT, own work.
1 REM SOUND: レジスタ番号 (0-13) とデータ (0-255) を音源レジスタへ直接書く。
2 REM 実行環境の音源レジスタを観測し、書いた値がそのまま現れることを照合する
3 REM (期待値は snd_01.expected_sound)。書いた状態を保つため終端は無限ループ。
10 SOUND 0,171
20 SOUND 1,1
30 SOUND 8,15
40 SOUND 7,248
50 GOTO 50
