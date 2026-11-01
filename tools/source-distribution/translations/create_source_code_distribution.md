# Creating the phpvpn source code distribution

## Purpose and scope

This document is the procedure for an agent that is to create a complete, portable distribution of the **source code** of the phpvpn project. It remains valid after future changes to the project. The names of existing components given here are an indicative minimum, not a closed list of files to copy.

A request to create or modify this document by itself **does not constitute an instruction to perform the distribution**. Perform the following operations only when assigned to create the source distribution.

The terms **MUST**, **MUST NOT** and **STOP** are binding. In case of uncertainty that affects security, completeness, licensing or portability, do not assume success. Stop the affected procedure and report the specific obstacle to the user.

| Task | Path |
| --- | --- |
| Default source checkout | root of this repository (the folder containing this document) |
| New source distribution | `C:\gitsigma\phpvpn` |
| Backup of the previous target content | `C:\gitsigma\phpvpn_old` |
| Temporary staging | A new unique directory next to the target, for example `C:\gitsigma\phpvpn_source_stage_<id>` |
| Report and working builds | A separate unique working directory outside the source, target, backup and staging |

A different source checkout may be chosen if the assignment implies it. Do not change the target and the backup without an explicit instruction. The absolute paths in this table are parameters of the publishing operation on this machine. They MUST NOT become a prerequisite for building or running the project at the recipient's site.

The document [distribution.md](distribution.md) describes building the binary distribution. That guide and its scripts belong in the source package, but its requirements for bundling EXE, DLL, RPM or DEB files **do not apply** to the source package. The source distribution preserves the structure of the source project, not that of the resulting `dist` tree.

## 1. First map the current project

1. Read the applicable repository instructions. If the `graft` context graph is available, start with `graft map` and relevant queries. The graph serves for orientation, not as a complete security check of all files.
2. Record the actual checkout root, branch, commit and working tree state, including staged, unstaged, untracked and ignored files. Use, for example, `git status --short --untracked-files=all`, `git ls-files -z`, `git ls-files --others --exclude-standard -z` and a separate listing of ignored entries. Process NUL-separated outputs as such, not by splitting them into lines.
3. Check the build configurations, presets, dependency and lock files, submodules, LFS, CI, tests, generators, application resources, installation and distribution scripts and their actual dependencies. Follow relative references and calls through to all necessary inputs.
4. Determine all currently supported platforms, server implementations and build procedures. Do not rely only on this document or an old list of extensions. A new language, platform or directory is assessed by its function.
5. Prepare an explicit file inventory with an include/exclude decision and a reason for each file. Being tracked by Git does not by itself mean that a file belongs in the distribution. Being ignored by Git does not by itself mean that a file is not needed.
6. Distribute the current intended content of the working tree, including relevant uncommitted changes. Do not revert files to `HEAD`, and do not perform an automatic commit, stash, checkout or cleanup of the source repository. For unclear work-in-progress components, request clarification.
7. Once the content has been determined, capture a stable snapshot and the SHA-256 of the inputs. If the sources change during copying, abort the procedure and repeat the preparation from a consistent state. A combination of files from different points in time MUST NOT be released as one verified whole.

`git archive` by itself is not sufficient. It omits uncommitted content and does not address the completeness of submodules, LFS, unsuitable tracked files or the metadata check.

## 2. Mandatory content

Include at least:

- All of the project's own source code of the client and the servers, headers, necessary resources, build configurations, generators, templates, schemas and portable sample configurations.
- Tests and safe test data needed to verify the distributed project.
- Build, packaging, installation and verification scripts, including all helpers and templates. Currently these include in particular `tools/make-distribution.ps1`, `tools/verify-distribution.ps1`, `tools/make-download-archives.ps1`, `tools/distribution-common.ps1`, `client/tools/` and `client/packaging/`. The conditions of the binary prohibition and the check of local bindings also apply inside these directories.
- Dependency inputs and lock files, for example `server/python/requirements-server.in` and `server/python/requirements-server.txt`. Preserve the necessary `.gitattributes`, `.gitignore` and other portable hidden configuration files.
- This document and the guide for creating the binary distribution. If the project has automation of the source distribution, include its source script, helpers, selection rules and validator. They must not exist only on the original author's machine or refer to an undelivered local tool.
- Documentation of building, testing, obtaining external dependencies and the limits of platform support.

### Mandatory project descriptions

