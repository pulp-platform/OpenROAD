source helpers.tcl

read_lef  Nangate45.lef
read_def gcd.def
read_liberty NangateOpenCellLibrary_typical.lib
read_sdc gcd.sdc
report_checks
set_net_voltage -net VDD -voltage 1.1
analyze_power_grid -net VDD
