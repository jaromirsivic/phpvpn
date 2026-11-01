# Source distribution tooling

Scripts of the procedure in [distribution.md](../../distribution.md), chapter "distribuce
zdrojových kódů do gitpilandrome", with the checks of
[create_source_code_distribution.md](../../create_source_code_distribution.md). They build a
verified, English-documented copy of the current sources without binaries, certificates, keys or
values of one computer, and write it into a target clone file by file. Everything runs with
`python -I` and the standard library only.

| Script | Purpose |
|---|---|
| `inventory.py <snapshot> <include-list> <exclusion-report>` | decides for every file of the snapshot whether it is distributed; the rules (planning notes, dated reports, work directory, editor backups, the Wintun DLLs, the Gradle wrapper JAR, certificates, local agent state) are at the top of the script |
| `audit.py <root> [--only <list>]` | read-only audit: the two forbidden names (built from code points, matched in NFC, without diacritics and in escaped forms, in file names, text, PNG text chunks and ZIP members), certificates and private keys, PE/ELF/Mach-O binaries, backup files |
| `merge_translation.py <cz-old> <en-old> <cz-new> <draft>` | line-aligned draft of a changed Czech document: unchanged lines keep their translation, changed lines are marked `@@CZ@@ ` for the translator |
| `check_translation.py <cz> <en>` | validates a finished translation: line count, no marker left, headings, blank lines, code spans, numbers, no Czech diacritics outside code and links |
| `assemble.py <snapshot> <include-list> <staging> <changes-log>` | copies the inventory into a new staging, replaces the Czech documents with `translations/`, remaps anchors, turns links to undistributed files into plain text, writes a portable `phpvpn.config.json` and adds `extras/` |
| `manifest.py write <staging> <version> <commit>` / `check <root>` | writes or checks `source-distribution-manifest.json` (paths, sizes, SHA-256; the manifest itself is not listed) |
| `links.py <root> <include-list>` | relative Markdown links of the distributed documents against the distributed file set |
| `publish.py <staging> <target> <obsolete-list> [--apply]` | copies the staging into the target clone file by file, never touches `.git`, removes only files on the obsolete list and reports files of the target that are in neither |

`translations/` holds the English translations of the Czech documents that the distribution
replaces (`client.md`, `tray_UX.md`, `distribution.md`, `create_source_code_distribution.md`,
`protocol.md`, `client/tests/e2e/manual-gates.md`, `client/tests/manual/README.md`). Each is line
aligned with its Czech original: `assemble.py` refuses a translation whose heading lines differ.
After a change of a Czech document, run `merge_translation.py` with the previous Czech version,
the previous translation and the new Czech version, translate the marked lines, and check the
result with `check_translation.py`. `extras/` holds the files that exist only in the distribution:
its `README.md`, the MIT `LICENSE` and `client/tools/get-wintun.ps1`.

## Sequence

From the repository root, with `<commit>` the committed state to distribute and `<work>` a folder
outside the repository and the target:

```sh
git archive --format=tar -o <work>/snapshot.tar <commit>
mkdir <work>/snapshot && tar -xf <work>/snapshot.tar -C <work>/snapshot
python -I tools/source-distribution/inventory.py <work>/snapshot <work>/include.txt <work>/excluded.txt
python -I tools/source-distribution/audit.py <work>/snapshot            # findings outside the inventory only
python -I tools/source-distribution/check_translation.py <work>/snapshot/client.md tools/source-distribution/translations/client.md   # each translated document
python -I tools/source-distribution/assemble.py <work>/snapshot <work>/include.txt <work>/staging <work>/changes.txt
python -I tools/source-distribution/manifest.py write <work>/staging <version> <commit sha>
python -I tools/source-distribution/links.py <work>/staging <work>/include.txt
python -I tools/source-distribution/audit.py <work>/staging              # must report nothing but the two install.sh false positives
python -I tools/source-distribution/publish.py <work>/staging <target> <work>/obsolete.txt           # dry run
python -I tools/source-distribution/publish.py <work>/staging <target> <work>/obsolete.txt --apply
python -I tools/source-distribution/manifest.py check <target>          # compares every file outside .git
```

Between the staging and the publication, verify a copy of the staging outside the repository
according to its `README.md`: `get-wintun.ps1` (or the three DLLs from a verified copy of the
official archive), `gradle wrapper` for `client/android`, a test Git commit with a neutral identity,
the Windows build with its tests, the server tests, the Linux packages from that commit and
`tools/make-distribution.ps1 -StagingOnly` with a configuration passed through `PHPVPN_CONFIG`.
The `audit.py` finding `pem-certificate-or-key` on `client/packaging/linux/dist/install.sh` and
`client/packaging/macos/dist/install.sh` is expected: the scripts search for the PEM header text.
A forbidden name, a certificate, a key or a binary in the staging stops the publication.

`publish.py` reports files of the target that are neither staged nor on the obsolete list; decide
each one by hand and list those to remove in the obsolete list, never mirror or empty the target.
The target's `.git` stays untouched; committing or pushing it is a separate decision.
