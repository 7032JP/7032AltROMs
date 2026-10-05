0 REM AltROMs test - MIT, own work.
1 REM BEEP と PLAY は音源のミキサ (R7) の上位 2 ビット (入出力ポートの向き) を
2 REM ビット 7 = 1・ビット 6 = 0 のまま変えない。BEEP と PLAY の後の R7 は
3 REM 184 (トーン 3 声有効・ノイズ無効) になる (期待値は mixer_01.expected_sound)。
10 BEEP
20 PLAY "C"
