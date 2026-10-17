# Security and privacy boundaries

Buddy's current production firmware has no cloud accounts, telemetry or remote update service. Voice recognition is local and limited; the microphone is off outside listening windows or when Sound is OFF. The firmware does not archive conversations.

SD update packages include integrity hashes, but are not signed. Someone with physical access and a malicious firmware package can change the application. Use only trusted packages. Cable Friends is an experimental, manually entered physical transport, not authenticated USB or a security-certified link. Follow the electrical rules in [docs/HARDWARE.md](docs/HARDWARE.md).

Do not post live flash backups, NVS dumps, full SD archives or private audio in public issues. Use GitHub private vulnerability reporting where enabled; no personal contact address is embedded in this repository. Do not claim encryption, authenticated updates or certification that the implementation does not provide.
