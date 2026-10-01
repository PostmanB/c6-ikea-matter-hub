"""Build an offline production PAA store from CSA MainNet approved roots.

Run on the setup computer only; firmware never accesses the DCL.
"""
import argparse
import datetime
import hashlib
import json
import re
import urllib.parse
import urllib.request
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

BASE = "https://on.dcl.csa-iot.org"
CD_SKID = "97:E4:69:D0:C5:04:14:C2:6F:C7:01:F7:7E:94:77:39:09:8D:F6:A5"

def get(path):
    req = urllib.request.Request(BASE + path, headers={"User-Agent": "c6-local-hub-setup/1"})
    with urllib.request.urlopen(req, timeout=45) as response:
        return json.load(response)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Do not mix a previous trust snapshot into a new one.
    if any(args.output.iterdir()):
        raise SystemExit("Output must be empty; preserve an existing snapshot and use another directory")
    roots = []
    path = "/dcl/pki/root-certificates?pagination.limit=500"
    while path:
        data = get(path)
        roots.extend(data["approvedRootCertificates"]["certs"])
        key = data.get("pagination", {}).get("next_key") or data.get("pagination", {}).get("nextKey")
        path = "/dcl/pki/root-certificates?pagination.limit=500&pagination.key=" + urllib.parse.quote(key, safe="") if key else None
    manifest = {"source": BASE, "retrieved_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "certificates": []}
    for root in roots:
        if root["subjectKeyId"].upper() == CD_SKID:
            continue
        path = "/dcl/pki/certificates/" + urllib.parse.quote(root["subject"], safe="") + "/" + urllib.parse.quote(root["subjectKeyId"], safe="")
        for item in get(path)["approvedCertificates"]["certs"]:
            cert = x509.load_pem_x509_certificate(item["pemCert"].encode())
            subject = cert.subject.rfc4514_string()
            if re.search(r"\b(test|development|non[- ]production)\b", subject, re.I):
                continue
            if cert.subject != cert.issuer or not cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
                raise RuntimeError("DCL entry is not a root CA: " + subject)
            cert.public_key().verify(cert.signature, cert.tbs_certificate_bytes, ec.ECDSA(cert.signature_hash_algorithm))
            der = cert.public_bytes(serialization.Encoding.DER)
            digest = hashlib.sha256(der).hexdigest()
            # SPIFFS names must fit its short name limit.
            name = digest[:24] + ".der"
            (args.output / name).write_bytes(der)
            manifest["certificates"].append({"file": name, "sha256": digest, "subject": subject})
    if not manifest["certificates"]:
        raise RuntimeError("No production PAA roots obtained")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Saved {len(manifest['certificates'])} production roots")

if __name__ == "__main__":
    main()
