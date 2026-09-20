0 REM AltROMs test - MIT, own work.
1 REM 画面比較: PSET (色番号 0-7 の点、色省略時は COLOR の色) と PRESET
2 REM (背景色で消す)。塗り箱の中の 1 点を PRESET で消す。
3 REM 描画を残すため終端は無限ループ。
10 CLS
20 FOR I=0 TO 7:PSET (10+I*20,10,I):NEXT
30 PSET (100,50):PSET (120,50,4)
40 LINE (200,40)-(260,60),PSET,7,BF
50 PRESET (230,50)
60 GOTO 60
