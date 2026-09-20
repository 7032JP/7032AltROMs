0 REM AltROMs test - MIT, own work.
1 REM F-BASIC V3.0 に PLAY 関数は無い。未定義の関数形を式に書いた 20 行で Syntax Error になることを確かめる。
2 REM 30 行以降はエラーにより実行されない。演奏状態を返す仕様を定義する試験ではない。
10 PLAY "L1C","L1E"
20 A=PLAY(0):B=PLAY(1):C=PLAY(2):D=PLAY(3)
30 PRINT A;B;C;D
40 WHILE PLAY(0):WEND
50 PRINT PLAY(0);PLAY(1)
