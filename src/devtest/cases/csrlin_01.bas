0 REM AltROMs test - MIT, own work.
1 REM CSRLIN はカーソルの垂直位置、POS(0) は水平位置、SCREEN(x,y) は指定した
2 REM キャラクタ座標のキャラクタコードを与える (参考書籍の規定)。
3 REM LOCATE で移した先を読み、画面に書いた文字列を SCREEN で逆順に読み戻す。
10 CLS
20 LOCATE 7,3:R=CSRLIN:C=POS(0)
30 A$="ALT ROM READBACK"
40 LOCATE 2,0:PRINT A$
50 LOCATE 0,1
60 FOR I=LEN(A$)+1 TO 2 STEP -1:PRINT CHR$(SCREEN(I,0));:NEXT
70 PRINT
80 PRINT "R=";R;"C=";C
90 END
