0 REM AltROMs test - MIT, own work.
1 REM LINE INPUT: 1 行をそのまま 1 個の文字列変数へ読む。引用符も値の一部。
2 REM プロンプトに ? を自動で付けない。行末の空白は切り落とし、行頭・行中は
3 REM 残す。空行は長さ 0。桁送りキー (TAB) は 8 桁刻みの桁位置まで空白を
4 REM 詰める。INPUT "プロンプト",変数 はプロンプトのみで ? を出さない。
5 REM (打鍵は linp_01.stdin)
10 LINE INPUT A$
20 PRINT "[";A$;"]";LEN(A$)
30 LINE INPUT "NM? ";B$
40 PRINT "[";B$;"]";LEN(B$)
50 LINE INPUT C$
60 PRINT "[";C$;"]";LEN(C$)
70 LINE INPUT D$
80 PRINT "[";D$;"]";LEN(D$)
90 INPUT "P",V
100 PRINT V