| Document | Rule |
| --- | --- |
| `protocol.md` | Complete current protocol specification. |
| `client.md` | Complete current client description. |
| `tray_ux.md` | Logical name of the tray UX documentation. In the current repository the file is named `tray_UX.md`. Preserve the actual spelling of the name and the matching links; do not create a second copy that differs only in letter case. |
| All server guides | Search recursively, case-insensitively, for at least `server.md`, `server-*.md` and `server_*.md`. Also include other documents describing individual servers if they have different names. Currently this includes the English `README.md` files in the folders `server/php`, `server/python`, `server/node.js` and `server/wordpress` and the developer guide `server/wordpress/version7/README.md`. |
| `readme.txt` | Mandatory file in the root of the result that clearly describes the source distribution. |
| `LICENSE` | Mandatory root file with the complete text of the MIT license for the project itself, provided the license check below is passed. |

Do not rename documents or flatten directories without verifying all links and scripts. Include the necessary local images and other documentation attachments. Check the links on a case-sensitive system as well. Do not replace a missing mandatory document with an empty file or an invented description.

The root `readme.txt` must explain that this is source code and state the project layout, the prerequisites, the exact commands for obtaining dependencies, building, testing and creating the binary distribution, and links to the mandatory guides and licenses. Also state the necessary external tools, any network access required and the platforms actually verified. When creating it initially, keep the project's language and style. Subsequent translation of the manuals is governed by section 7 and requires the user's explicit permission.

The current root `readme.txt` describes the installation of the finished binary distribution. If this is still the case at the time of execution, keep its original content in the place expected by the binary packager and supplement the root README of the source distribution so that it is not misleading. Examine in particular the scripts that verify the byte match of the README. Any change to the packaging tree or the README must be recorded and verified and must not silently change the meaning of the binary guide. Do not overwrite the source checkout with it without a separate assignment.

### Building the binary distribution from a new Git repository

In the root `readme.txt` of the source distribution, state this prerequisite: before building the complete binary distribution, the recipient must put the delivered sources into their own Git repository and create a commit. This may be a different repository with its own history. The original `.git` is not transferred and the original history is not needed for the build.

The Linux packages and the subsequent complete binary distribution must be produced from the same commit of the new repository, without uncommitted changes to the distributed sources. `tools/make-distribution.ps1` reads the current `HEAD` and compares it with the provenance of the Linux packages. Packages built from the original repository therefore cannot be used merely on the basis of matching content if they state a different commit. Do not bypass the provenance check with the `-AllowLinuxProvenanceMismatch` switch.

The commit of the original checkout in `source-distribution-manifest.json` remains a record of the provenance of the delivered sources. It does not replace the commit of the new repository used when building the binary packages, and it must not be overwritten to stand in for it.

To verify this procedure, it is permitted to initialize a separate temporary Git repository and create a test commit, but only in the external verification copy according to section 5. Use a neutral test identity. This repository and its `.git` do not belong in the staging, the target or the backup, and nothing is published from it. Initialization or a commit in the actual target repository still requires a separate assignment.

### Licenses and external components

The target license of the project itself is MIT. First verify the existing license statements and the right to use them. In the current tree, the MIT text is in `client/packaging/linux/LICENSE`, but its mere existence is not proof of the license of every component. For the root `LICENSE`, use the corresponding complete text and documented copyright information. Do not invent an author, do not change the rights holder and do not remove mandatory attributions.

Preserve the licenses, copyright notices and any NOTICE files of third parties. The project's MIT license does not replace them. A license conflict, unclear rights or a forbidden name in a mandatory attribution mean **STOP** and notifying the user, not automatic anonymization or relicensing.

## 3. Forbidden content and completeness without binaries

The following MUST NOT be included in the result:

- Compiled and native executable files, libraries, object files, drivers, debug symbols and bytecode. Typically EXE, DLL, SYS, COM, OBJ, O, LIB, A, SO including versioned names, DYLIB, PDB, PYC, PYO, CLASS and WASM.
- Installation packages and finished application packages, for example MSI, MSIX, DEB, RPM, AppImage, APK, JAR or WHL. An archive must not be used to hide forbidden content either.
- Build results, CMake cache and generated build trees, virtual environments, dependencies downloaded as a local cache, `node_modules`, tools delivered as binaries and previous distributions.
- Any certificates, including public, root, development, test and self-signed certificates, certificate chains and certificate stores. The prohibition also applies to certificates embedded in source code, documentation, resources, test data or archives.
- Sensitive keys, private signing material, real passwords, access tokens, API keys and other credentials. The exception for public phpvpn default values is set out in the following section.
- Runtime configurations with sensitive data, sessions, queues, databases, logs, dumps, temporary files and backups. Include the development and test configuration, for example `phpvpn.config.json`, according to the exception in section 4.
- `.git` or any other VCS history, local editor configuration, personal settings, working reports and internal notes that are of no significance to the recipient. The generatable context graph and local agent state are also not part of the distributed source.

