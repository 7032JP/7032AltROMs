0 REM AltROMs test - MIT, own work.
1 REM FRE: 数値引数は変数・配列の空き、文字列引数は文字列領域の空き。
2 REM 値は実行環境に依存するので正であることだけを照合する (真 = -1)。
3 REM POS(0) は現在のカーソル桁 (行頭 0、2 文字出力後 2)。
10 PRINT (FRE(0)>0) AND (FRE("")>0)
20 PRINT POS(0)
30 PRINT "AB";
40 PRINT POS(0)
