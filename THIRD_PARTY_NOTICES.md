# Third-party notices

The application source and setup page in this repository are distributed under
Apache-2.0. This project uses Espressif ESP-IDF, ESP-Matter, OpenThread and
connectedhomeip. SDK files and modifications retain their upstream copyright
and license notices. Their source is fetched by the pinned setup script; see
each SDK's LICENSE/NOTICE files for the complete terms.

The included generic firmware incorporates those SDKs and their dependencies.
`dependencies.tested.lock` records the resolved component versions.

`paa_cert/` and `firmware/3-paa_cert.bin` contain publicly distributed Matter PAA
root certificates obtained through the SDK's certificate-generation helper.
These are public trust anchors, not this hub's private keys or commissioned
device credentials. Their publication does not confer ownership of issuer names
or trademarks. See `scripts/prepare-paa.py` for their provenance.

The optional browser installer loads ESP Web Tools 10.1.1 from unpkg.com.
ESP Web Tools is a separate Apache-2.0 project of ESPHome/Open Home Foundation:
https://github.com/esphome/esp-web-tools . Its transitive dependencies retain
their own licenses. The USB pairing wizard itself uses no third-party packages.

IKEA, KAJPLATS, BILRESA, Espressif, ESP32, Matter and Thread identify compatible
products/technologies. This is an independent experimental project.
