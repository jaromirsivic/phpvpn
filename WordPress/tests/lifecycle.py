"""Exercise real WordPress activation, deactivation and uninstall commands.

Destructive to the plugin settings of a disposable local test site. Requires the
explicit vpn_review_disposable_fixture option, a loopback URL and a real copy
of the plugin, never a source-tree symlink. No credentials are printed.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--php', required=True)
    parser.add_argument('--wp-cli', required=True)
    parser.add_argument('--site', type=Path, required=True)
    args = parser.parse_args()
    site = args.site.resolve()
    plugin = site / 'wp-content/plugins/vpn-server'
    assert plugin.is_dir() and not plugin.is_symlink()
    assert plugin.resolve().is_relative_to(site), 'Plugin must be an isolated physical copy.'
    cli = [args.php, args.wp_cli, '--path=' + str(site), '--skip-plugins=plugin-check', '--skip-themes']

    def wp(*command):
        result = subprocess.run(cli + list(command), capture_output=True, text=True, encoding='utf-8', errors='replace')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        # Warnings must not be silently treated as a successful lifecycle operation.
        for marker in ('Warning:', 'Fatal error:', 'Deprecated:'):
            assert marker not in result.stdout + result.stderr, result.stdout + result.stderr
        return result.stdout.strip()

    assert wp('option', 'get', 'vpn_review_disposable_fixture') == '1', 'Not a marked disposable fixture.'
    assert wp('option', 'get', 'home').startswith('http://127.0.0.1:'), 'Not a loopback fixture.'
    with tempfile.TemporaryDirectory(prefix='vpn-lifecycle-', dir=site.parent) as temporary:
        directory = Path(temporary).resolve()
        assert directory.is_relative_to(site.parent)

        def evaluate(body):
            source = directory / 'phase.php'
            source.write_text('<?php\n' + body, encoding='utf-8')
            print(wp('eval-file', str(source)))

        evaluate(r'''
use Pilandrome\VpnServer\Settings;
use Pilandrome\VpnServer\Fast_Route;
require_once WP_PLUGIN_DIR . '/vpn-server/includes/server-core.php';
$settings = Settings::get();
$folder = Settings::default_storage();
if ( ! (new \PhpVpn\DiskStorage($folder))->isUsable() ) { throw new RuntimeException('Cannot prepare managed folder.'); }
file_put_contents($folder . '/owned-test.txt', 'disposable managed test');
$settings['storage'] = $folder;
$settings['storage_managed'] = true;
Settings::save($settings);
set_transient('vpn_server_self_test', array('review'), 3600);
update_option('vpn_review_managed_test_path', $folder);
update_option('vpn_review_expected_settings_hash', hash('sha256', wp_json_encode(Settings::get())));
if ( true !== Fast_Route::install() ) { throw new RuntimeException('Fast route installation failed.'); }
echo "PASS: Prepared managed storage, settings, transient and fast route.\n";
''')
        print(wp('plugin', 'deactivate', 'vpn-server'))
        evaluate(r'''
require_once WP_PLUGIN_DIR . '/vpn-server/includes/class-settings.php';
$actual = hash('sha256', wp_json_encode(\Pilandrome\VpnServer\Settings::get()));
if (get_option('vpn_review_expected_settings_hash') !== $actual) { throw new RuntimeException('Deactivation changed settings.'); }
if (!is_file(get_option('vpn_review_managed_test_path') . '/owned-test.txt')) { throw new RuntimeException('Deactivation removed data.'); }
if (file_exists(WPMU_PLUGIN_DIR . '/vpn-server-fast-route.php')) { throw new RuntimeException('Deactivation left fast route.'); }
echo "PASS: Deactivation preserves settings and data and removes the fast route.\n";
''')
        print(wp('plugin', 'activate', 'vpn-server'))
        evaluate(r'''
if (get_option('vpn_review_expected_settings_hash') !== hash('sha256', wp_json_encode(\Pilandrome\VpnServer\Settings::get()))) { throw new RuntimeException('Reactivation changed settings.'); }
echo "PASS: Reactivation preserves configuration.\n";
''')
        # Use the same two-step sequence as the WordPress admin: deactivate, then delete.
        print(wp('plugin', 'deactivate', 'vpn-server'))
        print(wp('plugin', 'uninstall', 'vpn-server', '--skip-delete'))
        evaluate(r'''
if (false !== get_option('vpn_server_settings')) { throw new RuntimeException('Settings survived uninstall.'); }
if (false !== get_transient('vpn_server_self_test')) { throw new RuntimeException('Transient survived uninstall.'); }
if (file_exists(get_option('vpn_review_managed_test_path'))) { throw new RuntimeException('Managed storage survived uninstall.'); }
if (file_exists(WPMU_PLUGIN_DIR . '/vpn-server-fast-route.php')) { throw new RuntimeException('Loader survived uninstall.'); }
echo "PASS: Uninstall removes settings, transient, managed storage and loader.\n";
''')
        print(wp('plugin', 'activate', 'vpn-server'))
        evaluate(r'''
$settings = \Pilandrome\VpnServer\Settings::get();
$folder = dirname(untrailingslashit(ABSPATH)) . '/lifecycle-custom-storage';
wp_mkdir_p($folder);
file_put_contents($folder . '/preserve.txt', 'administrator owned');
$settings['storage'] = $folder;
$settings['storage_managed'] = false;
\Pilandrome\VpnServer\Settings::save($settings);
update_option('vpn_review_custom_test_path', $folder);
echo "PASS: Prepared administrator-selected storage.\n";
''')
        print(wp('plugin', 'deactivate', 'vpn-server'))
        print(wp('plugin', 'uninstall', 'vpn-server', '--skip-delete'))
        evaluate(r'''
if ('administrator owned' !== file_get_contents(get_option('vpn_review_custom_test_path') . '/preserve.txt')) { throw new RuntimeException('Custom storage was modified.'); }
if (false !== get_option('vpn_server_settings')) { throw new RuntimeException('Settings survived custom-storage uninstall.'); }
echo "PASS: Uninstall preserves administrator-selected storage.\n";
''')
    print('All lifecycle checks passed. Plugin files retained, plugin inactive and uninstalled.')


if __name__ == '__main__':
    main()
