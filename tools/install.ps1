##
#
# Quick'n dirty helper to pickup a Kobo's USBMS mountpoint...
# Spoiler alert: I've never written anything in PowerShell before :D
# NOTE: Requires PowerShell 5+
#
##

# Find out where the Kobo is mounted...
$KOBO_MOUNTPOINT=$NULL
# FIXME: Filter by Label (KOBOeReader)
$KOBO_MOUNTPOINT=Get-Disk | Where BusType -eq USB | Get-Partition | Get-Volume

# Sanity check...
if (NOT Test-Path "$KOBO_MOUNTPOINT" + "\.kobo") {
	Write-Host("Can't find a .kobo directory, " + $KOBO_MOUNTPOINT + " doesn't appear to point to a Kobo eReader... Is one actually mounted?")
	Exit 1
}

# Ask the user what they want to install...
$AVAILABLE_PKGS=@()
foreach ($file in Get-ChildItem -Recurse KOReader-v*.zip Plato-*.zip KFMon-v*.zip) {
	if (Test-Path $file) {
		$AVAILABLE_PKGS.Add($file)
	}
}

Write-Host("* Here are the available packages:")
for ($i = 0; $i -lt $AVAILABLE_PKGS.Length; $i++) {
	Write-Host($i + ": " + $AVAILABLE_PKGS[$i])
}

$j = Read-Host -Prompt '* Enter the number corresponding to the one you want to install'

# Check if that was a sane reply...
if (NOT $j -is [int]) {
	Write-Host("That wasn't a number!")
	Exit 1
}

if ($j -lt 0 OR $j -ge $AVAILABLE_PKGS.Length) {
	Write-Host("That number was out of range!")
	Exit 1
}

# We've got a Kobo, we've got a package, let's go!
Write-Host("* Installing " + $AVAILABLE_PKGS[$j] + " . . .")
Write-Host("Expand-Archive " + $AVAILABLE_PKGS[$j] + " -DestinationPath " + $KOBO_MOUNTPOINT)
#Expand-Archive $AVAILABLE_PKGS[$j] -DestinationPath $KOBO_MOUNTPOINT
