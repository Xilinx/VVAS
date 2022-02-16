set script_path [ file dirname [ file normalize [ info script ] ] ]
puts $script_path
puts "${script_path}/prj_conf/stage1_suggestions.rqs"
read_qor_suggestions ${script_path}/stage1_suggestions.rqs
