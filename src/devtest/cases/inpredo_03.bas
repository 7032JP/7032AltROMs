0 REM AltROMs test (MIT)
1 REM INPUT の変数の添字に VAL を書いても、入力行を読み終えるまで壊さない。
10 DIM A(3)
20 INPUT A(VAL("1")),B
30 PRINT "GOT";A(1);B
40 END
