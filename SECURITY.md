# Security Policy

## Supported Versions

Security fixes are applied to the latest release only. We do not backport
fixes to older versions.

| Version | Supported |
|---------|-----------|
| 1.15+ | ✅ |
| <1.15 | ❌ |

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Use GitHub's private vulnerability reporting instead:
1. Go to the **Security** tab of this repository
2. Click **Report a vulnerability**
3. Fill in the details and submit

### What to include

A useful report tells us:
- Which component or file is affected
- What an attacker can do (impact) and under what conditions
- A minimal reproduction case or proof-of-concept if you have one
- Whether you believe it is remotely exploitable

You do not need a working exploit to report. An incomplete report is better
than no report.

## What to expect

This is a volunteer-maintained open-source project. We will do our best to
respond in a reasonable timeframe, but cannot commit to specific deadlines.

We ask that you give us a fair opportunity to investigate and address the
issue before any public disclosure. If you have not heard back after
**90 days**, feel free to follow up or proceed with disclosure at your
discretion.

## Scope

In scope:
- Remote code execution, memory corruption, or denial-of-service via crafted
  radio packets
- Authentication or encryption bypasses
- Vulnerabilities in the packet routing or path handling logic

Out of scope:
- Physical access attacks (e.g., JTAG, UART extraction of keys)
- Regulatory compliance (duty cycle, frequency restrictions)
- Jamming or other physical-layer radio interference
- Issues in third-party libraries (RadioLib, Crypto, etc.) — report those
  upstream
- "Best practice" suggestions without a demonstrated attack path
