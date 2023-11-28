set(COMPONENT_SRCS
        "main.c"
        "cmd_system.c"
        "can.c"
        "console.c"
        "fs.c"
        "xvprintf.c"
        "cmd_can.c"
        "cmd_utils.c"
)

set(COMPONENT_ADD_INCLUDEDIRS
        "."
)

register_component()
