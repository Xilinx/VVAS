#file copy -force ./stage1_suggestions.rqs ./binary_container_1/link/vivado/vpl/prj/prj.runs/impl_1/
#file copy -force ./prj_conf/stage1_suggestions.rqs ./binary_container_1/link/vivado/vpl/prj/prj.runs/impl_1/
set script_path [ file dirname [ file normalize [ info script ] ] ]
puts $script_path
read_qor_suggestions ${script_path}/stage3_suggestions.rqs
