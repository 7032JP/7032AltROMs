0 REM AltROMs test - MIT, own work.
1 REM PRINT USING (文字列): ! は先頭 1 文字、& & は欄の幅 (2 個の & と間の
2 REM 空白の合計)、@ は全体。書式は値が残っている限り繰り返し使われる。
10 PRINT USING "!";"ABC"
20 PRINT USING "& &";"ABCDE"
30 PRINT USING "& &";"AB"
40 PRINT USING "@";"ABCDE"
50 PRINT USING "<#> ";1;2;3
60 PRINT USING "!.!";"AB";"CD"
