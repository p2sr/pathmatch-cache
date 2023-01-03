# pathmatch-cache

A simple load optimisation tool for Portal 2 on Linux.

**This utility is obsolete. Its functionality is now part of [SourceAutoRecord].**

[SourceAutoRecord]: https://github.com/p2sr/SourceAutoRecord

## Usage

The library built, `pathmatch_cache.so`, should simply be injected into
the game process using `LD_PRELOAD`. This can be done by following these
steps:

- Place `pathmatch_cache.so` into `steamapps/common/Portal 2/`
- Edit the file `steamapps/common/Portal 2/portal2.sh`
- Beneath the line that reads `cd "$GAMEROOT"`, add the following line:
	`export LD_PRELOAD="pathmatch_cache.so:$LD_PRELOAD"`
- Save this file, and start the game.

## Results

On my system, this managed to decrease my average load time in a normal
singleplayer session from 3.9s to 1.6s (as reported by [SAR]).

Note that due to the implementation of this cache, it adds some small
limitations to what can be done in the game. Specifically, using a file
which you have just created - for instance, trying to run `playdemo` for
a demo you just recorded - may not be possible without restarting the
game.

[SAR]: https://github.com/p2sr/SourceAutoRecord

## Technical explanation

When running on Linux, the Source Engine uses a small system called
`pathmatch` to simulate a case-insensitive filesystem, as Linux
filesystems are normally case-sensitive. This is effectively a long
recursive function, called for most stdio file/directory function
invocations. Due to how this function is implemented, it is incredibly
slow, performing thousands of calls to `readdir`. This is a major
contributor to the fact that map loads (and general engine performance)
are significantly slower on Linux than on Windows.

The solution to this slowdown is caching the `pathmatch` results. The
function can be found within the `filesystem_stdio.so` module of the
engine; by loading this module and scanning for the function's byte
sequence, we can overwrite its code with a simple trampoline which first
calls into a caching system.
