# KFMon
[![License](https://img.shields.io/github/license/NiLuJe/kfmon.svg)](/LICENSE) (https://www.codacy.com/app/NiLuJe/kfmon?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=NiLuJe/kfmon&amp;utm_campaign=Badge_Grade) [![Latest tag](https://img.shields.io/github/tag-date/NiLuJe/kfmon.svg)](https://github.com/NiLuJe/kfmon/releases/)

Kute File Monitor

Licensed under the [GPLv3+](/LICENSE).
Housed [here on GitHub](https://github.com/NiLuJe/kfmon).

## What's it for?

This is intended as an improvement over [Sergey](https://bitbucket.org/vlasovsoft/free/src/master/fmon/)'s [Kobo File Monitor](http://www.mobileread.com/forums/showthread.php?t=218283). If you're unfamiliar with fmon, it basically launches a specific action when you open a specific book on your Kobo, thanks to Linux's `inotify` API. Usually, a single PNG file is used as the "trigger" book.

The original fmon does zero sanity checking, and, given the intricacies of how Nickel actually processes books, it might trigger an action *before* the trigger file has successfully been processed by Nickel. Depending on the action in question, this might lead to fun boot loops or other weirdness ;).

KFMon tries to alleviate this issue by doing a number of checks before deeming that launching the action is "safe": we first check if the file in question is even *in* the Library database, and then we confirm that it has been processed further by checking for the existence of the various thumbnails Nickel creates when first displaying a book in its Library and Homescreen. We also handle some more recent quirks of FW >= 4.13 properly.

On top of that, we have a few extra features: instead of launching one instance per book/action pair, KFMon is a centralized daemon, which simply parses a number of simple INI config files. Each book/action pair gets a dedicated config file.

It also keeps track of processes it has launched, mainly to ensure that for a given watch, only one single instance of its action can ever be run concurrently. As a practical exemple, this means that if you use KFMon to launch KOReader (by tapping its PNG icon), and, once inside KOReader, you try to do the very same thing, KFMon will remember that KOReader is already running, and will refuse to launch a second instance until the previous one has exited. This means that long-running apps don't necessarily need to kill KFMon when they start.

There's also an extra layer of protection (user configurable, enabled for KOReader & Plato) that'll prevent simply *anything* from being launched as long as that custom document reader is still running. (You can also enforce this behavior from *outside* of KFMon, via a BLOCK file, see the FAQ at the bottom of this page).

In the same vein, KFMon's startup script will also refuse to run concurrent instances of KFMon itself.

~~It's also integrated in the Kobo boot process in an unobtrusive manner (an udev hook), unlike fmon (which modifies a startup script).~~ (See [Issue #2](https://github.com/NiLuJe/kfmon/issues/2) for the various troubles that caused us ;)).

And it also properly persists across unmounts & remounts (like during an USBMS export).

Since v1.1.0, it's also using my [FBInk](https://github.com/NiLuJe/FBInk) library to provide visual feedback :). We also ship the `fbink` commandline tool to be used in your own startup scripts ;).

**IMPORTANT NOTE**: Some of these checks requires a decently recent enough Nickel version. Make sure you're running a firmware version equal to or newer than 2.9.0! That's the only actual requirement: KFMon is completely device-agnostic, and should work on the full range of Kobo devices (even new and unreleased ones), provided they run a supported Nickel version.

**NOTE**: If you know you're running a legacy fmon-friendly FW version, and you're just looking for a drop-in replacement of Sergey's fmon, check out [Baskerville](https://github.com/baskerville/fmon)'s implementation of fmon. It's safer & saner than the original, while keeping parts of the design instact (namely, and of particular interest to end-users, it's using a similar config scheme).

## How do I install this?

First, if you're currently using fmon, it might be a good idea to uninstall it first ;). Since both patch the same startup script, only the last one you installed will actually "take".
In the same vein, if you're using KSM and it is starting up at boot, it will also inhibit our startup. This should ensure you'll never have multiple different launchers running concurrently.

Then, head over to the [dedicated MobileRead thread](http://www.mobileread.com/forums/showthread.php?t=274231), and simply unpack the ZIP archive (the main one, not the uninstaller, obviously) to the USB root of your Kobo when it's plugged to a computer. Do *NOT* try to open the archive and drag/copy stuff manually, just "Extract to" the root of your device, and say yes to replacing existing content if/when applicable (the directory structure & content have to be preserved *verbatim*, and there are hidden *nix folders in there that your OS may hide from you)!

The package contains an example config to launch [KOReader](http://www.mobileread.com/forums/forumdisplay.php?f=276), if it is already installed, as well as a `KoboRoot.tgz` which will actually install KFMon itself.
This ensures that the KOReader PNG file will first be processed by Nickel before the KoboRoot package triggers a reboot for installation.

The same is also true for [Plato](https://www.mobileread.com/forums/showthread.php?t=292914), [see inside](/config/plato.ini) for details (it basically boils down to: follow upstream's install instructions ;)).

If any of these extra icons in your Library bother you, you *can* safely delete them however you wish, but you'll also have to delete the matching config files if you want to avoid the warning message a config without its icon will trigger ;).

### One Click Packages

If your ultimate goal is installing KOReader and/or Plato *for the first time*, I'm also providing "one-click" packages that rely on KFMon to do just that: take a look at the [MobileRead thread](https://www.mobileread.com/forums/showthread.php?t=314220) ;).

## How can I tinker with it?

The config files are stored in the */mnt/onboard/*__.adds/kfmon/config__ folder.

KFMon itself has a dedicated config file, [kfmon.ini](/config/kfmon.ini), with three knobs:

`db_timeout = 500`, which sets the maximum amount of time (in ms) we wait for Nickel to relinquish its hold on its database when we try to access it ourselves. If the timeout expires, KFMon assumes that Nickel is busy, and will *NOT* launch the action.
This default value (500ms) has been successfully tested on a moderately sized Library, but if stuff appears to be failing to launch (after ~10s) on your device, and you have an extensive or complex Library, try increasing this value.  
Note that on current FW versions (i.e., **>= 4.6.x**), the potential issue behind the design of this option is far less likely to ever happen, so you shouldn't have to worry about it ;).

In any case, you can confirm KFMon's behavior by checking its log, which we'll come to presently.

`use_syslog = 0`, which dictates whether KFMon logs to a dedicated log file (located in */usr/local/kfmon/kfmon.log*), or to the syslog (which you can access via the *logread* tool on the Kobo). Might be useful if you're paranoid about flash wear. Disabled by default. Be aware that the log file will be trimmed if it grows over 1MB.

`with_notifications = 1`, which dictates whether KFMon will print on-screen feedback messages (via [FBInk](https://github.com/NiLuJe/FBInk)) when an action is launched successfully. Note that error messages will *always* be shown, regardless of this setting.

Note that this file will be *overwritten* by the KFMon install package, so, if you want your changes to persist across updates, you may want to make your modifications in a copy of that file, one that you should name *kfmon*__.user__*.ini*.

## How can I add my own actions?

Each action gets a [dedicated INI file](/config/usbnet.ini) in the config folder, so just drop a new `.ini` in the config folder.
This should make it trivial to port existing fmon setups.
As you would expect, a simple file/action pair only requires two entries:

`filename = /mnt/onboard/my_pretty_icon.png`, which points to the "book" file you want to tie your action to. In this example, it's a simple PNG file named `my_pretty_icon.png` located at the USB root of the device. This has to be an absolute path, and, of course, has to point to a location Nickel will parse (i.e., usually somewhere in */mnt/onboard*, and not nested in a dotfolder). The basename of that file should also be *unique* across all your configs, so avoid common names.

`action = /mnt/onboard/.adds/mycoolapp/app.sh`, which points to the binary/script you want to trigger when your "book" is opened. This has to be an absolute path. And if this points to somewhere on the rootfs, it has to have the exec bit set.

Note that the section all these key/value pairs fall under *has* to be named `[watch]`!

Note that none of these two fields can exceed **128 characters**, if they do, the whole file will be discarded!

Next comes optional entries:

`label = My cool app`, which specify a text label that may be used by a GUI frontend (otherwise, the basename of the filename entry is used).

`hidden = 0`, which, when set to 1, prevents this action from being listed by a GUI frontend.

`block_spawns = 0`, which, when set to 1, prevents *anything* from being launched by KFMon while the command from the watch marked as such is still running. This is mainly useful for document readers, since they could otherwise unwittingly trigger a number of other watches (usually through their background metadata reader, their thumbnailer, or more generally their file manager). Which is precisely why this is set to 1 for KOReader & Plato ;).

In addition to that, you can try to do some cool but potentially dangerous stuff with the Nickel database: updating the Title, Author and Comment entries of your "book" in the Library.
This is disabled by default, because ninja writing to the database behind Nickel's back *might* upset Nickel, and in turn corrupt the database...
If you want to try it, you will have to first enable this knob:

`do_db_update = 1`

And you will have to set *all three* of the following key/value pairs:

`db_title = My Cool App`, which sets the Title of your "book" in the Library.

`db_author = An Awesome Team`, which sets the Author of your "book" in the Library.

`db_comment = A cool app that does neat stuff made by an awesome team.`, which sets the Comment shown in the "Details" panel of the "book" in the Library.

Note that these three fields will be cropped at 128 characters.

When in doubt, look at an existing config, like the [USBNet](/config/usbnet.ini) one (and its matching [icon](/resources/usbnet.png)), tailored for my USBNet/USBMS toggle script from [KoboStuff](https://www.mobileread.com/forums/showthread.php?t=254214) ;).

## How do I uninstall this?

There is a *KFMon-Uninstaller.zip* package available in the MobileRead thread. Inside, you'll find a *KoboRoot.tgz* that will automate much of this. (It will leave whatever's in `/mnt/onboard/.adds/kfmon` untouched).

### I want to do it manually!

You absolutely can, and here's how (ideally over SSH).

You'll basically just have to delete a couple of things:

The file `/etc/udev/rules.d/99-kfmon.rules` (which may not exist anymore, depending on which version of KFMon you were running).

And the folders `/usr/local/kfmon`, as well as `/mnt/onboard/.adds/kfmon` too, if you don't want to keep whatever custom things you might have written in there.

Optionally, you might also want to restore a vanilla version of `/etc/init.d/on-animator.sh` (e.g., [as found here](https://github.com/NiLuJe/kfmon/blob/master/scripts/uninstall/on-animator.sh)), although nothing untoward will happen if you don't (plus, it's one of the files being replaced during a FW update).

## Things to watch out for

-   If any of the watched files cannot be found, KFMon will simply forget about it, and keep honoring the rest of the watches. It will shout at you to warn you about it, though!  
    -   KFMon will only parse its own config file(s) at boot, but it *will* check for new/removed/updated **watch** config files after an USBMS session.  
    -   This means you will *NOT* need to reboot your device after adding new config files or modifying or removing existing ones over USB ;).  
    -   But if you delete one of the files being watched, don't forget to delete the matching config file, or KFMon will continue to try to watch it (and thus warn about it).  

-   Due to the exact timing at which Nickel parses books, for a completely new file, the first action might only be triggered the first time the book is *closed*, instead of opened (i.e., the moment the "Last Book Opened" tile is generated and shown on the Homescreen).
    -   Good news: If your FW version is recent enough to feature the new Homescreen, there's a good chance things will work in a more logical fashion (because the last few files added now automatically pop up on the Home page) ;).  

-   Due to the way Nickel may be caching some operations, if you try to restore an icon that you had previously deleted *in the current boot cycle*, it will keep being flagged as *processing* until the next boot, because Nickel may be using in-memory instances of the thumbnails, while we check for them on-disk!
    -   This has nothing to do with KFMon in particular, e.g., if you were to update some book covers/thumbnails via Calibre, you'd see the same behavior. A simple reboot will put things back in order :).  

-   KFMon only expects to watch for files in the internal storage of the device (i.e., *onboard*). On devices with an external sdcard, watching for files on that external storage is unsupported (it may work, but the code makes a number of assumptions which may not hold true in that case, which could lead to undefined behavior).  

-   Proper interaction with KOReader in general requires a recent version of KOReader (i.e., >= 2015.11-1735).
    -   As far as for successfully restarting nickel on exit is concerned, I'd also recommend running a current FW version (last tested on FW 4.7.x - 4.28.x).  

-   When either KOReader or Plato is launched *through KFMon*, **nothing** will be allowed to spawn while that document reader is still running. This is to prevent spurious events that may be triggered by their file manager.  

-   PSA about the proper syntax expected in an INI file: while the `;` character indeed marks the beginning of an inline comment, it must be preceded by some kind of whitespace to actually register as a comment. Otherwise, it's assumed to be part of the value.
    -   Meaning `key=value;` will probably not work as you might expect (it'll parse as `key` set to `value;` and not `value`).
    -   On a related note, a line cannot exceed 200 bytes. If the log reports a parsing error on a seemingly benign line, but one which happens to feature a humonguous amount of inline comments, that may very well be the reason ;).
    -   If the log reports a parsing error at (or near, depending on commented lines) the top of the config file, check that you haven't forgotten the `[watch]` section name ;).  
    -   If you keep getting a "still processing" warning for a brand new watch, despite the thumbnails having visibly been processed, make sure you respected the case properly in the filename field of the watch config: FAT32 is case-insensitive, but we make case-sensitive SQL queries because they're much faster!  

-   You **will** have to reinstall KFMon after a firmware update (since most FW update packages ship the vanilla version of the startup script patched to launch KFMon).  

-   Speaking of updates, if, right after a KFMon update or reinstall, KFMon appears to be disabled, simply fully restart your device. There's a weird quirk involving specific timings during the Kobo update process that may prevent KFMon from starting up properly. You can (manually) check the KFMon logs for more info.  

-   Speaking of the log, as mentioned earlier, it is located by default in */usr/local/kfmon/kfmon.log*, but tapping the KFMon icon, besides printing the tail end of it on screen, will also dump a full copy of it in */mnt/onboard/***.adds/kfmon/log/kfmon_dump.log**, making it easily accessible even if you don't have shell access to your device.

-   Right now, KFMon supports a maximum of [16](https://github.com/NiLuJe/kfmon/blob/08f18a8f30653e88132b5ecb0fda6efc5886951a/kfmon.h#L181) file watches. Ping me if that's not enough for you ;).

-   If, for some reason, you need to prevent KFMon from spawning *anything* for a while, just drop a blank *BLOCK* file in the *config* folder, i.e., *touch /mnt/onboard/.adds/kfmon/config/BLOCK*. Simply remove it when you want KFMon to do its thing again ;).

-   You can optionally replace the boot progress bar with a faster custom alternative, in order to shave a few seconds off of Nickel's boot time. This is not done by default, because it *might* break older FW versions (say, < 4.8), and it *will* break some custom apps (e.g., Sergey's launcher, KSM) *if* you bypass Nickel entirely.
    If you know you're safe (i.e., you're running a current FW release, and you always boot straight into Nickel), you can enable this by simply dropping a blank *BAR* file in the *config* folder, i.e., *touch /mnt/onboard/.adds/kfmon/config/BAR*.
    NOTE: On FW >= 5.x, this needs to be created on the rootfs instead: *touch /usr/local/kfmon/BAR* (because onboard isn't mounted yet when the animation starts).
    Current versions of KOReader and Plato will have no issue with this new behavior.
    
-   Since FW 4.17, Nickel *will* index content found in *nix hidden directories by default. Thankfully, this behavior is [configurable](https://www.mobileread.com/forums/showpost.php?p=3892463&postcount=10).  
    Note that the "[One Click Packages](#one-click-packages)" [installers](https://github.com/NiLuJe/kfmon/blob/43f43c0d2570bab87d3e0736d297cb699c326178/tools/install.sh#L85-L97) take care of handling that setting automatically ;).
    
-   KFMon 1.4.0 introduced an IPC mechanism, allowing interaction (be it listing available actions, or triggering them) with KFMon from the outside world (be it scripts or even a GUI frontend, like [NickelMenu](https://www.mobileread.com/forums/showthread.php?t=329525)).  
    Communication is done over a Unix socket, see [kfmon_ipc.c](/utils/kfmon-ipc.c) for a basic C implementation, which ships with every KFMon installation.  
    Just run `kfmon-ipc` in a shell, or use it as part of a shell pipeline, e.g., `echo "list" | kfmon-ipc 2>/dev/null`. KFMon will reply with usage information if you send an invalid or malformed command.
    
-   Since v1.4.1, to ensure proper IPC behavior, the *basename* of **every** watch filename key should be *unique*. Check KFMon's logs when in doubt, it'll enforce that restriction and warn about it.

-   Since v1.4.1, you can now make a config IPC-only by deleting the trigger image (i.e., what *filename* points to). The actual *filename* entry in the config still *has to* exist, and follow the uniqueness requirements, though.

<!-- kate: indent-mode cstyle; indent-width 4; replace-tabs on; remove-trailing-spaces none; -->
