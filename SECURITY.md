# Security Guarantees

Luwu provides a safe sandbox that scripts can not escape from, short of vulnerabilities in custom C functions exposed by the host. This includes the virtual machine, builtin libraries and native code generation facilities.

Any source code can not result in memory safety errors or crashes during its compilation or execution. Violations of memory safety are considered vulnerabilities.

Note that Luwu does not provide termination guarantees - some code may exhaust CPU or RAM resources on the system during compilation or execution.

The runtime expects valid bytecode as an input. Feeding bytecode that was not produced by the Luwu compiler into the VM is not supported, and
doesn't come with any security guarantees; make sure to sign and/or encrypt the bytecode when it crosses a network or file system boundary to avoid tampering.

# Reporting a Vulnerability

Report memory safety vulnerabilities privately through this repository's
[security advisories](https://github.com/luwu-community/luwu/security/advisories/new), never in a public issue. That
covers anything a script can abuse, like an exploitable buffer overflow. Crashes (`SIGSEGV`, `SIGILL`, ...) are public
issues. [BUGS.md](BUGS.md) lists how serious each kind of bug is.
