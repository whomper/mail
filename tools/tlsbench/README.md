# TLS on the Falcon: measurements

Can the Falcon do TLS itself, without the Raspberry Pi? These two programs
time the expensive parts of a TLS handshake on the machine itself.

- `TLSB030.PRG`: BearSSL 0.6 compiled for the 68030 times X25519, P-256,
  ECDSA and RSA-2048 signature checks, SHA-256, AES-GCM and
  ChaCha20-Poly1305.
- `TLSDSP.PRG`: Montgomery multiplication (the core of RSA and elliptic
  curves) on the DSP56001, for 2048-bit and 256-bit numbers. It checks the
  DSP's answers against values worked out in Python (`gen.py`).

Each shows its results and saves them to `TLSBENCH.TXT` / `TLSDSP.TXT`.
Run `build.sh` to rebuild them (needs `a56`, see the script).

## Emulated Falcon (Hatari, 68030 16 MHz, cycle-exact; DSP emulated)

| | 68030 (BearSSL C) | DSP56001 |
|---|---|---|
| RSA-2048 signature check | 2515 ms | about 60 ms (19 x 3.15 ms) |
| X25519 | 1070 ms | about 180 ms (estimate: 2550 x 72 us) |
| ECDSA P-256 check | 7975 ms | not measured |
| Handshake, 2 x X25519 + 3 RSA checks | about 9.6 s | about 0.5 s |
| ChaCha20-Poly1305 / AES-128-GCM | 39 / 16 KB/s | |

Real hardware may differ: run both programs on a real Falcon.
