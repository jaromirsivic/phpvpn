<?php
/**
 * Optional fast route: a small must-use plugin that answers client requests before the
 * regular plugins load. An administrator turns it on explicitly in Settings, VPN Server.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Writes and removes wp-content/mu-plugins/vpn-server-fast-route.php.
 */
final class Fast_Route {

	const FILE_NAME = 'vpn-server-fast-route.php';

	/** Marker that identifies a loader written by this plugin. */
	const MARKER = 'Managed by the VPN Server plugin.';

	/**
	 * Absolute path of the loader.
	 */
	public static function path(): string {
		return trailingslashit( wp_normalize_path( WPMU_PLUGIN_DIR ) ) . self::FILE_NAME;
	}

	/**
	 * True when a loader of this plugin is installed.
	 */
	public static function is_installed(): bool {
		$path = self::path();
		if ( ! is_readable( $path ) ) {
			return false;
		}
		$text = file_get_contents( $path ); // phpcs:ignore WordPress.WP.AlternativeFunctions.file_get_contents_file_get_contents -- local file.
		return is_string( $text ) && str_contains( $text, self::MARKER );
	}

	/**
	 * True when the loader belongs to the plugin copy that is running now.
	 */
	public static function is_current(): bool {
		$text = self::is_installed() ? file_get_contents( self::path() ) : false; // phpcs:ignore WordPress.WP.AlternativeFunctions.file_get_contents_file_get_contents -- local file.
		return is_string( $text ) && $text === self::contents();
	}

	/**
	 * Writes the loader.
	 *
	 * @return true|\WP_Error
	 */
	public static function install() {
		$filesystem = self::filesystem();
		if ( null === $filesystem ) {
			return new \WP_Error( 'vpn_server_filesystem', __( 'WordPress cannot write files directly on this server.', 'vpn-server' ) );
		}
		$directory = trailingslashit( wp_normalize_path( WPMU_PLUGIN_DIR ) );
		if ( ! $filesystem->is_dir( $directory ) && ! $filesystem->mkdir( $directory, FS_CHMOD_DIR ) ) {
			return new \WP_Error( 'vpn_server_mu_dir', __( 'The mu-plugins folder could not be created.', 'vpn-server' ) );
		}
		if ( ! $filesystem->put_contents( self::path(), self::contents(), FS_CHMOD_FILE ) ) {
			return new \WP_Error( 'vpn_server_mu_write', __( 'The fast route file could not be written.', 'vpn-server' ) );
		}
		return true;
	}

	/**
	 * Deactivation hook. On multisite the loader is shared by the network, so it stays while
	 * another site still runs the plugin. The loader itself checks the plugins of each site.
	 */
	public static function deactivate(): void {
		if ( is_multisite() ) {
			$basename = plugin_basename( PLUGIN_FILE );
			foreach ( get_sites( array( 'fields' => 'ids', 'number' => 0 ) ) as $site ) {
				if ( (int) $site !== get_current_blog_id() && in_array( $basename, (array) get_blog_option( (int) $site, 'active_plugins', array() ), true ) ) {
					return;
				}
			}
		}
		self::uninstall();
	}

	/**
	 * Removes the loader if it was written by this plugin.
	 */
	public static function uninstall(): void {
		if ( ! self::is_installed() ) {
			return;
		}
		$filesystem = self::filesystem();
		if ( null !== $filesystem ) {
			$filesystem->delete( self::path() );
		}
	}

	/**
	 * Loader source. It finds this plugin below WP_PLUGIN_DIR at run time, so a moved site and a
	 * symlinked plugin folder keep working and plugins_url() resolves the assets.
	 */
	private static function contents(): string {
		$basename = plugin_basename( PLUGIN_FILE );
		return '<?php' . "\n"
			. "/**\n"
			. " * Plugin Name: VPN Server fast route\n"
			. " * Description: Answers VPN Server client requests before the other plugins load. " . self::MARKER . " It is removed when VPN Server is deactivated.\n"
			. ' * Version: ' . VERSION . "\n"
			. " *\n"
			. " * @package VpnServer\n"
			. " */\n\n"
			. "if ( ! defined( 'ABSPATH' ) ) {\n\texit;\n}\n\n"
			. "( static function () {\n"
			. "\t\$active = (array) get_option( 'active_plugins', array() );\n"
			. "\tif ( ! in_array( " . var_export( $basename, true ) . ", \$active, true ) ) {\n" // phpcs:ignore WordPress.PHP.DevelopmentFunctions.error_log_var_export -- builds a PHP string literal.
			. "\t\treturn;\n"
			. "\t}\n"
			. "\t\$main   = WP_PLUGIN_DIR . '/' . " . var_export( $basename, true ) . ";\n" // phpcs:ignore WordPress.PHP.DevelopmentFunctions.error_log_var_export -- builds a PHP string literal.
			. "\t\$server = dirname( \$main ) . '/includes/class-server.php';\n"
			. "\tif ( ! is_readable( \$main ) || ! is_readable( \$server ) ) {\n"
			. "\t\treturn;\n"
			. "\t}\n"
			. "\trequire_once \$server;\n"
			. "\t\\Pilandrome\\VpnServer\\Server::maybe_serve( \$main );\n"
			. "} )();\n";
	}

	/**
	 * WP_Filesystem with direct access, null when only FTP or SSH access is available.
	 *
	 * @return \WP_Filesystem_Base|null
	 */
	private static function filesystem() {
		require_once ABSPATH . 'wp-admin/includes/file.php';
		if ( 'direct' !== get_filesystem_method() || ! WP_Filesystem() ) {
			return null;
		}
		global $wp_filesystem;
		return $wp_filesystem;
	}
}