Decide by content and purpose, not just by extension or folder name. For example, `client/packaging/linux/dist/` currently contains source installation scripts and documentation that cannot be excluded just because of the name `dist`. Conversely, a native ELF without an extension is still a forbidden binary. The check must recognize PE, ELF and Mach-O and their common variants, libraries and bytecode even after renaming.

Text source and installation scripts `.ps1`, `.sh`, `.bat`, `.py`, `.php` or `.js` are mandatory source inputs if the project needs them. The prohibition of executable files does not apply to these text scripts or to their POSIX execute bit. Preserve the necessary modes and line endings. Source icons, images and other uncompiled resources may be bundled after their content, metadata and license have been checked.

The current repository contains, for example, `client/third_party/wintun/*/wintun.dll`. **These DLLs are not copied into the source distribution.** Keep the headers, licenses and guides. For this and every future binary dependency, the recipient must have a documented, portable and verifiable way of obtaining a compatible version from the official source, or of building it from source. State the version, the integrity and the expected location. Do not skip dependency verification and do not weaken the packagers' signature checks.

If the existing build or packager requires an excluded file, it is not sufficient to delete the file and declare the package finished. First secure and verify a reproducible procedure for obtaining the dependency. A necessary fix of the project that goes beyond the assignment is handed over to the user as an obstacle. A private signing key is not replaced with the original author's key, with its export or by disabling checks. Document the supported way of creating or using your own signing material.

### Certificates, secrets and permitted default values

**Public default passwords and tokens of phpvpn are permitted.** Keep them if they are documented default values of the project intended for public distribution, not real operational data. This also includes public default values in fields labeled secret or access key, if they have exactly this function. The variable name alone, low entropy or an occurrence in a test or README is not proof of a safe default value. The README must warn that they need to be changed before actual deployment.

For each distribution, derive the exact list of permitted values from the current sources and documentation of the project and record the justification of the exceptions. Do not permit a whole file, a whole directory or all values of a variable wholesale. An unclear origin of a value blocks publication until it is clarified. The exception for default passwords and tokens **never permits a certificate or a private signing key**.

Check the content and metadata of all candidates and of the result, including hidden files, strings in sources, URLs with credentials and encoded values. Extensions such as `.cer`, `.crt`, `.pem`, `.der`, `.pfx`, `.p12`, `.p7b`, `.jks`, `.keystore` or `.key` serve only as indicators. The check must recognize certificates and sensitive keys even without an extension, in a different format or embedded in text. Certificates cannot be accepted merely because they do not contain a private key.

Exclude standalone certificates and local secret files from the inventory. If certificates or non-permitted secrets are embedded in necessary sources, or if their exclusion would break the build or the tests, stop publication and report the obstacle without printing the sensitive value. Do not automatically remove parts of the code and do not weaken signature verification. The recipient must obtain or generate the necessary certificates and keys by a documented procedure outside the distributed package. Likewise, any test certificates are created only during the test and must not remain in the staging.

The bundling prohibition applies to the new source distribution and to its manifest and documentation. The internal backup of the previous target according to section 6 must remain complete, must not be filtered because of this prohibition and is never bundled with the distribution. In the report, record only the location and kind of a sensitive finding, never its value.

## 4. Mandatory check of names, metadata and local bindings

Perform this check **before running the project's scripts and before any change to the target or the backup**, again on the staging, and finally on the result. The audit must cover the entire current source tree, including hidden, untracked and ignored entries, not just the planned package content. Entries intended for exclusion must not serve as a blind spot for findings in the sources. Git history does not need to be exported or rewritten, but check the metadata of the current files and the Git metadata in use. Do not follow links outside the examined tree.

### Forbidden names

Search for both strings forbidden by the user as substrings, regardless of letter case. Their exact definition using Unicode code points is:

