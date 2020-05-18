#!/usr/bin/env python3
# -*- coding:utf-8 -*-
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Massage the latest KOReader/Plato releases into "one-click" install bundles.
#
##

import sys

# We need Python >= 3.7 (as we're passing a pathlib.Path to shutil.make_archive)
if sys.version_info < (3, 7):
	raise SystemExit("This script requires Python 3.7+")

from bs4 import BeautifulSoup
from email.utils import parsedate
from github import Github
from io import DEFAULT_BUFFER_SIZE
import logging
from natsort import natsorted
import os
from pathlib import Path
import requests
import shutil
from tempfile import gettempdir
from time import mktime
from tqdm import tqdm

# Set up a logger for shutil.make_archive
logging.basicConfig(level=logging.WARNING)
logger = logging.getLogger("KFMon")

# We'll need the current KFMon install package first
print("* Looking for the latest KFMon install package . . .")
kfm = Path("Kobo")
kfmon_package = None
# There *should* only ever be one, but assume I might do something stupid later down the road...
for kfmon in kfm.glob("KFMon-v*.zip"):
	print("* Found {}".format(kfmon.name))
	kfmon_package = kfmon.resolve(strict=True)
	# Remember its mtime
	kfmon_date = kfmon.stat().st_mtime

if kfmon_package is None:
	raise SystemExit("Couldn't find a KFMon install package!")

# We'll be doing as much as possible through the GitHub API, possibly authenticated
gh = Github(os.getenv("GH_API_ACCESS_TOK"))

# Get the latest NickelMenu release
print("* Looking for the latest NickelMenu release . . .")
nickelmenu = gh.get_repo("geek1011/NickelMenu")
latest_nickelmenu = nickelmenu.get_latest_release()
nickelmenu_version = latest_nickelmenu.tag_name
nickelmenu_url = None
print("Looking at NickelMenu {} ...".format(nickelmenu_version))
# Loop over assets until we find the KoboRoot tarball ;)
for asset in latest_nickelmenu.get_assets():
	if asset.name == "KoboRoot.tgz":
		nickelmenu_url = asset.browser_download_url
		break

if nickelmenu_url is None:
	raise SystemExit("Couldn't find the latest NickelMenu package!")
latest_nickelmenu = None
nickelmenu = None

# Get the latest Plato release
print("* Looking for the latest Plato release . . .")
plato = gh.get_repo("baskerville/plato")
latest_plato = plato.get_latest_release()
plato_version = latest_plato.tag_name
plato_url = None
print("Looking at Plato {} ...".format(plato_version))
for asset in latest_plato.get_assets():
	if asset.name == "plato-{}.zip".format(plato_version):
		plato_url = asset.browser_download_url
		break

if plato_url is None:
	raise SystemExit("Couldn't find the latest Plato package!")
latest_plato = None
plato = None

# Get the latest KOReader release
print("* Looking for the latest KOReader release . . .")
koreader = gh.get_repo("koreader/koreader")
latest_koreader = koreader.get_latest_release()
# Try to pickup a hotfix release if there's one...
latest_ko_tag = koreader.get_tags()[0].name
koreader_version = latest_koreader.tag_name
koreader_url = None
# Loop over assets until we find the Kobo package ;)
for asset in latest_koreader.get_assets():
	if asset.name == "koreader-kobo-{}.zip".format(koreader_version):
		koreader_url = asset.browser_download_url
		break
	elif asset.name == "koreader-kobo-{}.zip".format(latest_ko_tag):
		koreader_url = asset.browser_download_url
		koreader_version = latest_ko_tag
		break

if koreader_url is None:
	raise SystemExit("Couldn't find the latest KOReader package!")
latest_koreader = None
koreader = None

# Get the latest KOReader nightly
print("* Looking for the latest KOReader nightly . . .")
koreader_nightly_version = None
# NOTE: We're opting for a crawl of the nighlies, instead of parsing the koreader-kobo-latest-nightly.zsync file...
koreader_nightly_url = "http://build.koreader.rocks/download/nightly/"
r = requests.get(koreader_nightly_url)
if r.status_code != 200:
	raise SystemExit("Couldn't crawl KOReader's nightlies!")
# That's a simple directory listing, so we'll have to scrape it...
soup = BeautifulSoup(r.text, "lxml")
# We're of course concerned with the links
ko_nightlies = []
for link in soup.find_all("a"):
	# We want the link, minus the final /
	if link.get("href") != "../":
		ko_nightlies.append(link.get("href")[:-1])
# Sort that to find the latest one, but we'll walk them all backwards until we find one that contains a Kobo build,
# in case the latest nightlies were only cooked for a subset of platforms...
for nightly in natsorted(ko_nightlies, key=lambda x: x.replace('.', '~'), reverse=True):
	print("Looking at KOReader {} ...".format(nightly))
	r = requests.get(koreader_nightly_url + nightly)
	if r.status_code != 200:
		raise SystemExit("Couldn't crawl KOReader's nightly!")
	# And look for a Kobo build in there...
	soup = BeautifulSoup(r.text, "lxml")
	for link in soup.find_all("a"):
		if "koreader-kobo" in link.get("href"):
			koreader_nightly_version = nightly
			break
	# If we found a Kobo nightly, we're done!
	if koreader_nightly_version:
		break
