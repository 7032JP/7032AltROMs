0 REM AltROMs test - MIT, own work.
1 REM RND / RANDOMIZE: 値域 0 以上 1 未満。RND(0) は直前の値の再取得。
2 REM 負の引数は系列の初期化で、同じ負数からは同じ列が得られる。
3 REM RANDOMIZE n も同じ種からは同じ列。値そのものは実装固有なので、
4 REM 性質 (真 = -1) だけを照合する。
10 A=RND(-3):B=RND(0):PRINT A=B
20 C=RND:D=RND(0):PRINT C=D;C=A
30 X=RND:PRINT (X>=0) AND (X<1)
40 E=RND(-3):F=RND:G=RND(-3):H=RND:PRINT (E=G) AND (F=H)
50 RANDOMIZE 7:R1=RND:RANDOMIZE 7:R2=RND:PRINT R1=R2
