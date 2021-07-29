foreach plugin : ['filter', 'multisrc', 'abrscaler']
  if not get_option(plugin).disabled()
    subdir(plugin)
  endif
endforeach

if host_machine.cpu_family() == 'x86_64'
  foreach plugin : ['vcudec', 'vcuenc', 'lookahead']
    if not get_option(plugin).disabled()
      subdir(plugin)
    endif
   endforeach
endif
