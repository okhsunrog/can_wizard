#ifndef MAIN_CMD_SYSTEM_H
#define MAIN_CMD_SYSTEM_H

// Register all system functions
void register_system(void);

// Register common system functions: "version", "restart", "free", "heap", "tasks"
void register_system_common(void);

// Register deep and light sleep functions
void register_system_sleep(void);

#endif // MAIN_CMD_SYSTEM_H
