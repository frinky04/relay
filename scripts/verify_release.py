"""Verify the tested Velopack files before making a release public."""
import hashlib
import json
import pathlib
import sys
import xml.etree.ElementTree as ET
import zipfile

directory = pathlib.Path(sys.argv[1])
version, commit = sys.argv[2:4]
package = f"frinky04.Relay-{version}-full.nupkg"
required = {
    "frinky04.Relay-win-Setup.exe", "frinky04.Relay-win-Portable.zip", package,
    "releases.win.json", "RELEASES", "assets.win.json",
}
info = json.loads((directory / "build-info.json").read_text())
assert info == {"version": version, "commit": commit}, "Build does not match the tag"
checked = set()
for line in (directory / "SHA256SUMS").read_text().splitlines():
    digest, name = line.split("  ", 1)
    assert name in required and name not in checked, "Unexpected checksum entry"
    assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == digest, f"Hash mismatch: {name}"
    checked.add(name)
assert checked == required, "Missing release files"

assets = json.loads((directory / "releases.win.json").read_text())["Assets"]
assert len(assets) == 1, "Expected one full update package"
asset = assets[0]
assert (asset["PackageId"], asset["Version"], asset["Type"], asset["FileName"]) == (
    "frinky04.Relay", version, "Full", package), "Feed does not match the release"
data = (directory / package).read_bytes()
assert asset["SHA256"].lower() == hashlib.sha256(data).hexdigest(), "Feed checksum mismatch"
assert asset["Size"] == len(data), "Feed size mismatch"

with zipfile.ZipFile(directory / "frinky04.Relay-win-Portable.zip") as archive:
    names = {name.lower(): name for name in archive.namelist()}
    expected = {
        ".portable", "relay.exe", "update.exe", "current/relay.exe", "current/sq.version",
        "current/version.txt", "current/license", "current/third_party_notices.md",
        "current/velopack_libc.dll", "current/vcruntime140.dll", "current/msvcp140.dll",
        "current/assets/notosansmono-medium.ttf", "current/assets/ofl.txt", "current/assets/relay.ico",
        "current/plugins/calc.lua", "current/plugins/web.lua",
        "current/licenses/imgui.txt", "current/licenses/lua.txt", "current/licenses/sol2.txt",
        "current/licenses/velopack.txt",
    }
    assert expected <= names.keys(), f"Missing portable files: {expected - names.keys()}"
    for name in names:
        parts = pathlib.PurePosixPath(name).parts
        assert not {"docs", "tests", ".git", ".."}.intersection(parts), "Private/development file in release"
        assert not name.endswith("_test.exe"), "Test executable in release"
    assert archive.read(names["current/version.txt"]).decode().strip() == version, "Binary version mismatch"
    metadata = ET.fromstring(archive.read(names["current/sq.version"]))
    assert metadata.findtext(".//{*}version") == version, "Velopack version mismatch"
    assert metadata.findtext(".//{*}id") == "frinky04.Relay", "Unexpected package identity"
print(f"Verified Relay {version}: installer, portable ZIP, update feed, package, checksums")