| String | Unicode code points |
| --- | --- |
| Forbidden first name | `U+004A U+0061 U+0072 U+006F U+006D U+00ED U+0072` |
| Forbidden surname | `U+0160 U+0069 U+0076 U+0069 U+0063` |

The definition using code points makes it possible to distribute this check rule without its literal quotation itself inserting the forbidden name into the package. The check tool builds the search strings from the code points in memory. First normalize the text to Unicode NFC and compare case-insensitively. In addition, examine variants without diacritics, decomposed Unicode forms and escaped or encoded values in the supported formats. Treat these variants as findings to be clarified as well.

The check covers file and directory names, text content, comments, copyright, authorship, resource and manifest files, configuration values, image and document metadata, NTFS alternate data streams and other metadata that may be carried over. Do not limit yourself to the lines displayed by an editor. Take into account UTF-8, UTF-16 and any other encodings actually used. For containers, check the internal names, content and metadata without executing their content and without unsafe extraction of paths outside the working directory.

A mere `rg` pass or a search for ASCII strings in a binary file is not a complete metadata audit. For each retained non-text format, a corresponding check must be available. An unreadable file, an unsupported relevant format, a decoding error or unexamined metadata mean an incomplete audit and block publication.

### Bindings to a specific computer

**Machine-specific absolute paths, especially paths for testing, belong only in configuration, not hardcoded in source code or scripts.** Code and scripts must load them from configuration.

**The prohibition of absolute paths does not apply to configuration files.** The file `phpvpn.config.json` may be transferred to the new source distribution with its existing values, including the paths to tools, test directories and the SSH key, and the local VM settings. Such a configuration value by itself is not a reason to stop the distribution; do not automatically rewrite or anonymize it. The path to a key does not permit bundling the private key itself. The other checks of the configuration content, in particular the prohibition of certificates and sensitive credentials, remain in force.

In the root `readme.txt`, explain that the recipient must adapt the configuration to their environment before building or testing. When verifying portability, modify the configuration only in the external verification copy, or use the supported configuration switch `PHPVPN_CONFIG`. Record the settings used in the report, without secrets. This exception does not excuse hidden dependencies in code or build checks that were not performed.

Examine in particular:

- Absolute Windows and UNC paths, `file://` links, user profiles, home directories, specific checkouts, mapped drives, local VMs and internal network addresses.
- Hardcoded paths to the compiler, SDK, Python, CMake, libraries or caches. Also dependency references such as a local `file:` reference or editable installations outside the project.
- CMake cache, user presets, generated project files, environment variables, registry entries, account names, machine names, certificates or thumbprints without which another user could not build or package the project.
- Scripts referring to undelivered helpers, files outside the project root, private repositories or settings available only to the original author.

First classify a found absolute path by its context. A configuration value according to the exception above, a standard system path, an explicit illustrative placeholder, an isolated test value or the target paths of this publishing methodology are not by themselves a forbidden binding to the original computer. Each exception must have a specific justification in the report. Do not introduce blanket exceptions for whole directories or all tests. An unresolved doubt about portability outside these exceptions means **STOP**.

### Response to a finding

When a forbidden name or a suspicious local binding is found, **stop creating the distribution immediately and report the finding to the user**. State the relative path, the line or metadata field name, the type of problem, its impact and a proposed solution. Quote only the necessary excerpt and mask secrets. Do not perform automatic replacement, anonymization, removal of attribution or omission of a file in order to circumvent the check.

Do not perform the backup rotation, deletion of the target or publication. If the finding arises only during the final check, mark the result as defective, preserve the verified backup and proceed according to the recovery procedure below. Continuing to resolve the finding requires an explicit instruction from the user and repetition of the affected checks.

## 5. Prepare and verify the staging

