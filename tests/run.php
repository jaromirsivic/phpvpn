<?php

/**
 * phpvpn test runner.
 *
 * Usage:
 *   php tests/run.php                     run every group
 *   php tests/run.php unit integration    run selected groups
 *   php tests/run.php --filter=nonce      run tests whose name contains a string
 *
 * Requires nothing but PHP CLI. No Composer, no PHPUnit, no extra extension.
 */

declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    exit("The test runner must be started from the command line.\n");
}

require __DIR__ . '/lib/Assert.php';
require __DIR__ . '/lib/Suite.php';
require __DIR__ . '/lib/HttpClient.php';
require __DIR__ . '/lib/SignRequest.php';
require __DIR__ . '/lib/StatsClient.php';
require __DIR__ . '/lib/Harness.php';
require __DIR__ . '/lib/Vpn.php';

const GROUPS = ['compat', 'unit', 'integration', 'concurrency', 'fault'];

/**
 * Loads phpvpn.php into this process so unit tests can reach its classes.
 *
 * Constants are file scoped, so exactly one configuration can ever be loaded
 * in process. Everything that needs a different configuration is an HTTP test.
 */
function loadServerInProcess(): string
{
    static $queueRoot = null;
    if ($queueRoot !== null) {
        return $queueRoot;
    }

    $dir = Harness::tempRoot() . '/in-process';
    $queueRoot = $dir . '/queue';
    if (!is_dir($queueRoot) && !mkdir($queueRoot, 0700, true) && !is_dir($queueRoot)) {
        throw new RuntimeException('Cannot create ' . $queueRoot);
    }

    $file = $dir . '/phpvpn.php';
    file_put_contents($file, Harness::render([
        'SERVER_SECRET' => var_export(Harness::SECRET, true),
        'QUEUE_ROOT' => var_export($queueRoot, true),
        'ACCESS_TOKENS' => Harness::tokenLiteral(Harness::TOKENS),
    ]));

    define('PHPVPN_TEST_MODE', true);
    require $file;
    Ctx::init('unit');

    return $queueRoot;
}

$selectedGroups = [];
$filter = '';
foreach (array_slice($argv, 1) as $argument) {
    if (str_starts_with($argument, '--filter=')) {
        $filter = substr($argument, 9);
        continue;
    }
    if (in_array($argument, GROUPS, true)) {
        $selectedGroups[] = $argument;
        continue;
    }
    exit("Unknown argument: {$argument}\n");
}
if ($selectedGroups === []) {
    $selectedGroups = GROUPS;
}

$suite = new Suite();
foreach ($selectedGroups as $group) {
    $directory = __DIR__ . '/' . $group;
    if (!is_dir($directory)) {
        continue;
    }
    $files = glob($directory . '/*_test.php') ?: [];
    sort($files);
    foreach ($files as $file) {
        $suite->group($group);
        $register = require $file;
        if (!is_callable($register)) {
            exit("Test file {$file} must return a callable.\n");
        }
        $register($suite);
    }
}

$tests = $suite->all();
$passed = 0;
$failed = 0;
$skipped = 0;
$failures = [];
$startedAt = microtime(true);

echo "phpvpn test suite on PHP ", PHP_VERSION, " (", PHP_OS_FAMILY, ")\n";
echo str_repeat('-', 72), "\n";

$index = 0;
foreach ($tests as $name => $body) {
    if ($filter !== '' && !str_contains($name, $filter)) {
        continue;
    }
    $index++;
    try {
        $body();
        $passed++;
        printf("ok   %3d  %s\n", $index, $name);
    } catch (SkippedTest $skip) {
        $skipped++;
        printf("skip %3d  %s (%s)\n", $index, $name, $skip->getMessage());
    } catch (Throwable $error) {
        $failed++;
        $failures[] = $name . "\n      " . $error->getMessage()
            . "\n      at " . $error->getFile() . ':' . $error->getLine();
        printf("FAIL %3d  %s\n", $index, $name);
        printf("          %s\n", $error->getMessage());
    }
}

Harness::shutdown();

echo str_repeat('-', 72), "\n";
printf(
    "%d passed, %d failed, %d skipped in %.2fs\n",
    $passed,
    $failed,
    $skipped,
    microtime(true) - $startedAt
);

if ($failures !== []) {
    echo "\nFailures:\n";
    foreach ($failures as $failure) {
        echo '  - ', $failure, "\n";
    }
}

Harness::removeTree(Harness::tempRoot());

exit($failed === 0 ? 0 : 1);
