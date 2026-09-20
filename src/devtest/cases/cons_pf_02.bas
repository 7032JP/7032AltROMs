0 REM AltROMs test - MIT, own work.
1 REM 機能キー行の表示中に KEY で定義を変えると、表示中の行が描き直される。
10 KEY 1,"run":KEY 2,"list":KEY 3,"k3":KEY 4,"k4":KEY 5,"k5"
20 KEY 6,"ab":KEY 7,"k7":KEY 8,"k8":KEY 9,"k9":KEY 10,"xyz"
30 CONSOLE 0,18,1
40 KEY 1,"new1":KEY 7,"seven"
50 PRINT "A"
60 GOTO 60