1. Create a new empty staging with a unique name. Do not copy the old distribution into it and do not use blind mirroring of the source checkout.
2. Copy only the entries of the approved inventory, preserving their relative paths. Evaluate case-insensitive name collisions, non-portable names, invalid relative paths and links escaping outside the root. Collisions and unresolved links block the result.
3. Do not copy or automatically follow symlinks, junctions and other reparse points. If the project needs them, define and verify a portable way of delivering them. Otherwise stop on the unresolved dependency. For submodules and LFS, verify that the actual sources are present in the required version, not just a gitlink or a pointer.
4. Add the mandatory documentation and licenses. The content of the sources must match the input snapshot, except for explicitly recorded changes to the documentation and the package metadata. Do not transfer personal ACLs, ownership or alternate data streams. Document and verify the necessary script modes for the target platforms.
5. Create `source-distribution-manifest.json` with the schema version, the project version, the actual source commit or a value indicating that it is unavailable, a flag for uncommitted changes, and a list of the relative paths, sizes and SHA-256 of all distributed files. Omit the manifest itself from its list and record its hash in the external report. Do not insert the author's name, the hostname, absolute local paths or secrets. Any changes compared with the source snapshot must be traceable in the report.
6. Compare the exact sets of paths. The result must contain exactly the declared files and the manifest, including hidden files. Every mandatory input must be included or, if the rules forbid bundling it, have a verified procedure for external acquisition. An unknown file means a check, not automatic acceptance.
7. Perform the check of forbidden content, certificates, secrets, names, metadata and paths according to the previous sections. Accept default passwords and tokens only according to the documented list of permitted values. Also check the newly created README and manifest.
8. Copy the staging into a new verification folder outside the original checkout, ideally with spaces and a non-ASCII character in the path. Verify the build according to the delivered guide from this copy, without `.git`, the original build cache, personal presets or access to the files of the original checkout. System tools are permitted only as documented prerequisites. For strong verification, use a clean container or VM if one is available.
9. Obtain the dependencies in the documented way, configure and build the supported targets, run the relevant tests and verify that the delivered scripts can create the binary distribution from this copy. For the complete binary distribution, first create a new test Git repository and a commit in the verification copy according to section 2. Do not take over the original `.git` or history. For Python, use `uv`, including installing packages via `uv pip`. All resulting binaries, environments and logs must remain outside the staging and the resulting source package.
10. The report must distinguish success, failure and a check not performed for each platform. A missing environment or missing access to a dependency does not mean success. If the necessary portability or completeness cannot be verified, do not publish until the user explicitly decides on the specific limitation of verification. The prohibition of binaries, names and unresolved local bindings remains in force.
11. After the tests, verify again that the staging is clean and that its hashes match the manifest. Only a successfully verified staging may proceed to the replacement of the target.

## 6. Safe rotation and filling of the target

The required order of operations is binding: **delete the previous `phpvpn_old`, copy the existing `phpvpn` to `phpvpn_old`, verify this backup, clear the content of `phpvpn`, fill it with the new distribution**. The preparation of the staging and the checks must take place before this sequence.

### Before destructive operations

- Acquire an exclusive lock for the publishing operation outside the target and the backup. Merely checking whether a lock file exists is not sufficient; the lock must be created atomically. Prevent concurrent publications and verify that nothing is currently writing to the target.
- Canonicalize the absolute paths of the source, target, backup, staging and working directory. Verify the actual targets in the file system and all existing ancestors. The target and the backup must be exactly the two expected, distinct direct children of `C:\gitsigma`, not a drive root or an alias of another path. The source, staging, target and backup must not be inside one another.
- Reject symlinks, junctions, reparse points and mount points on the paths and inside the trees intended for recursive deletion or copying. Also examine risky hardlinks. An unresolved alias of a file outside the designated tree blocks the operation.
- Verify permissions, readability, free space for the complete backup and the new content, and the stability of the existing target. Record its listing and hashes in the external report before the rotation. Unreadability or a change of the target means stopping.
- The existing target may also contain hidden files, `.git`, old binaries or files unrelated to the new inventory. The backup must capture the entire previous content. The new distribution does not take it over. If any expected directory path is a regular file, or its state changes during the check, stop.
- Before you delete the previous `phpvpn_old`, it must be certain that it is not the only complete copy left after a previously interrupted operation. If there is a record of an unfinished publication, a partial target or an unclear state, resolve the recovery first. A repeated run must not overwrite the last valid backup with an incomplete target.

### Exact procedure

