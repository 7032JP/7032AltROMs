0 REM AltROMs test - MIT, own work.
1 REM GCURSOR の座標の読取り。(x,y) にグラフィックカーソルを出し、移動キーで
2 REM 動かして、改行キーが押された時点の座標を変数の組へ読み込む。組は 10 個まで
3 REM 書け、末尾にパレットコードを添えられる。ここでは移動キーを打たずに改行キー
4 REM だけを打つので、出した位置がそのまま読まれる。受け皿は配列の要素でもよい。
10 CLS
20 DIM V(4)
30 X=0:Y=0:P=0:Q=0:R=0:S=0
40 GCURSOR (100,50),(X,Y)
50 GCURSOR (100,50),(P,Q),(R,S),3
60 GCURSOR (100,50),(V(0),V(1)),2
70 PRINT X;Y;P;Q
80 PRINT R;S;V(0);V(1)
90 END
