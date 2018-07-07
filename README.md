# KFMon
Kute File Monitor

Licensed under the [AGPLv3](/LICENSE).
Housed [here on GitHub](https://github.com/NiLuJe/kfmon).

# What's it for?

This is intended as an improvement over [Sergey](https://bitbucket.org/vlasovsoft/free/src/tip/fmon/)'s [Kobo File Monitor](http://www.mobileread.com/forums/showthread.php?t=218283). If you're unfamiliar with fmon, it basically launches a specific action when you open a specific book on your Kobo, thanks to Linux's ```inotify``` API. Usually, a single PNG file is used as the "trigger" book.

The original fmon does zero sanity checking, and, given the intricacies of how Nickel actually processes books, it might trigger an action *before* the trigger file has successfully been processed by Nickel. Depending on the action in question, this might lead to fun boot loops or other weirdness ;).

KFMon tries to alleviate this issue by doing a number of checks before deeming that launching the action is "safe": we first check if the file in question is even *in* the Library database, and then we confirm that it has been processed further by checking for the existence of the various thumbnails Nickel creates when first displaying a book in its Library and Homescreen.

On top of that, we have a few extra features: instead of launching one instance per book/action pair, KFMon is a centralized daemon, which simply parses a number of simple INI config files. Each book/action pair gets a dedicated config file.

It also keeps track of processes it has launched, mainly to ensure that for a given watch, only one single instance of its action can ever be run concurrently. As a practical exemple, this means that if you use KFMon to launch KOReader (by tapping its PNG icon), and, once inside KOReader, you try to do the very same thing, KFMon will remember that KOReader is already running, and will refuse to launch a second instance until the previous one has exited. This means that long-running apps don't necessarily need to kill KFMon when they start.

There's also an extra layer of protection (user configurable, enabled for KOReader & Plato) that'll prevent simply *anything* from being launched as long as that custom document reader is still running.

In the same vein, KFMon's startup script will also refuse to run concurrent instances of KFMon itself.

~~It's also integrated in the Kobo boot process in an unobtrusive manner (an udev hook), unlike fmon (which modifies a startup script).~~ (See [Issue #2](https://github.com/NiLuJe/kfmon/issues/2) for the various troubles that caused us ;)).

And it also properly persists across unmounts & remounts (like during an USBMS export).

Since v1.1.0, it's also using my [FBInk](https://github.com/NiLuJe/FBInk) library to provide visual feedback :). We also ship the `fbink` commandline tool to be used in your own startup scripts ;).

**IMPORTANT NOTE**: Some of these checks requires a decently recent enough Nickel version. Make sure you're running a firmware version equal to or newer than 2.9.0! That's the only actual requirement: KFMon is completely device-agnostic, and should work on the full range of Kobo devices (even new and unreleased ones), provided they run a supported Nickel version.

**NOTE**: If you're just looking for a drop-in replacement of Sergey's fmon, check out [Baskerville](https://github.com/baskerville/fmon)'s implementation of fmon. It's safer & saner than the original, while keeping parts of the design instact (namely, and of particular interest to end-users, it's using a similar config scheme).

# How do I install this?

First, if you're currently using fmon, it might be a good idea to uninstall it first ;). Since both patch the same startup script, only the last one you installed will actually "take".
In the same vein, if you're using KSM and it is starting up at boot, it will also inhibit our startup. This should ensure you'll never have multiple different launchers running concurrently.

Then, head over to the [dedicated MobileRead thread](http://www.mobileread.com/forums/showthread.php?t=274231), and simply unpack the ZIP archive (the main one, not the uninstaller, obviously) to the USB root of your Kobo when it's plugged to a computer.

The package contains an example config to launch [KOReader](http://www.mobileread.com/forums/forumdisplay.php?f=276), if it is already installed, as well as a ```KoboRoot.tgz``` which will actually install KFMon itself.
This ensures that the KOReader PNG file will first be processed by Nickel before the KoboRoot package triggers a reboot for installation.

You can also find a config to launch [Plato](https://www.mobileread.com/forums/showthread.php?t=292914) in the repository, [right here](/config/plato.ini). See inside for details.

# How can I tinker with it?

The config files are stored in the */mnt/onboard/*__.adds/kfmon/config__ folder.

KFMon itself has a dedicated config file, [kfmon.ini](/config/kfmon.ini), with two knobs:

```db_timeout = 500```, which sets the maximum amount of time (in ms) we wait for Nickel to relinquish its hold on its database when we try to access it ourselves. If the timeout expires, KFMon assumes that Nickel is busy, and will *NOT* launch the action.
This default value (500ms) has been successfully tested on a moderately sized Library, but if stuff appears to be failing to launch (after ~10s) on your device, and you have an extensive or complex Library, try increasing this value.
Note that on current FW versions (i.e., >= 4.6.x), the potential issue behind the design of this option is far less likely to ever happen, so you shouldn't have to worry about it ;).

In any case, you can confirm KFMon's behavior by checking its log, which we'll come to presently.

```use_syslog = 0```, which dictates whether KFMon logs to a dedicated log file (located in */usr/local/kfmon/kfmon.log*), or to the syslog (which you can access via the *logread* tool on the Kobo). Might be useful if you're paranoid about flash wear. Disabled by default. Be aware that the log file will be trimmed if it grows over 1MB.

# How can I add my own actions?

Each action gets a [dedicated INI file](/config/koreader.ini) in the config folder, so just drop a new ```.ini``` in the config folder.
This should make it trivial to port existing fmon setups.
As you would expect, a simple file/action pair only requires two entries:

```filename = /mnt/onboard/my_pretty_icon.png```, which points to the "book" file you want to tie your action to. In this example, it's a simple PNG file named ```my_pretty_icon.png``` located at the USB root of the device. This has to be an absolute path, and, of course, has to point to a location Nickel will parse (i.e., usually somewhere in */mnt/onboard*, and not nested in a dotfolder).

```action = /mnt/onboard/.adds/mycoolapp/app.sh```, which points to the binary/script you want to trigger when your "book" is opened. This has to be an absolute path. And if this points to somewhere on the rootfs, it has to have the exec bit set.

Note that the section all these key/value pairs fall under *has* to be named ```[watch]```!

Next comes optional entries:

```block_spawns = 0```, which, when set to 1, prevents *anything* from being launched by KFMon while the command from the watch marked as such is still running. This is mainly useful for document readers, since they could otherwise unwittingly trigger a number of other watches (usually through their background metadata reader, their thumbnailer, or more generally their file manager). Which is precisely why this is set to 1 for KOReader & Plato ;).

In addition to that, you can try to do some cool but potentially dangerous stuff with the Nickel database: updating the Title, Author and Comment entries of your "book" in the Library.
This is disabled by default, because ninja writing to the database behind Nickel's back *might* upset Nickel, and in turn corrupt the database...
If you want to try it, you will have to first enable this knob:

```do_db_update = 1```

And you will have to set *all three* of the following key/value pairs:

```db_title = My Cool App```, which sets the Title of your "book" in the Library.

```db_author = An Awesome Team```, which sets the Author of your "book" in the Library.

```db_comment = A cool app that does neat stuff made by an awesome team.```, which sets the Comment shown in the "Details" panel of the "book" in the Library.

Note that these three fields will be cropped at 128 characters.

# How do I uninstall this?

There is a *KFMon-Uninstaller.zip* package available in the MobileRead thread. Inside, you'll find a *KoboRoot.tgz* that will automate much of this. (It will leave whatever's in ```/mnt/onboard/.adds/kfmon``` untouched).

## I want to do it manually!

You absolutely can, and here's how (ideally over SSH):

You'll basically just have to delete a couple of things:

The file ```/etc/udev/rules.d/99-kfmon.rules``` (which may not exist anymore, depending on which version of KFMon you were running).

And the folders ```/usr/local/kfmon```, as well as ```/mnt/onboard/.adds/kfmon``` too, if you don't want to keep whatever custom things you might have written in there.

Optionally, you might also want to restore a vanilla version of ```/etc/init.d/on-animator.sh``` (f.g., [as found here](https://github.com/NiLuJe/kfmon/blob/master/scripts/uninstall/on-animator.sh)), although nothing untoward will happen if you don't.

# Things to watch out for

* KFMon will abort if any of the watched files cannot be found when it starts up.
* KFMon will only parse config files at boot.
  * Meaning you will need to reboot your device after adding new config files or modifying or removing existing ones ;).
  * If it's a new config file, try to make sure it points to a file that has already been processed by Nickel (after an USBMS plug/eject session, for instance) to save you some puzzlement ;).
  * If you delete one of the files being watched, don't forget to delete the matching config file, and then to reboot your device!
* Due to the exact timing at which Nickel parses books, for a completely new file, the first action might only be triggered the first time the book is *closed*, instead of opened (i.e., the moment the "Last Book Opened" tile is generated and shown on the Homescreen).
  * Good news: If your FW version is recent enough to feature the new Homescreen, there's a good chance things will work in a more logical fashion ;).
* KFMon only expects to watch for files in the internal storage of the device (i.e., *onboard*). On devices with an external sdcard, watching for files on that external storage is unsupported (it may work, but the code makes a number of assumptions which may not hold true in that case, which could lead to undefined behavior).

* Proper interaction with KOReader in general requires a recent version of KOReader (i.e., >= 2015.11-1735).
  * A far as for successfully restarting nickel on exit is concerned, I'd also recommend a current FW version (last tested on FW 4.7.x - 4.9.x).
* When either KOReader or Plato is launched *through KFMon*, __nothing__ will be allowed to spawn while that document reader is still running. This is to prevent spurious events that may be triggered by their file manager.

* PSA about the proper syntax expected in an INI file: while the ```;``` character indeed marks the beginning of an inline comment, it must be preceded by some kind of whitespace to actually register as a comment. Otherwise, it's assumed to be part of the value.
  * Meaning ```key=value;``` will probably not work as you might expect (it'll parse as ```key``` set to ```value;``` and not ```value```).
* On a related note, a line cannot exceed 200 bytes. If the log reports a parsing error on a seemingly benign line, but one which happens to feature a humonguous amount of inline comments, that may very well be the reason ;).
* If the log reports a parsing error at (or near, depending on commented lines) the top of the config file, check that you haven't forgotten the ```[watch]``` section name ;).

* You will most likely have to reinstall KFMon after a firmware update (since most FW update packages ship the vanilla version of the startup script patched to launch KFMon).

* Right now, KFMon supports a maximum of [16](/kfmon.h#L172) file watches. Ping me if that's not enough for you ;).