if koreader_nightly_version is None:
	raise SystemExit("Couldn't find the latest KOReader nightly!")
soup = None
# We can build the proper URL!
koreader_nightly_url = "{}{}/koreader-kobo-{}.zip".format(koreader_nightly_url, koreader_nightly_version, koreader_nightly_version)
# We'll want to tame down the version...
koreader_nightly_version = koreader_nightly_version.split("-g")[0]

# Recap what we found
print("\nNickelMenu: {}\n{}\n\nKOReader Release {}:\n{}\nKOReader Nightly {}:\n{}\n\nPlato {}:\n{}\n".format(nickelmenu_version, nickelmenu_url, koreader_version, koreader_url, koreader_nightly_version, koreader_nightly_url, plato_version, plato_url))
gh = None

# Do we want to use KOReader stable or nightly?
if len(sys.argv) > 1:
	print("* Using the latest nightly instead of the latest release for KOReader!\n")
	koreader_version = koreader_nightly_version
	koreader_url = koreader_nightly_url

# Let's start building our one-click packages...
# We'll work in a temporary directory, one that's hosted on a tmpfs (at least on my end ;p)...
tmpdir = Path(gettempdir())
t = Path(tmpdir / "KFMon")
t.mkdir(parents=True, exist_ok=True)

# Start with NickelMenu, because we'll merge it into KFMon's KoboRoot
print("* Downloading NickelMenu")
nm = Path(t / "NickelMenu")
nm.mkdir(parents=True, exist_ok=True)
nm_main = Path(nm / "NickelMenu.tgz")
with requests.get(nickelmenu_url, stream=True) as r:
	if r.status_code != 200:
		raise SystemExit("Couldn't download the latest NickelMenu release!")
	# We'll restore its mtime later...
	nickelmenu_date = mktime(parsedate(r.headers["Last-Modified"]))
	clen = int(r.headers.get("Content-Length", 0))
	wrote = 0
	with nm_main.open(mode="w+b") as f:
		with tqdm(total=clen, unit='B', unit_scale=True, unit_divisor=1024) as pbar:
			for data in r.iter_content(chunk_size=DEFAULT_BUFFER_SIZE):
				written = f.write(data)
				wrote += written
				pbar.update(written)
	if clen != 0 and wrote != clen:
		raise SystemExit("Wrote {} bytes to disk instead of the {} expected!".format(wrote, clen))

# Unpack both KFMon & NM
print("* Merging NickelMenu with KFMon")
shutil.unpack_archive(kfmon_package, nm)
# Merge both KoboRoot tarballs...
nm_kobo = Path(nm / "KOBOROOT")
shutil.unpack_archive(nm / ".kobo/KoboRoot.tgz", nm_kobo)
shutil.unpack_archive(nm_main, nm_kobo)
merged_koboroot = Path(nm / "KoboRoot")
shutil.make_archive(merged_koboroot, format="gztar", root_dir=nm_kobo, base_dir=".", owner="root", group="root", logger=logger)
# Update to the actual filename created by make_archive
merged_koboroot = merged_koboroot.with_name("KoboRoot.tar.gz")

# Where the NickelMenu config shards live
nm_cfg = Path("nm")

# Start with Plato
print("\n* Creating a one-click package for Plato . . .")
# It'll be staged in its own directory
pl = Path(t / "Plato")

# Download both packages...
print("* Downloading original package")
pl_main = Path(t / "Plato.zip")
with requests.get(plato_url, stream=True) as r:
	if r.status_code != 200:
		raise SystemExit("Couldn't download the latest Plato release!")
	# We'll restore its mtime later...
	plato_date = mktime(parsedate(r.headers["Last-Modified"]))
	clen = int(r.headers.get("Content-Length", 0))
	wrote = 0
	with pl_main.open(mode="w+b") as f:
		with tqdm(total=clen, unit='B', unit_scale=True, unit_divisor=1024) as pbar:
			for data in r.iter_content(chunk_size=DEFAULT_BUFFER_SIZE):
				written = f.write(data)
				wrote += written
				pbar.update(written)
	if clen != 0 and wrote != clen:
		raise SystemExit("Wrote {} bytes to disk instead of the {} expected!".format(wrote, clen))

# Stage KFMon first
print("* Staging it . . .")
shutil.unpack_archive(kfmon_package, pl)
# Filter out KOReader config & icons
Path(pl / ".adds/kfmon/config/koreader.ini").unlink()
Path(pl / "koreader.png").unlink()
# Use the KFMon + NM KoboRoot
shutil.copyfile(merged_koboroot, pl / ".kobo/KoboRoot.tgz")
# Add the relevant NM configs
nm_dir = Path(pl / ".adds/nm")
nm_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(nm_cfg / "kfmon", nm_dir / "kfmon")
shutil.copy2(nm_cfg / "plato", nm_dir / "plato")

# Then stage Plato
pl_dir = Path(pl / ".adds/plato")
pl_dir.mkdir(parents=True, exist_ok=True)
shutil.unpack_archive(pl_main, pl_dir)

