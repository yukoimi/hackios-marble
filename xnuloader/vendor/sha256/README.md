SHA-256 from Brad Conte, B-Con/crypto-algorithms, public domain.
Pinned upstream commit: cfbde48414baacf51fc7c74f275190881f037d32.
Original file hashes/URLs: out/adsp-native-dependencies-v2-plan/references/bcon-sha256-manifest.json.
Local port: conditional libc headers for EFI; cast input bytes to unsigned WORD before shifts, avoiding signed-promotion overflow.
Used only for public native instruction fingerprints, with host SHA256 comparison and known-answer tests. No secrets or authentication protocol use.
