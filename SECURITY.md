# Security

## Reporting a bug in Lode

Report security bugs in Lode via a private GitHub Security Advisory at
<https://github.com/lode-luau/LodeRuntime/security/advisories/new>.

Reports are reviewed by the maintainers. After the initial reply, the
maintainers will endeavor to keep you informed of the progress being made
towards a fix and full announcement, and may ask for additional information
or guidance surrounding the reported issue.

The Lode project does not have a bug bounty program.

## Reporting a bug in a third-party module

Security bugs in third-party modules (for example community native modules)
should be reported to their respective maintainers. They are not part of the
Lode runtime and follow their own release cycles.

## Disclosure policy

Here is the security disclosure policy for Lode:

- The security report is received and assigned to a maintainer. The problem is
  validated against the supported platforms. Once confirmed, the code is
  audited to find any potential similar problems.
- A fix is prepared and validated through the regular validation flow: Debug
  and Release builds of the runtime plus the sanity regression suite.
- The fix is merged to `main` and included in the next release. A GitHub
  Security Advisory describing the issue and the affected versions is
  published once the fix is available.

## Vulnerability reporting guidelines

When reporting security vulnerabilities, reporters must adhere to the
following guidelines:

1. **No Harmful Actions**: Security research and vulnerability reporting must
   not:
   * Cause damage to running systems or production environments.
   * Disrupt Lode development or infrastructure.
   * Affect other users' applications or systems.
   * Include actual exploits that could harm users.

2. **Responsible Testing**: When testing potential vulnerabilities:
   * Use isolated, controlled environments.
   * Do not test on production systems without prior authorization.
   * Do not attempt to access or modify other users' data.

3. **Report Quality**:
   * Provide clear, detailed steps to reproduce the vulnerability.
   * Include reproducible code written in Luau (and C++ when the issue
     involves a native module).
   * Include only the minimum proof of concept required to demonstrate the
     issue.
   * Remove any malicious payloads or components that could cause harm.

## The Lode threat model

Lode is a scripting runtime in the same category as Node.js and Python. In
the Lode threat model there are trusted elements such as the underlying
operating system and the code Lode is asked to run. Vulnerabilities that
require the compromise of these trusted elements are outside the scope of the
Lode threat model.

**Lode does NOT trust**:

* Data received from the remote end of inbound network connections that are
  accepted through the use of Lode APIs (for example the `http`, `socket`, and
  `websocket` modules) and which is transformed/validated by Lode before being
  passed to the application.
* The file content or other I/O data that is opened for reading or writing
  through the use of Lode APIs (for example `stdio` and `filesystem` module
  calls). The paths are trusted; the content is not.

In other words, if data passing through Lode to/from the application can
trigger actions other than those documented for the APIs, there is likely a
security vulnerability. Examples of unwanted actions are polluting globals,
causing an unrecoverable crash, or any other unexpected side effects that can
lead to a loss of confidentiality, integrity, or availability.

**Lode trusts everything else**. Examples include:

* The developers and infrastructure that run it.
* The operating system that Lode is running under and its configuration,
  along with anything under the control of the operating system.
* The code it is asked to run, including Luau scripts and native modules,
  even if said code is dynamically loaded, e.g. community modules installed
  as shared libraries. The code run inherits all the privileges of the
  execution user.
* Inputs provided to it by the code it is asked to run, as it is the
  responsibility of the application to perform the required input
  validations.
* The file system when requiring a module. `require` resolves paths relative
  to the requirer and may traverse the file system; this is documented
  behavior and not a security boundary. The only constraint enforced by the
  runtime is that a `lode.json` manifest may only reference native library
  files inside its own module directory. That check is evaluated on the
  canonical path at load time and is a packaging-correctness constraint, not a
  security boundary against a hostile file system. In addition, a native
  library is only accepted as a module when it exports the `LodeModuleInit`
  entrypoint; this is an interface contract, not a security boundary, since
  the library's code runs with the privileges of the process.
