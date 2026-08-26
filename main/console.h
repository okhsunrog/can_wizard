#ifndef MAIN_CONSOLE_H
#define MAIN_CONSOLE_H

// Sets up the console device, esp_console and linenoise, and registers all commands.
// Call before starting console_task_interactive.
void initialize_console(void);

// Runs the interactive prompt. Spawns the TX task that prints asynchronous output
// (CAN frames, logs) without corrupting the line being edited.
void console_task_interactive(void *arg);

#endif // MAIN_CONSOLE_H
