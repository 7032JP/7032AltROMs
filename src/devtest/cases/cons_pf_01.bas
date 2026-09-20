0 REM AltROMs test - MIT, own work.
1 REM CONSOLE の第 3 引数 (機能キー行表示)。1 にすると画面の下から 2 行に
2 REM 機能キーの定義が出て、スクロールに使える行が 2 行減る
3 REM (参考書籍「CONSOLE 文」)。欄の割り付け (1 行 5 個、
4 REM 欄は桁数の 1/5、左詰め、余りは空白) は書籍が定めない細部を本実装が
5 REM 定めたもの。
10 KEY 1,"run":KEY 2,"list":KEY 3,"k3":KEY 4,"k4":KEY 5,"k5"
20 KEY 6,"ab":KEY 7,"k7":KEY 8,"k8":KEY 9,"k9":KEY 10,"xyz"
30 CONSOLE 0,18,1,0
40 PRINT "TOP"
50 LOCATE 0,14:PRINT "ROW14"
60 GOTO 60
