<div align="center">

<br/>

```
███╗   ██╗██╗   ██╗██╗  ██╗███████╗██╗  ██╗
████╗  ██║╚██╗ ██╔╝╚██╗██╔╝██╔════╝██║  ██║
██╔██╗ ██║ ╚████╔╝  ╚███╔╝ ███████╗███████║
██║╚██╗██║  ╚██╔╝   ██╔██╗ ╚════██║██╔══██║
██║ ╚████║   ██║   ██╔╝ ██╗███████║██║  ██║
╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝
```

**A minimal, fast Unix shell written in C — built from scratch.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Language: C](https://img.shields.io/badge/Language-C-00599C.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-FCC624?logo=linux&logoColor=black)](https://kernel.org)
[![Status: Active](https://img.shields.io/badge/Status-Active-brightgreen.svg)]()

</div>

---

## What is nyxsh?

**nyxsh** is a Unix shell written entirely in C, designed to give developers full control over their command-line environment without the bloat. It is close to bash/zsh parity — supporting pipelines, redirection, job control, scripting, and a full set of built-in commands — while remaining lightweight, readable, and hackable.

Named after **Nyx**, the Greek goddess of the night, nyxsh is built for those who prefer the raw terminal over abstractions.

---

## Features

- **Interactive REPL** — Readline-based input with history and line editing
- **Command execution** — Foreground and background process management via `fork`/`exec`
- **Built-in commands** — `cd`, `exit`, `echo`, `export`, `unset`, `pwd`, `alias`, `history`, `help`, and more
- **Pipelines** — Full multi-pipe support: `cmd1 | cmd2 | cmd3`
- **I/O Redirection** — Input (`<`), output (`>`), append (`>>`), and stderr (`2>`) redirection
- **Environment variables** — Full `$VAR` expansion and `export`/`unset` support
- **Globbing** — Wildcard expansion (`*`, `?`, `[...]`)
- **Job control** — `fg`, `bg`, `jobs`, `Ctrl+Z` support
- **Scripting** — Run `.nyxsh` script files directly
- **Signal handling** — Proper `SIGINT`, `SIGTSTP`, `SIGCHLD` handling
- **Command history** — Persistent history across sessions
- **Prompt customization** — Configurable prompt with user, host, and cwd
- **Exit status** — `$?` and proper exit code propagation

---

## Installation

### Requirements

- GCC or Clang
- GNU Make
- Linux (Ubuntu, Debian, Arch, Kali, Pop!_OS, etc.)
- `libreadline-dev`

### Build from source

```bash
# Clone the repository
git clone https://github.com/NYXXAARIS/nyxsh.git
cd nyxsh

# Install readline dependency
sudo apt install libreadline-dev   # Debian/Ubuntu
# sudo pacman -S readline          # Arch

# Build
make

# Run
./nyxsh
```

### Optional: install system-wide

```bash
sudo make install
```

This installs `nyxsh` to `/usr/local/bin/nyxsh`.

---

## Usage

### Start an interactive session

```bash
nyxsh
```

### Run a script

```bash
nyxsh script.nyxsh
```

### Example session

```sh
nyxsh> echo "Hello from nyxsh"
Hello from nyxsh

nyxsh> ls -la | grep ".c" | sort
...

nyxsh> cat input.txt > output.txt

nyxsh> export NAME=nyxsh && echo $NAME
nyxsh

nyxsh> jobs
[1]  Running    sleep 100 &

nyxsh> history
1  echo "Hello from nyxsh"
2  ls -la | grep ".c" | sort
...
```

---

## Built-in Commands

| Command | Description |
|---|---|
| `cd [dir]` | Change working directory |
| `pwd` | Print working directory |
| `echo [args]` | Print arguments to stdout |
| `export VAR=val` | Set environment variable |
| `unset VAR` | Unset environment variable |
| `alias name=cmd` | Create a command alias |
| `history` | Show command history |
| `jobs` | List background jobs |
| `fg [n]` | Bring job to foreground |
| `bg [n]` | Resume job in background |
| `help` | List all built-in commands |
| `exit [code]` | Exit the shell |

---

## Project Structure

```
nyxsh/
├── src/
│   ├── main.c          # Entry point, REPL loop
│   ├── parser.c        # Tokenizer and command parser
│   ├── executor.c      # Fork/exec and pipeline logic
│   ├── builtins.c      # Built-in command implementations
│   ├── signals.c       # Signal handlers
│   ├── history.c       # Command history management
│   ├── expand.c        # Variable and glob expansion
│   └── jobs.c          # Job control
├── include/
│   └── nyxsh.h         # Header definitions
├── Makefile
├── LICENSE
└── README.md
```

---

## Roadmap

- [ ] Tab completion
- [ ] Syntax highlighting in prompt
- [ ] `.nyxshrc` config file support
- [ ] Here-documents (`<<EOF`)
- [ ] Arithmetic expansion (`$((expr))`)
- [ ] Function definitions
- [ ] Plugin/hook system

---

## Contributing

Contributions are welcome. To contribute:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Commit your changes: `git commit -m "feat: add your feature"`
4. Push and open a Pull Request

Please keep code clean, commented, and consistent with the existing C style.

---

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

<div align="center">

Built with C, caffeine, and a deep hatred for bloated shells.

**[NYXXAARIS](https://github.com/NYXXAARIS)**

</div>
