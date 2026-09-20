0 REM AltROMs test - MIT, own work.
1 REM GET@/PUT@ 形式1/2 (書式指定子 G なし): キャラクタ座標の矩形にある文字を
2 REM 配列へ読み込み、別の位置へ書き戻して読み直す。形式1 は 1 セル 1 バイトの
3 REM キャラクタコード、形式2 (@A) は 1 セル 2 バイトで下位がキャラクタコード。
10 CLS
20 DIM A%(1):DIM B%(1):DIM C%(1)
30 LOCATE 10,5:PRINT "F-BASIC";
40 GET@(10,5)-(11,5),A%
50 GET@A(10,5)-(10,5),C%
60 CLS
70 PUT@(0,10)-(1,10),A%
80 GET@(0,10)-(1,10),B%
90 CLS
100 PRINT A%(0);A%(1)
110 PRINT B%(0);B%(1)
120 PRINT C%(0) AND 255
