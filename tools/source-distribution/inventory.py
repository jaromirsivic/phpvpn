"""Builds the include/exclude inventory of the source distribution from a snapshot tree.

Usage: python -I inventory.py <snapshot> <include-list> <exclusion-report>
"""
import os
import re
import sys

RULES = [
    (r'^(Intents|Plans|Reports|micro_updates)/', 'internal planning notes and work reports'),
    (r'^graft/|^graft (start|stop)\.bat$|^tests/test_graft_launchers\.ps1$', 'generated context graph and its local launchers'),
    (r'^work/', 'working directory: downloads, previews, scratch files'),
    (r'^\.vscode/', 'personal editor settings'),
    (r'^\.ignore$', 'search filter for the undistributed context graph'),
    (r'^\.intent-builder-active$', 'local agent state with paths of this machine'),
    (r'^AGENTS\.md$', 'instructions for local coding agents (refers to the undistributed graph)'),
    (r'^(audit\.md|audit results\.md|nalezy\.md|anal.za k.du\.md)$', 'internal audit notes and findings'),
    (r'^client/tests/[^/]*(report|matrix|audit|handoff|implementation-plan)[^/]*\.md$', 'dated test report'),
    (r'^client/tests/macos-client-report-[^/]*\.md$', 'dated test report'),
    (r'^client/tests/(linux-tray-b2|android-client)-report-[^/]*\.md$', 'dated test report'),
    (r'^client/tests/android/[^/]*-report-[^/]*\.md$', 'dated test report'),
    (r'^assets/android-cz/', 'Czech variant of the Android screen design; the complete English variant assets/android-en is distributed'),
    (r'^server/python/tests/[^/]*validation[^/]*\.md$', 'dated validation report'),
    (r'^testing\.md$', 'dated record of one test session on the author machine (local addresses and backups)'),
    (r'~$', 'editor backup file'),
    (r'^client/assets/signing/.*\.cer$', 'certificate: never distributed, the recipient creates an own signing identity'),
    (r'^client/third_party/wintun/[^/]+/wintun\.dll$', 'prebuilt binary: obtained from the official Wintun release, see README.md'),
    (r'^client/android/gradle/wrapper/gradle-wrapper\.jar$', 'compiled JAR: recreated with gradle wrapper, see README.md'),
]


def decide(rel: str):
    for pattern, reason in RULES:
        if re.search(pattern, rel):
            return reason
    return None


if __name__ == '__main__':
    root, include_path, report_path = sys.argv[1:4]
    included, excluded = [], []
    for folder, _, files in os.walk(root):
        for file in files:
            rel = os.path.relpath(os.path.join(folder, file), root).replace(os.sep, '/')
            reason = decide(rel)
            (excluded if reason else included).append((rel, reason))
    included.sort()
    excluded.sort()
    with open(include_path, 'w', encoding='utf-8', newline='\n') as handle:
        handle.write('\n'.join(rel for rel, _ in included) + '\n')
    with open(report_path, 'w', encoding='utf-8', newline='\n') as handle:
        for rel, reason in excluded:
            handle.write(f'{rel}\t{reason}\n')
    print(f'included {len(included)}, excluded {len(excluded)}')