1. In an external state record, store the operation identifier and the state `staging_verified`. Write the state by atomic file replacement and update it at each subsequent step, so that an interrupted run can be recognized.
2. If `C:\gitsigma\phpvpn_old` exists, remove it entirely after its exact path has just been verified. Any deletion error terminates the operation. Do not ignore it.
3. If `C:\gitsigma\phpvpn` exists, copy its **entire content including hidden entries** into a new `C:\gitsigma\phpvpn_old`. Also preserve empty directories and the metadata needed for recovery, including any alternate data streams and permissions. This concerns the internal backup, not the source distribution. Do not filter the backup by the rules for the new package. Verify the exact sets of paths, types, lengths, SHA-256, restorable metadata and the stability of the original target. Only then write `backup_verified`.
4. If the target does not exist at the first release, record this fact and skip copying the nonexistent directory. Do not create a fake backup. Its absence, however, must not mask an interrupted previous publication.
5. Immediately before deletion, verify again the target path, its state and that it is unchanged compared with the verified backup. Delete all its children, including hidden files. Keep the root directory, or create it if it did not exist yet. Verify that it really is empty. Write `target_empty`.
6. Copy the content of the verified staging into the target without an additional wrapping folder. Write `target_copied`.
7. Compare the exact set of paths and the hashes with the staging and the manifest again. Verify the mandatory documents and licenses, and the absence of forbidden content, of all certificates, of non-permitted secrets, of names and of suspicious local bindings. Unexpected files or differing metadata mean failure.
8. Only after successful verification, write `source_distribution_verified`. The source distribution has been created; next comes the decision about the optional translation according to section 7. Mark the entire requested procedure `complete` only after the permitted translation has been completed, or after recording that the translation was not permitted and did not take place. Keep the valid backup `phpvpn_old` for the next rotation and as a reference for the translation.

In PowerShell, use literal path handling (`-LiteralPath` where the cmdlet supports it), inclusion of hidden entries and termination on error. Check the exit codes of native tools according to their actual meaning. Do not use `cmd /c` with a generated delete command, `Invoke-Expression` or broad wildcard deletion from an unverified working directory. The verification before a destructive operation must take place while the lock is held. The lock coordinates the publishing tools; it must not be presented as protection against all foreign processes.

### Failure and recovery

- Before the target is cleared, leave the target unchanged. If copying the backup fails, do not mark the partial `phpvpn_old` as valid and do not start deleting the target. Under the prescribed order, deleting the previous backup generation is irreversible; do not promise its recovery.
- After the target has been cleared, or on a publication error, preserve the verified backup, the staging and the exact state of the operation. If it is safe to empty the target again and it has not been changed by another process in the meantime, restore it by copying from the verified `phpvpn_old` and check that they match. Do not move or delete the backup.
- If the safety of the recovery cannot be verified, delete nothing further. Report the exact state of the target, the location of the valid backup and the point from which to continue. At a first release without a backup, state explicitly that there is nothing to restore.
- Clean up the staging, the working outputs and your own lock only after completion or after a safely documented recovery. On interruption, keep everything needed for diagnosis and continuation. Do not terminate foreign processes.

## 7. Optional translation of all manuals into English

**After creating the source distribution, go through the individual manuals and, after the user's explicit permission, translate all Czech manuals and the Czech parts of mixed manuals into English.** Do not retranslate parts that are already in English. The scope includes all distributed guides and specifications, in particular `protocol.md`, `client.md`, `tray_UX.md`, all server guides, `readme.txt`, the distribution methodologies including this document, and further manuals in subdirectories. Do not limit the inventory to the `.md` extension or to the files named here.

