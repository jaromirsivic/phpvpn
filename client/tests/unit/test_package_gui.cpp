#include "harness.h"
#include "script_runner.h"
#include <chrono>
#include <fstream>

using namespace phpvpn::package;
namespace {
struct Fixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        (L"phpvpn GUI český ' & [fixture] " + std::to_wstring(GetCurrentProcessId()));
    Fixture() { std::filesystem::create_directories(directory); }
    ~Fixture() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    void write(const std::string& body, Mode mode = Mode::install) {
        std::ofstream file(directory / scriptName(mode), std::ios::binary);
        file << "\xef\xbb\xbf[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)\n"
                "$OutputEncoding = [Console]::OutputEncoding\n" << body;
    }
    Result run(std::wstring& text, Mode mode = Mode::install, const ProcessApi& api = {}) {
        std::atomic_bool cancel{false};
        return runScript(mode, directory, [&](std::wstring value) { text += value; }, cancel, api);
    }
};
BOOL WINAPI badPipe(PHANDLE, PHANDLE, LPSECURITY_ATTRIBUTES, DWORD) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
BOOL WINAPI badProcess(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
DWORD WINAPI badWait(HANDLE, DWORD) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
BOOL WINAPI badExit(HANDLE, LPDWORD) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
BOOL WINAPI badPeek(HANDLE, LPVOID, DWORD, LPDWORD, LPDWORD, LPDWORD) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
BOOL WINAPI badRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
}
PHPVPN_TEST(PackageGui, ExitCodeIsAuthoritativeAndModesAreFixed) {
    Fixture fixture;
    for (const int code : {0, 1, 37}) {
        fixture.write("Write-Host 'success'; Write-Output 'output'; Write-Warning 'warning'; [Console]::Error.WriteLine('error'); exit " + std::to_string(code));
        std::wstring text;
        const auto result = fixture.run(text);
        PHPVPN_CHECK_EQ(result.exitCode, static_cast<DWORD>(code));
        PHPVPN_CHECK(result.error.empty());
        PHPVPN_CHECK_EQ(result.succeeded(), code == 0);
        for (const auto* expected : {L"success", L"output", L"warning", L"error"}) PHPVPN_CHECK(text.find(expected) != text.npos);
    }
    fixture.write("throw 'synthetic exception'");
    std::wstring text;
    PHPVPN_CHECK(!fixture.run(text).succeeded());
    fixture.write("Write-Output 'uninstall only'; exit 0", Mode::uninstall);
    text.clear();
    PHPVPN_CHECK(fixture.run(text, Mode::uninstall).succeeded());
    PHPVPN_CHECK(text.find(L"uninstall only") != text.npos);
    PHPVPN_CHECK(text.find(L"synthetic exception") == text.npos);
}
PHPVPN_TEST(PackageGui, Utf8ChunkBoundariesAndInvalidInput) {
    const std::string utf8 = "žluťoučký 😀\r\n";
    for (std::size_t split = 0; split <= utf8.size(); ++split) {
        Utf8Decoder decoder;
        std::wstring text;
        PHPVPN_CHECK(decoder.append(std::string_view(utf8).substr(0, split), false, text));
        PHPVPN_CHECK(decoder.append(std::string_view(utf8).substr(split), true, text));
        PHPVPN_CHECK_EQ(text, L"žluťoučký 😀\r\n");
    }
    for (const std::string invalid : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xe2\x82"), std::string(1, '\0')}) {
        Utf8Decoder decoder;
        std::wstring text;
        PHPVPN_CHECK(!decoder.append(invalid, true, text));
    }
}
PHPVPN_TEST(PackageGui, RawSplitUtf8AndCancellation) {
    Fixture fixture;
    fixture.write("$s = [Console]::OpenStandardOutput(); $s.WriteByte(240); $s.Flush(); Start-Sleep -Milliseconds 100; $s.WriteByte(159); $s.WriteByte(152); $s.WriteByte(128); $s.Flush(); exit 0");
    std::wstring text;
    PHPVPN_CHECK(fixture.run(text).succeeded());
    PHPVPN_CHECK_EQ(text, L"😀");
    fixture.write("$s = [Console]::OpenStandardOutput(); $s.WriteByte(226); $s.Flush(); exit 0");
    PHPVPN_CHECK(!fixture.run(text).succeeded());
    std::atomic_bool cancel{true};
    const auto result = runScript(Mode::install, fixture.directory, [](std::wstring) {}, cancel);
    PHPVPN_CHECK(!result.succeeded());
    PHPVPN_CHECK(result.error.find(L"unknown") != result.error.npos);
}
PHPVPN_TEST(PackageGui, StreamingAndLargeLogBeforeCompletion) {
    Fixture fixture;
    fixture.write("[Console]::WriteLine('FIRST žluťoučký 😀'); Start-Sleep -Milliseconds 600; [Console]::Error.WriteLine('STDERR'); [Console]::Write(('x' * 300000)); exit 0");
    std::wstring text;
    bool sawEarly = false;
    ULONGLONG first = 0;
    std::atomic_bool cancel{false};
    const auto result = runScript(Mode::install, fixture.directory, [&](std::wstring value) {
        if (!first) first = GetTickCount64();
        text += value;
        if (text.find(L"FIRST žluťoučký 😀") != text.npos && text.find(L"STDERR") == text.npos) sawEarly = true;
    }, cancel);
    PHPVPN_CHECK(result.succeeded());
    PHPVPN_CHECK(sawEarly);
    PHPVPN_CHECK(GetTickCount64() - first >= 450);
    PHPVPN_CHECK(text.find(L"STDERR") != text.npos);
    PHPVPN_CHECK_EQ(std::count(text.begin(), text.end(), L'x'), 300000);
}
PHPVPN_TEST(PackageGui, OsFailuresAndMissingFilesFailClosed) {
    Fixture fixture;
    fixture.write("[Console]::WriteLine('fixture'); exit 0");
    for (int fault = 0; fault < 6; ++fault) {
        ProcessApi api;
        if (fault == 0) api.createPipe = badPipe;
        if (fault == 1) api.createProcess = badProcess;
        if (fault == 2) api.wait = badWait;
        if (fault == 3) api.exitCode = badExit;
        if (fault == 4) api.peek = badPeek;
        if (fault == 5) api.read = badRead;
        std::wstring text;
        const auto result = fixture.run(text, Mode::install, api);
        PHPVPN_CHECK(!result.succeeded());
        PHPVPN_CHECK(!result.error.empty());
    }
    std::wstring text;
    PHPVPN_CHECK(!fixture.run(text, Mode::uninstall).succeeded());
    std::atomic_bool cancel{false};
    PHPVPN_CHECK(!runScript(Mode::install, fixture.directory, [](std::wstring) {}, cancel, {}, fixture.directory / L"absent.exe").succeeded());
    fixture.write("[Console]::OpenStandardOutput().WriteByte(255); exit 0");
    PHPVPN_CHECK(!fixture.run(text).succeeded());
}
PHPVPN_TEST(PackageGui, SurvivingDescendantDoesNotDelayCompletion) {
    Fixture fixture;
    // This child inherits the actual redirected handles via CreateProcess.
    // It deliberately survives its parent and writes later, without touching the system.
    fixture.write(R"PS(
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Child {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct SI {
 public int cb; public string reserved, desktop, title; public int x,y,cx,cy,xc,yc,fill,flags; public short show,cb2; public IntPtr reserved2,input,output,error; }
 [StructLayout(LayoutKind.Sequential)] public struct PI { public IntPtr process,thread; public int pid,tid; }
 [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool CreateProcess(string app,string cmd,IntPtr pa,IntPtr ta,bool inherit,int flags,IntPtr env,string cwd,ref SI si,out PI pi);
 [DllImport("kernel32")] public static extern IntPtr GetStdHandle(int n);
 [DllImport("kernel32")] public static extern bool CloseHandle(IntPtr h);
}
'@
$si = New-Object Child+SI
$si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si)
$si.flags = 256
$si.input = [Child]::GetStdHandle(-10)
$si.output = [Child]::GetStdHandle(-11)
$si.error = [Child]::GetStdHandle(-12)
$pi = New-Object Child+PI
$exe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$command = '"' + $exe + '" -NoProfile -NonInteractive -Command "Start-Sleep -Seconds 3"'
if (-not [Child]::CreateProcess($exe,$command,[IntPtr]::Zero,[IntPtr]::Zero,$true,134217728,[IntPtr]::Zero,$PSScriptRoot,[ref]$si,[ref]$pi)) { [Console]::WriteLine(('CreateProcess error ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error())); exit 4 }
[Console]::WriteLine(('CHILD=' + $pi.pid))
[Child]::CloseHandle($pi.process) | Out-Null
[Child]::CloseHandle($pi.thread) | Out-Null
[Console]::WriteLine('FINAL BUFFERED DATA')
exit 0
)PS");
    std::wstring text;
    const auto result = fixture.run(text);
    if (!result.succeeded()) std::wcerr << L"Runner error: " << result.error << L" code=" << result.exitCode << L"\n" << text << L"\n";
    PHPVPN_CHECK(result.succeeded());
    PHPVPN_CHECK(text.find(L"FINAL BUFFERED DATA") != text.npos);
    const auto pos = text.find(L"CHILD=");
    PHPVPN_CHECK(pos != text.npos);
    if (pos != text.npos) {
        const auto pid = static_cast<DWORD>(std::stoul(text.substr(pos + 6)));
        const auto child = OpenProcess(SYNCHRONIZE, FALSE, pid);
        PHPVPN_CHECK(child != nullptr);
        if (child) {
            PHPVPN_CHECK_EQ(WaitForSingleObject(child, 0), static_cast<DWORD>(WAIT_TIMEOUT));
            // Only wait on the synthetic child, never terminate a real tray.
            PHPVPN_CHECK_EQ(WaitForSingleObject(child, 10000), static_cast<DWORD>(WAIT_OBJECT_0));
            CloseHandle(child);
        }
    }
}
