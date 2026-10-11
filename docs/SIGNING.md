# Signed Windows releases

Official Windows releases use the same local SSL.com CodeSignTool workflow as
Elka VoiceMeeter FX Host. Account details and signing helpers stay outside Git.
Passwords and phone codes must never be saved in the repository or build logs.

1. Build and test with Visual Studio 2026: `tools/build.ps1 -Package`.
2. Prepare an Inno signed-uninstaller cache using
   `tools/package-installer.ps1 -SignedUninstallerDirectory <local-cache>`.
   The first compile creates the uninstaller and stops until it is signed.
3. Malware-scan and batch-sign the plugin DLL and generated uninstaller together.
   Verify the publisher, Authenticode signature and timestamp of both files.
4. Repackage the signed DLL without rebuilding it. Compile setup with the same
   signed-uninstaller cache, then scan/sign the completed setup EXE separately.
   Setup must contain the signed DLL and signed uninstaller before setup is signed.
5. Run `tools/test-installer.ps1 -RequireSigned`. This checks installed payloads
   and signatures in isolated standard and portable OBS roots.
6. Verify every released EXE/DLL and regenerate SHA-256 checksums after signing.
   Upload only these final bytes and compare the downloaded assets to the hashes.

Two phone approvals cover the payload/uninstaller batch and the final installer.
Text documents, images and ZIP containers use SHA-256 checksums; their contained
Windows executable code carries the Authenticode signatures.

macOS releases use a universal plugin with an ad-hoc loading signature. The PKG
is unsigned; neither package nor plugin is Apple Developer ID signed or notarized.
The stable/development release channel is separate from code-signing status.
