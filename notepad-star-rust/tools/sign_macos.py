"""Sign and notarize a candidate using pre-provisioned publisher credentials."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("bundle", type=Path)
args = parser.parse_args()
if sys.platform != "darwin":
    raise SystemExit("Signing/notarization requires a real Mac.")
identity = os.environ.get("NOTEPAD_STAR_MAC_IDENTITY", "")
profile = os.environ.get("NOTEPAD_STAR_NOTARY_PROFILE", "")
if not identity.startswith("Developer ID Application:") or not profile:
    raise SystemExit("Configure a Developer ID Application identity and a notarytool keychain profile.")
bundle = args.bundle.resolve(strict=True)
if bundle.suffix != ".app" or not (bundle / "Contents" / "Info.plist").is_file():
    raise SystemExit("Expected a deployed application bundle.")

def run(*arguments):
    subprocess.run(arguments, check=True)

magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
         b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
         b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
paths = sorted(bundle.rglob("*"), key=lambda path: len(path.parts), reverse=True)
for path in paths:
    if path.is_symlink() or not path.is_file():
        continue
    with path.open("rb") as stream:
        binary = stream.read(4) in magic
    if binary:
        run("codesign", "--force", "--options", "runtime", "--timestamp", "--sign", identity, str(path))
for path in paths:
    if not path.is_symlink() and path.is_dir() and path.suffix in {".framework", ".bundle", ".xpc", ".app"}:
        run("codesign", "--force", "--options", "runtime", "--timestamp", "--sign", identity, str(path))
run("codesign", "--force", "--options", "runtime", "--timestamp", "--sign", identity, str(bundle))
run("codesign", "--verify", "--deep", "--strict", str(bundle))
with tempfile.TemporaryDirectory(prefix="notepad-star-notary-") as directory:
    archive = Path(directory) / "submission.zip"
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(bundle), str(archive))
    response = subprocess.check_output(["xcrun", "notarytool", "submit", str(archive),
        "--keychain-profile", profile, "--wait", "--timeout", "20m", "--output-format", "json"])
    result = json.loads(response)
    if result.get("status") != "Accepted":
        raise SystemExit(f"Notarization was not accepted (submission {result.get('id', 'unknown')}).")
run("xcrun", "stapler", "staple", str(bundle))
run("xcrun", "stapler", "validate", str(bundle))
run("spctl", "--assess", "--type", "execute", "--verbose=2", str(bundle))
print("Publisher signing, notarization and Gatekeeper assessment succeeded.")
