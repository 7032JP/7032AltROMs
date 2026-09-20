0 REM AltROMs test - MIT, own work.
1 REM 画面比較: COLOR=(パレット,カラー) のパレットコード 0 には常に 0 が
2 REM 割り付けられるので、地の色は黒のままになる。パレットコード 1 への
3 REM 割り付けは反映され、色 1 で描いた区画が指定した色で出る。
10 CLS
20 COLOR=(0,7)
30 COLOR=(1,3)
40 LINE (0,0)-(99,50),PSET,1,BF
50 GOTO 50
