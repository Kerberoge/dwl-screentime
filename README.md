### dwl-screentime
This is a simple daemon that keeps track of your screentime in dwl. It works by querying the appid of the currently focused window; stats are saved to a file of your choice in /tmp (/tmp/screentime by default).

### Features
- Updates only on window focus changes or when `screentime` is run, to conserve computing resources
- Screentime is reset at midnight
- Ability to restore screentime if the file already exists; this is useful in case screentimed gets terminated
- An archive of daily screentimes is kept in ~/.local/share/screentime by default

### Quick start
#### 1. Compile and install screentime and screentimed
Edit the makefile to change the prefix, and then run:
```
make
make install
```

#### 2. Patch dwl
screentimed requires a small modification to dwl in order to operate. Copy writeinfo.patch to the source directory of dwl, and then apply it:
```
git apply --reject writeinfo.patch
```
You might need to fix conflicts manually.

#### 3. Add screentimed to autostart[]
screentimed needs to run in the background while you're using dwl. Apply the [autostart patch](https://codeberg.org/dwl/dwl-patches/src/branch/main/patches/autostart) if you haven't done so already. Add an entry for screentimed to autostart[]:
```c
static const char *const autostart[] = {
    "screentimed", NULL,
    NULL,
};
```

#### 4. Recompile
Recompile dwl, log out and log back in for the changes to take effect.

### Usage
Simply run `screentime` to view your screentime:
```sh
$ screentime
Total       4m33s

Firefox     4m29s
foot           4s
```
