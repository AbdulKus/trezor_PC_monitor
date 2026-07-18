# Security policy

## Supported versions

Only the latest published release receives fixes. This is experimental community firmware and must not be treated as a wallet or a security device.

## Reporting a vulnerability

Please do not publish exploitable details in a public issue. Use GitHub's private vulnerability reporting for this repository when available, or contact the repository owner privately through their GitHub profile. Include affected versions, reproduction steps and impact. Allow reasonable time for validation and a coordinated release.

Do not send seeds, PINs, wallet backups or personal telemetry. If a real wallet was ever used on the test device, move funds using a trusted official setup before experimenting with unofficial firmware.

## Security boundaries

- USB reports, asset packs and project files are treated as untrusted and require bounds validation.
- Shell and PowerShell actions are powerful local features. They are disabled/marked with warnings and should only run from trusted projects.
- Physical firmware confirmations are never automated.
- Release checksums detect accidental corruption; they are not a substitute for reviewing source and build provenance.
