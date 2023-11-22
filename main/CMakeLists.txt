set(COMPONENT_SRCS
        "main.c"
        "cmd_system.c"
        "can.c"
        "console_task.c"
        "fs.c"
)

set(COMPONENT_ADD_INCLUDEDIRS
        "."
)

register_component()