# Finally, zip it up!
print("* Bundling it . . .")
pl_basename = "Plato-{}".format(plato_version)
pl_zip = Path(t / pl_basename)
shutil.make_archive(pl_zip, format="zip", root_dir=pl, base_dir=".", logger=logger)
# And restore Plato's original mtime...
pl_zip = pl_zip.with_name("{}.zip".format(pl_basename))
os.utime(pl_zip, times=(plato_date, plato_date))

# Cleanup behind us
shutil.rmtree(pl)
pl = None

# Do KOReader next
print("\n* Creating a one-click package for KOReader . . .")
# It'll be staged in its own directory
ko = Path(t / "KOReader")

# Download the package
print("* Downloading original package")
ko_main = Path(t / "KOReader.zip")
with requests.get(koreader_url, stream=True) as r:
	if r.status_code != 200:
		raise SystemExit("Couldn't download the latest KOReader release!")
	# We'll restore its mtime later...
	koreader_date = mktime(parsedate(r.headers["Last-Modified"]))
	clen = int(r.headers.get("Content-Length", 0))
	wrote = 0
	with ko_main.open(mode="w+b") as f:
		with tqdm(total=clen, unit='B', unit_scale=True, unit_divisor=1024) as pbar:
			for data in r.iter_content(chunk_size=DEFAULT_BUFFER_SIZE):
				written = f.write(data)
				wrote += written
				pbar.update(written)
	if clen != 0 and wrote != clen:
		raise SystemExit("Wrote {} bytes to disk instead of the {} expected!".format(wrote, clen))

# Stage KFMon first
print("* Staging it . . .")
shutil.unpack_archive(kfmon_package, ko)
# Filter out Plato config & icons
Path(ko / ".adds/kfmon/config/plato.ini").unlink()
Path(ko / "icons/plato.png").unlink()
Path(ko / "icons").rmdir()
# Use the KFMon + NM KoboRoot
shutil.copyfile(merged_koboroot, ko / ".kobo/KoboRoot.tgz")
# Add the relevant NM configs
nm_dir = Path(ko / ".adds/nm")
nm_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(nm_cfg / "kfmon", nm_dir / "kfmon")
shutil.copy2(nm_cfg / "koreader", nm_dir / "koreader")

# Then stage KOReader
shutil.unpack_archive(ko_main, ko / ".adds")
# Filter out some extraneous stuff (old fmon relics)
Path(ko / ".adds" / "koreader.png").unlink()
Path(ko / ".adds" / "README_kobo.txt").unlink()

# Finally, zip it up!
print("* Bundling it . . .")
ko_basename = "KOReader-{}".format(koreader_version)
ko_zip = Path(t / ko_basename)
shutil.make_archive(ko_zip, format="zip", root_dir=ko, base_dir=".", logger=logger)
# And restore KOReader's original mtime...
ko_zip = ko_zip.with_name("{}.zip".format(ko_basename))
os.utime(ko_zip, times=(koreader_date, koreader_date))

# Cleanup behind us
shutil.rmtree(ko)
ko = None

# And while we're there, I guess we can do both at once ;)
print("\n* Creating a one-click package for Plato + KOReader . . .")
pk = Path(t / "Both")

# Stage KFMon first
print("* Staging it . . .")
shutil.unpack_archive(kfmon_package, pk)
# Use the KFMon + NM KoboRoot
shutil.copyfile(merged_koboroot, pk / ".kobo/KoboRoot.tgz")
# Add the relevant NM configs
nm_dir = Path(pk / ".adds/nm")
nm_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(nm_cfg / "kfmon", nm_dir / "kfmon")
shutil.copy2(nm_cfg / "koreader", nm_dir / "koreader")
shutil.copy2(nm_cfg / "plato", nm_dir / "plato")
# Then Plato
pl_dir = Path(pk / ".adds/plato")
pl_dir.mkdir(parents=True, exist_ok=True)
shutil.unpack_archive(pl_main, pl_dir)
# Then KOReader
shutil.unpack_archive(ko_main, pk / ".adds")
# Filter out some extraneous stuff
Path(pk / ".adds" / "koreader.png").unlink()
Path(pk / ".adds" / "README_kobo.txt").unlink()

# Finally, zip it up!
print("* Bundling it . . .")
pk_basename = "Plato-{}_KOReader-{}".format(plato_version, koreader_version)
pk_zip = Path(t / pk_basename)
shutil.make_archive(pk_zip, format="zip", root_dir=pk, base_dir=".", logger=logger)
# And restore KFMon's original mtime...
pk_zip = pk_zip.with_name("{}.zip".format(pk_basename))
os.utime(pk_zip, times=(kfmon_date, kfmon_date))

# Cleanup behind us
shutil.rmtree(pk)
pk = None
shutil.rmtree(nm)
nm = None

# Final cleanup
ko_main.unlink()
pl_main.unlink()

# Print a recap
print("\n* Here are the packages we created:\n")
for ocp in t.glob("*.zip"):
	oneclick_package = ocp.resolve(strict=True)
	print(oneclick_package)
