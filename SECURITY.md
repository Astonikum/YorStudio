# Security policy

Do not open public issues for security vulnerabilities. Report them privately
through GitHub's security advisory workflow for `Astonikum/YorStudio`.

Project opening is treated as an untrusted boundary: manifests are validated
before native code or plugins are loaded, paths are kept inside the project
root, and safe mode must disable third-party modules. Reports should include
the commit, operating system, reproduction steps, and whether project files
were modified.