* The execution path. Lode path handling functions trust their input; reports
  about issues related to these functions that rely on unsanitized input are
  not considered vulnerabilities, as it is the user's responsibility to
  sanitize path inputs according to their security requirements.

### What constitutes a vulnerability

Being able to cause the following through control of the elements that Lode
does not trust is considered a vulnerability:

* Disclosure or loss of integrity or confidentiality of data protected
  through the correct use of Lode APIs.
* The unavailability of the runtime, including the unbounded degradation of
  its performance.
* An unrecoverable crash of the runtime (for example memory corruption or a
  use-after-free in the runtime or in the native C API boundary) reachable
  through correct use of the APIs with untrusted data.

If Lode loads configuration files or runs code by default (without a specific
request from the user), and this is not documented, it is considered a
vulnerability. Vulnerabilities related to this case may be fixed by a
documentation update.

#### Denial of Service (DoS) vulnerabilities

For a behavior to be considered a DoS vulnerability, the PoC must meet the
following criteria:

* The API is being correctly used.
* The API is public and documented.
* The behavior is significant enough to cause a denial of service quickly or
  in a context not controlled by the application developer (for example
  network parsing).
* The behavior is directly exploitable by an untrusted source without
  requiring application mistakes.
* The attack demonstrates asymmetric resource consumption, where the attacker
  expends significantly fewer resources than what is required by the runtime
  to process the attack.

### Examples of vulnerabilities

* Buffer out-of-bounds reads or writes reachable through correct use of the
  public buffer APIs.
* A crash of the process caused by malformed data received from a network
  peer through the `http`, `socket`, or `websocket` modules.
* Undocumented automatic loading of a configuration file that changes the
  behavior of the runtime.

### Examples of non-vulnerabilities

#### Malicious third-party modules (CWE-1357)

* Code is trusted by Lode. Native modules are trusted-by-design, as are Luau
  packages. Therefore any scenario that requires a malicious third-party
  module cannot result in a vulnerability in Lode.
* This includes the absence of a sandbox for native modules: the runtime does
  not isolate the code it runs. If a security boundary around untrusted code
  is required, use OS-level isolation, such as separate users, containers, or
  platform sandboxes.

#### Path traversal in require (CWE-427)

* Lode trusts the file system in the environment accessible to it. Therefore
  it is not a vulnerability if `require` accesses or loads modules from any
  path that is accessible to it.

#### Absence of a permission model

* A permission model would be an opt-in mechanism to reduce the blast radius
  of mistakes in trusted application code. It is not implemented, and its
  absence is not a vulnerability. Lode is not designed to run untrusted code.

#### User input sanitization (CWE-20)

* Lode trusts the inputs provided to it by application code. It is up to the
  application to sanitize appropriately. Therefore any scenario that requires
  control over user input is not considered a vulnerability.

#### Exposing Application-Level APIs to Untrusted Users (CWE-653)

* Lode trusts the application code that uses its APIs. When application code
  exposes Lode functionality to untrusted users in an unsafe manner (for
  example passing unsanitized file paths to `filesystem` functions or
  untrusted strings into shell commands), any resulting issues are not
  considered vulnerabilities in Lode itself.

#### Uncontrolled Resource Consumption (CWE-400) on outbound connections

* If Lode is asked to connect to a remote site and return an artifact, it is
  not considered a vulnerability if the size of that artifact is large enough
  to impact performance or cause the runtime to run out of resources.

#### The `lode.json` native library path constraint

* The check that `lode.json` library paths remain inside the module directory
  is enforced on the canonical path at load time. Reports that depend on the
  attacker already having control over the module directory or the file
  system are not considered vulnerabilities: at that point the attacker
  controls the module content itself, which is a trusted element.

## Receiving security updates

Security fixes are released through the regular release process. Advisories
are published on GitHub at
<https://github.com/lode-luau/LodeRuntime/security/advisories>.
