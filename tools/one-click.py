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
# Plato doesn't actually store releases in assets, so, parse the body of the Release Notes instead
plato_notes = etree.fromstring(markdown.markdown(latest_plato.body))
for link in plato_notes.xpath("//a"):
	# We want both the main fmon package, as well as the launcher scripts
	if link.text == "plato-{}.zip".format(plato_version):
		plato_main_url = link.get("href")
	if link.text == "plato-launcher-fmon-{}.zip".format(plato_version):
		plato_scripts_url = link.get("href")

# Get the latest KOReader release
koreader = g.get_repo("koreader/koreader")
latest_koreader = koreader.get_latest_release()
koreader_version = latest_koreader.tag_name
# Loop over assets until we find the Kobo package ;)
for asset in latest_koreader.get_assets():
	if asset.name == "koreader-kobo-arm-kobo-linux-gnueabihf-{}.zip".format(koreader_version):
		koreader_url = asset.browser_download_url

print("KO {}:\n{}\n\nPlato {}:\nMain: {}\nScripts: {}".format(koreader_version, koreader_url, plato_version, plato_main_url, plato_scripts_url))
