# off grid inst overlapping row site
source helpers.tcl
read_lef NangateOpenCellLibrary.lef
read_def overlap1.def
legalize_placement

set def_file [make_result_file overlap1.def]
write_def $def_file
diff_file $def_file overlap1.defok