1. After verifying the created distribution, compile an inventory of the manuals and their languages. **Neither the request to create the distribution nor the existence of this rule constitutes consent to the translation.** Unless translation has already been explicitly permitted for the given release, ask the user for permission for this specific step. Do not translate in the reply. On refusal or in the absence of an answer, the verified distribution stays in the original languages and the report states that the translation did not take place. Do not ask again for an explicit permission that has already been granted.
2. If the user permits the translation, use **primarily the phrasing, terminology and style of the English translations of the previous version in `C:\gitsigma\phpvpn_old`**. Find the corresponding manuals by path and by content, including renamed documents. Only read the backup and do not change it. Before taking over text, perform the relevant checks of names, secrets and local bindings from this methodology; an older version is not automatically a safe source.
3. The authority for the content is the current manual of the new distribution. Use the earlier English wording for passages with identical meaning and as a stylistic model. Translate changed and new parts according to the current Czech text. Do not carry over outdated requirements, old paths or removed chapters. If no previous English translation or backup exists, create a complete translation of the current manual after permission is given, and keep the terminology consistent across all manuals.
4. Translate the complete content, not a summary. Preserve the normative meaning, structure, tables, identifiers, file names, commands, code samples, configuration values and URLs. Update any anchor links according to the translated headings and verify that they work. Do not arbitrarily translate or change license texts and mandatory legal notices. For a passage in a language other than Czech or English, clarify the scope with the user.
5. Prepare the translations in a separate working copy of the newly created distribution and replace the corresponding manuals with them, preserving their paths and names. The source checkout and `phpvpn_old` remain unchanged. This permission applies to the manuals of the new source distribution; it does not authorize updating the preserved translation `documentation/protocol.en.md` in the source checkout or any other changes outside the distribution copy.
6. Compare each translated manual with the complete current original and check that no chapter, requirement, table or warning is missing. Verify that the terminology is consistent and that no untranslated Czech prose remains. Examine the links and the script rules that depend on the content of the documentation, especially the README checks. Do not weaken these checks; an unresolved dependency blocks the completion of the translation.
7. Update the manifest and the SHA-256 of the changed files and perform the checks of the final content according to section 5 again. In the report, record the permission, the list of translated and already-English manuals, the templates used from the backup and the verification result. Do not take over old hashes or verification claims from the previous version.
8. Use the verified translated working copy as the new staging. While holding the exclusive lock, verify that the target is unchanged, keep the new untranslated distribution separately in case recovery is needed, and publish the translated content including the manifest. **Do not run the `phpvpn_old` rotation again**, because it would destroy the backup of the previous release. Verify again that the target exactly matches the translated staging. On error, preserve or safely restore the newly created untranslated distribution and mark the translation as unfinished.

## 8. Requirements for possible automation

This document itself does not implement any new script. If another agent automates the procedure, the script and its validator must belong to the distributed sources and comply with all the rules above. A preparation or `dry-run` mode must not delete or change the target and the backup. The preparation phase must build the inventory and print a concrete plan of changes; the publishing phase may accept only a re-verified staging.

The script must be portable, use parameters for the location of the source and of the working data, and must not itself require the local paths given here to compile the project. Only the publishing tool may have the default publishing destination according to this methodology. The result must not depend on the order of directory listings. Sort the manifest stably and do not modify the bytes of source files when copying.

Verify at least the following failure scenarios in separate test directories, never on the actual distribution target:

- A missing target at the first release, and the regular rotation of an existing target and backup.
- A forbidden name in text, in a path and in metadata, different letter cases and Unicode forms. Create the test strings from code points at runtime, not as literal forbidden values in the sources.
- A suspicious local dependency, an unreadable input, a missing mandatory document and a renamed binary without a typical extension.
- A certificate without a private key, a certificate embedded in a source and a sensitive key or operational token must be rejected. A documented phpvpn default password or token must pass; another value in the same field or file must not inherit this exception. Generate sensitive test inputs only in an isolated working directory.
- A reparse point, a dangerous target, a name collision, a concurrent run and a change of input during copying.
- A backup failure, an interruption after the target has been emptied, an incomplete filling of the target, recovery, and a repeated run after a crash without loss of the last valid backup.

## 9. Conditions for completion and handover

The distribution is complete only when:

- It contains the current complete sources and scripts needed to build and create the distribution, including verified acquisition of the dependencies that are not bundled.
- It contains all mandatory guides, a correct `readme.txt`, the root MIT license and the necessary third-party notices.
- It contains no compiled or native executable files, no certificates, sensitive keys, non-permitted passwords or tokens, private data, forbidden names or unresolved bindings to the original computer. Any bundled phpvpn default passwords and tokens have a documented exception according to section 3.
- The portability check and the relevant tests have passed; any explicitly approved limitations of verification are stated precisely.
- The target exactly matches the verified staging and the manifest. The backup matches the entire previous content of the target, if it existed.
- The state of the optional translation is recorded. If the user permitted it, all Czech manuals and Czech parts of manuals have been translated into English according to section 7, and the final manifest corresponds to the translated distribution. Without permission, the manuals have remained in their original languages.

Hand over to the user the path to the result and to the backup, the version and identification of the source state, the number of files, the manifest hash, brief verification results and a reference to the external report. State the checks that were not performed and the open obstacles. Never label a partial result a finished distribution.

Creating a ZIP, initializing Git, committing, pushing or publishing are not part of this operation unless the user requests them separately. The only exception is the temporary test repository in the external verification copy according to sections 2 and 5. Any archive created later must pass the same checks after safe extraction and must preserve the necessary script modes.
