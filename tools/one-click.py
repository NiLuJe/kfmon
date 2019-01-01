#!/usr/bin/env python3
# -*- coding:utf-8 -*-
#
# Massage the latest KOReader/Plato releases into "one-click" install bundles.
#
##

from github import Github
import markdown
from lxml import etree
import requests
import shutil

# We'll be doing as much as possibly rhough the GitHub API, unauthenticated
g = Github()

# Get the latest Plato release
plato = g.get_repo("baskerville/plato")
latest_plato = plato.get_latest_release()
plato_version = latest_plato.tag_name
# As an added quirk, a release isn't guaranteed to ship a script package, if it doesn't need to.
# So walk backwards through releases until we find one...
plato_main_url = None
plato_scripts_url = None
for release in plato.get_releases():
	version = release.tag_name
	print("Looking at Plato {}".format(version))
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

# Get the latest KOReader release
koreader = g.get_repo("koreader/koreader")
latest_koreader = koreader.get_latest_release()
koreader_version = latest_koreader.tag_name
# Loop over assets until we find the Kobo package ;)
for asset in latest_koreader.get_assets():
	if asset.name == "koreader-kobo-arm-kobo-linux-gnueabihf-{}.zip".format(koreader_version):
		koreader_url = asset.browser_download_url

print("KO {}:\n{}\n\nPlato {}:\nMain: {}\nScripts: {}".format(koreader_version, koreader_url, plato_version, plato_main_url, plato_scripts_url))
