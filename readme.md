# launcher, launcher64 - windows utility to launch process with restrictions

May 2020. Was called "master+slave" back then. VS2017 redistributable was needed to launch this program

## Usage

```
launcher "what" TL ML "in" "out"
```

Launcher return value (view as echo %errorlevel%):

|code|      description      |
|----|-----------------------|
|  0 | Normal                |
|  1 | Time limit exceeded   |
|  2 | Memory limit exceeded |
|  3 | Runtime error         |
|  4 | Launcher error        |

__What__ can either be
- a path to executable, e.g. "C:/WINDOWS/System32/notepad.exe"
- a path to executable with arguments, e.g. "notepad C:/boot.ini"

__Time limit__ is set in milliseconds (1/1000 of second). 0 to disable time limit check, value should never be negative.

**WARNING!** As user time limit in Windows Job Objects is inprecise, this TL is imprecise too.

Memory limit is set in KiBytes (1024 bytes). 0 to disable memory limit, value should never be negative. If set to lesser than 512K then ML will be set to 512K.

**WARNING!** 32-bit executables without PAE support memory limit is 4194304K

__in__ and __out__ are paths to Input and Output files respectively. Input file must be accessible. Output file will be cleared before executing slave. If one of the files is inaccessible you will get Launcher error (code 4).

This application is designed to be run from other applications and not to be run directly. ← (Jun 2023: I would never believe this. lmao)

If you put something "not-a-number" into TL or ML, then you will get an undefined behavior.

I recommend you to put executable command line, input and output file path into double quotes, line in "hello_world.inp"


↓ what is this?? ↓
Checker (create it yourself)
Usage: checker "in" "out"
where
  in - input passed to slave
  out - output got from slave
Checker should return
  0 in case of OK
  1 in case of WA
  2 in case of internal error


## Building

probably with qbs, needs VC++ toolchain for /MD flags (something related to stdlib?)
