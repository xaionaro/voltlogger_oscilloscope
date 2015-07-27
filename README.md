This code is supposed to visualize data saved by [https://devel.mephi.ru/dyokunev/voltlogger_parser](https://devel.mephi.ru/dyokunev/voltlogger_parser). At the moment there's no control panels/keys in the application, so you have to edit `x_userdiv`, `x_useroffset`, `y_userscale`, `y_useroffset`, `trigger_start_mode`, `trigger_start_y`, `trigger_end_mode` and `trigger_end_y` in `main.c` manually before compiling.

The main repository: git clone [https://devel.mephi.ru/dyokunev/voltlogger_oscilloscope](https://devel.mephi.ru/dyokunev/voltlogger_oscilloscope)

For example:

    socat -u udp-recv:30319 - | ./voltlogger_parser/voltlogger_parser -b -i - -n -t > ~/voltage.binlog &
    ./voltlogger_oscilloscope/voltlogger_oscilloscope -i ~/voltage.binlog -t

Screenshot:

![screenshot_20150727.png](https://devel.mephi.ru/dyokunev/voltlogger_oscilloscope/raw/master/doc/screenshot_20150727.png)

