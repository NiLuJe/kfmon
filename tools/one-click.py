#!/usr/bin/env python3.7
# -*- coding:utf-8 -*-
#
# Massage the latest KOReader/Plato releases into "one-click" install bundles.
#
##

from github import Github
import glob
import markdown
from lxml import etree
from pathlib import Path
import requests
import shutil
import sys

# We'll need the current KFMon install package first
kfm = Path("Kobo")
kfmon_package = None
for kfmon in kfm.glob("KFMon-v*-g*.zip"):
	kfmon_package = kfmon.resolve(strict=True)

if kfmon_package is None:
	print("Couldn't find a KFMon install package!")
	sys.exit(-1)

# We'll be doing as much as possible through the GitHub API, unauthenticated
gh = Github()

# Get the latest Plato release
plato = gh.get_repo("baskerville/plato")
latest_plato = plato.get_latest_release()
plato_version = latest_plato.tag_name
# As an added quirk, a release isn't guaranteed to ship a script package, if it doesn't need to.
# So walk backwards through releases until we find one...
plato_main_url = None
plato_scripts_url = None
for release in plato.get_releases():
	version = release.tag_name
	print("Looking at Plato {}".format(version))
	# Plato doesn't actually store releases in assets, so, parse the MD body of the Release Notes instead
	notes = etree.fromstring(markdown.markdown(release.body))
	for link in notes.xpath("//a"):
		# We want both the main fmon package, as well as the launcher scripts
		if plato_main_url is None and link.text == "plato-{}.zip".format(version):
			plato_main_url = link.get("href")
		if plato_scripts_url is None and link.text == "plato-launcher-fmon-{}.zip".format(version):
			plato_scripts_url = link.get("href")
	# If we've got both packages, we're done!
	if plato_main_url is not None and plato_scripts_url is not None:
		break

if plato_main_url is None and plato_scripts_url is None:
	print("Couldn't find the latest Plato packages!")
	sys.exit(-1)
latest_plato = None
plato = None

# Get the latest KOReader release
koreader = gh.get_repo("koreader/koreader")
latest_koreader = koreader.get_latest_release()
koreader_version = latest_koreader.tag_name
koreader_url = None
# Loop over assets until we find the Kobo package ;)
for asset in latest_koreader.get_assets():
	if asset.name == "koreader-kobo-arm-kobo-linux-gnueabihf-{}.zip".format(koreader_version):
		koreader_url = asset.browser_download_url

if koreader_url is None:
	print("Couldn't find the latest KOReader package!")
	sys.exit(-1)
latest_koreader = None
koreader = None

print("KO {}:\n{}\n\nPlato {}:\nMain: {}\nScripts: {}".format(koreader_version, koreader_url, plato_version, plato_main_url, plato_scripts_url))
gh = None

# Let's start building our one-click packages...
# We'll work in a temporary directory, one that's hosted on a tmpfs (at least on my end ;p)...
t = Path("/var/tmp/KFMon")
t.mkdir(parents=True, exist_ok=True)

# Start with Plato
# It'll be staged in its own directory
pl = Path(t / "Plato")

# Download both packages...
pl_main = Path(t / "Plato.zip")
r = requests.get(plato_main_url)
with pl_main.open(mode="w+b") as f:
	f.write(r.content)
pl_scripts = Path(t / "Plato-Scripts.zip")
r = requests.get(plato_scripts_url)
with pl_scripts.open(mode="w+b") as f:
	f.write(r.content)

# Stage KFMon first
shutil.unpack_archive(kfmon_package, pl)
# Filter out KOReader config & icons
Path(pl / ".adds/kfmon/config/koreader.ini").unlink()
Path(pl / "koreader.png").unlink()

# Then stage Plato (start with the scripts, since it'll create the required folders for us)
shutil.unpack_archive(pl_scripts, pl)
shutil.unpack_archive(pl_main, pl / ".adds/plato")

# Finally, zip it up!
shutil.make_archive(t / "Plato-{}.zip".format(plato_version), root_dir=pl, base_dir=".")

# Cleanup behind us
#shutil.rmtree(pl)
pl = None

# Do KOReader next
# It'll be staged in its own directory
ko = Path(t / "KOReader")

# Download the package
ko_main = Path(t / "KOReader.zip")
r = requests.get(koreader_url)
with ko_main.open(mode="w+b") as f:
	f.write(r.content)

# Stage KFMon first
shutil.unpack_archive(kfmon_package, ko)
# Filter out Plato config & icons
Path(ko / ".adds/kfmon/config/plato.ini").unlink()
Path(ko / "icons/plato.png").unlink()

# Then stage KOReader
shutil.unpack_archive(ko_main, ko / ".adds")
# Filter out some extraneous stuff
Path(ko / ".adds" / "koreader.png").unlink()
Path(ko / ".adds" / "README_kobo.txt").unlink()

# Finally, zip it up!
shutil.make_archive(t / "KOReader-{}.zip".format(koreader_version), root_dir=ko, base_dir=".")

# Cleanup behind us
#shutil.rmtree(ko)
ko = None

# And while we're there, I guess we can do both at once ;)
pk = Path(t / "Both")

# Stage KFMon first
shutil.unpack_archive(kfmon_package, pk)
# Then Plato
shutil.unpack_archive(pl_scripts, pk)
shutil.unpack_archive(pl_main, pk / ".adds/plato")
# Then KOReader
shutil.unpack_archive(ko_main, pk / ".adds")
# Filter out some extraneous stuff
Path(pk / ".adds" / "koreader.png").unlink()
Path(pk / ".adds" / "README_kobo.txt").unlink()

# Finally, zip it up!
shutil.make_archive(t / "Plato-{}_KOReader-{}.zip".format(plato_version, koreader_version), root_dir=pk, base_dir=".")

# Cleanup behind us
#shutil.rmtree(pk)
pk = None

# Final cleanup
ko_main.unlink()
pl_scripts.unlink()
pl_main.unlink()

# Print a recap
for ocp in t.glob("*.zip"):
	oneclick_package = ocp.resolve(strict=True)
	print(oneclick_package)
