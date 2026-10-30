<?php
/**
 * Plugin settings, stored in one autoloaded option so a client request needs no extra query.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Reads, validates and writes the vpn_server_settings option.
 */
final class Settings {

	const OPTION           = 'vpn_server_settings';
	const DEFAULT_ENDPOINT = 'vpn-server';

	/** Default server secret of phpvpn.php and of the client Configuration Wizard. */
	const DEFAULT_SECRET = 'CHANGE_THIS_SERVER_SECRET';

	/** Shortest server secret that the settings show as secure. */
	const SECURE_SECRET_LENGTH = 32;

	/** Last octets of the clients created on activation, the ACCESS_KEYS of phpvpn.php. */
	const DEFAULT_CLIENTS = array( 2, 15 );

	/**
	 * Entries of the client select box on the public status page, the CLIENT_DOWNLOADS of phpvpn.php.
	 * A type 'file' entry shows a Download button, a 'link' entry an Open button that opens the URL in
	 * a new tab. A non-empty os preselects the entry for the visitor's system.
	 */
	const DEFAULT_CLIENT_DOWNLOADS = array(
		array(
			'name' => 'Windows 10, 11+ (x64, arm64)',
			'url'  => 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-win.zip',
			'type' => 'file',
			'os'   => 'windows',
		),
		array(
			'name' => 'macOS',
			'url'  => 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-macos.zip',
			'type' => 'file',
			'os'   => 'macos',
		),
		array(
			'name' => 'Linux (DEB, RPM - x64, arm64)',
			'url'  => 'https://www.pilandrome.com/downloads/phpvpn/client/1.x/phpvpn-linux.zip',
			'type' => 'file',
			'os'   => 'linux',
		),
		array(
			'name' => 'Source Code',
			'url'  => 'https://github.com/pilandrome/phpvpn',
			'type' => 'link',
			'os'   => '',
		),
	);

	/** Download types and the systems an entry can be preselected for. */
	const DOWNLOAD_TYPES   = array( 'file', 'link' );
	const DOWNLOAD_SYSTEMS = array( '', 'windows', 'macos', 'linux' );

	/** Most entries of the client select box. */
	const MAX_CLIENT_DOWNLOADS = 20;

	/** Longest entry name in characters. */
	const MAX_DOWNLOAD_NAME_LENGTH = 100;

	/**
	 * Default poll intervals in milliseconds, the MIN_DATA_POLL_INTERVAL_MS and MAX_DATA_POLL_INTERVAL_MS
	 * of phpvpn.php. tools/build-wordpress-plugin.py copies them from phpvpn.php and checks them.
	 */
	const DEFAULT_MIN_DATA_POLL_INTERVAL_MS = 50;
	const DEFAULT_MAX_DATA_POLL_INTERVAL_MS = 1800;

	/** Lowest and highest poll intervals in milliseconds that the settings accept. */
	const MIN_DATA_POLL_INTERVAL_RANGE = array( 25, 500 );
	const MAX_DATA_POLL_INTERVAL_RANGE = array( 500, 5000 );

	/** Storage folders the plugin creates itself, the only ones uninstall removes. */
	const MANAGED_STORAGE_PATTERN = '/^vpn-server-[0-9a-f]{16}$/';

	/**
	 * Current settings with defaults for missing or malformed members.
	 *
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool, client_downloads: list<array{name: string, url: string, type: string, os: string}>, min_data_poll_interval_ms: int, max_data_poll_interval_ms: int}
	 */
	public static function get(): array {
		$stored = get_option( self::OPTION );
		return self::normalize( is_array( $stored ) ? $stored : array() );
	}

	/**
	 * Saves complete settings.
	 *
	 * @param array<string, mixed> $values Settings as returned by get().
	 */
	public static function save( array $values ): void {
		update_option( self::OPTION, self::normalize( $values ), true );
	}

	/**
	 * Keeps only known members of the expected types.
	 *
	 * @param array<mixed, mixed> $values Raw option value.
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool, client_downloads: list<array{name: string, url: string, type: string, os: string}>, min_data_poll_interval_ms: int, max_data_poll_interval_ms: int}
	 */
	public static function normalize( array $values ): array {
		$keys = array();
		if ( isset( $values['access_keys'] ) && is_array( $values['access_keys'] ) ) {
			foreach ( $values['access_keys'] as $address => $key ) {
				if ( is_string( $address ) && is_string( $key ) ) {
					$keys[ $address ] = $key;
				}
			}
		}
		$endpoint  = isset( $values['endpoint'] ) && is_string( $values['endpoint'] ) ? self::sanitize_endpoint( $values['endpoint'] ) : '';
		$downloads = array();
		if ( isset( $values['client_downloads'] ) && is_array( $values['client_downloads'] ) ) {
			// A saved empty list stays empty, it hides the select box on the status page.
			foreach ( $values['client_downloads'] as $entry ) {
				$download = is_array( $entry ) ? self::sanitize_download( $entry ) : null;
				if ( null !== $download && count( $downloads ) < self::MAX_CLIENT_DOWNLOADS ) {
					$downloads[] = $download;
				}
			}
		} else {
			$downloads = self::DEFAULT_CLIENT_DOWNLOADS;
			// Settings of version 1.0.2 and older kept one URL per system, their custom URLs are kept.
			if ( isset( $values['client_download_urls'] ) && is_array( $values['client_download_urls'] ) ) {
				foreach ( $downloads as $index => $download ) {
					$value = $values['client_download_urls'][ $download['os'] ] ?? '';
					$url   = is_string( $value ) && '' !== $download['os'] ? self::sanitize_download_url( $value ) : '';
					// The old default of Windows named the archive phpvpn-win-x64.zip, the new default replaces it.
					if ( '' !== $url && ! str_ends_with( $url, '/phpvpn/client/1.x/phpvpn-win-x64.zip' ) ) {
						$downloads[ $index ]['url'] = $url;
					}
				}
			}
		}
		return array(
			'secret'                    => isset( $values['secret'] ) && is_string( $values['secret'] ) ? $values['secret'] : '',
			'access_keys'               => $keys,
			'endpoint'                  => '' === $endpoint ? self::DEFAULT_ENDPOINT : $endpoint,
			'storage'                   => isset( $values['storage'] ) && is_string( $values['storage'] ) ? $values['storage'] : '',
			'storage_managed'           => ! empty( $values['storage_managed'] ),
			'status_page'               => ! isset( $values['status_page'] ) || (bool) $values['status_page'],
			'client_downloads'          => $downloads,
			'min_data_poll_interval_ms' => self::poll_interval( $values['min_data_poll_interval_ms'] ?? null, self::MIN_DATA_POLL_INTERVAL_RANGE ) ?? self::DEFAULT_MIN_DATA_POLL_INTERVAL_MS,
			'max_data_poll_interval_ms' => self::poll_interval( $values['max_data_poll_interval_ms'] ?? null, self::MAX_DATA_POLL_INTERVAL_RANGE ) ?? self::DEFAULT_MAX_DATA_POLL_INTERVAL_MS,
		);
	}

	/**
	 * Validates a poll interval in milliseconds.
	 *
	 * @param mixed $value Integer or string of decimal digits.
	 * @param int[] $range Lowest and highest accepted value.
	 * @return int|null Null when the value is not a whole number inside the range.
	 */
	public static function poll_interval( $value, array $range ): ?int {
		if ( is_string( $value ) && preg_match( '/^[0-9]{1,9}$/', trim( $value ) ) ) {
			$value = (int) trim( $value );
		}
		return is_int( $value ) && $value >= $range[0] && $value <= $range[1] ? $value : null;
	}

	/**
	 * Sanitizes one entry of the client select box.
	 *
	 * @param array<mixed, mixed> $entry Raw entry with name, url, type and os.
	 * @return array{name: string, url: string, type: string, os: string}|null Null for an invalid entry.
	 */
	public static function sanitize_download( array $entry ): ?array {
		$name = isset( $entry['name'] ) && is_string( $entry['name'] ) ? sanitize_text_field( $entry['name'] ) : '';
		$url  = isset( $entry['url'] ) && is_string( $entry['url'] ) ? self::sanitize_download_url( $entry['url'] ) : '';
		$type = $entry['type'] ?? 'file';
		$os   = $entry['os'] ?? '';
		if ( '' === $name || mb_strlen( $name ) > self::MAX_DOWNLOAD_NAME_LENGTH || '' === $url
			|| ! in_array( $type, self::DOWNLOAD_TYPES, true ) || ! in_array( $os, self::DOWNLOAD_SYSTEMS, true ) ) {
			return null;
		}
		return array(
			'name' => $name,
			'url'  => $url,
			'type' => $type,
			'os'   => $os,
		);
	}

	/**
	 * Sanitizes an absolute HTTP(S) download link. The URL is never fetched by the server.
	 *
	 * @param string $value Raw input.
	 */
	public static function sanitize_download_url( string $value ): string {
		if ( ! preg_match( '~^https?://~i', trim( $value ) ) ) {
			return '';
		}
		$url   = sanitize_url( $value, array( 'http', 'https' ) );
		$parts = wp_parse_url( $url );
		if ( ! is_array( $parts ) || empty( $parts['host'] ) || ! isset( $parts['scheme'] )
			|| ! in_array( strtolower( $parts['scheme'] ), array( 'http', 'https' ), true ) ) {
			return '';
		}
		return $url;
	}

	/**
	 * Endpoint path below the site address: lowercase segments of letters, digits, '-' and '_'.
	 *
	 * @param string $value Raw input.
	 */
	public static function sanitize_endpoint( string $value ): string {
		$segments = array();
		foreach ( explode( '/', strtolower( $value ) ) as $segment ) {
			$segment = preg_replace( '/[^a-z0-9_-]/', '', $segment );
			if ( '' !== $segment ) {
				$segments[] = $segment;
			}
		}
		return implode( '/', $segments );
	}

	/**
	 * Server URL the clients use. The client appends ?action=..., so it never carries a query.
	 *
	 * @param array{endpoint: string} $settings Current settings.
	 */
	public static function endpoint_url( array $settings ): string {
		return home_url( '/' . $settings['endpoint'] . '/' );
	}

	/**
	 * Lowercase hexadecimal text of random bytes.
	 *
	 * @param int $bytes Number of random bytes.
	 */
	public static function random_hex( int $bytes ): string {
		return bin2hex( random_bytes( $bytes ) );
	}

	/**
	 * Why a server secret is weak: 'default' for the default or an all zero secret, 'short' below
	 * SECURE_SECRET_LENGTH characters, '' for a secret that is shown as secure.
	 *
	 * @param string $secret Server secret.
	 */
	public static function secret_weakness( string $secret ): string {
		if ( self::DEFAULT_SECRET === $secret || str_repeat( '0', strlen( $secret ) ) === $secret ) {
			return 'default';
		}
		return strlen( $secret ) < self::SECURE_SECRET_LENGTH ? 'short' : '';
	}

	/**
	 * Default access key of a client as in phpvpn.php and the client Configuration Wizard: the last
	 * octet of the VPN address as one hexadecimal byte, repeated 32 times.
	 *
	 * @param string $address VPN address of the client.
	 */
	public static function default_access_key( string $address ): string {
		$octets = explode( '.', $address );
		return str_repeat( sprintf( '%02x', (int) end( $octets ) & 0xff ), 32 );
	}

	/**
	 * Default storage folder: a random, unguessable name inside the uploads folder.
	 *
	 * @return string Absolute path, empty when the uploads folder is unavailable.
	 */
	public static function default_storage(): string {
		$uploads = wp_upload_dir( null, false );
		if ( ! empty( $uploads['error'] ) || empty( $uploads['basedir'] ) ) {
			return '';
		}
		return trailingslashit( wp_normalize_path( $uploads['basedir'] ) ) . 'vpn-server-' . self::random_hex( 8 );
	}

	/**
	 * True when the storage folder was created by the plugin inside the uploads folder.
	 *
	 * @param array{storage: string, storage_managed: bool} $settings Current settings.
	 */
	public static function is_managed_storage( array $settings ): bool {
		return $settings['storage_managed'] && 1 === preg_match( self::MANAGED_STORAGE_PATTERN, basename( $settings['storage'] ) );
	}

	/**
	 * Activation: refuses 32-bit PHP and creates the settings once, existing values stay.
	 *
	 * @param bool $network_wide True for a network activation on multisite.
	 */
	public static function activate( $network_wide = false ): void {
		if ( PHP_INT_SIZE < 8 ) {
			deactivate_plugins( plugin_basename( PLUGIN_FILE ) );
			wp_die( esc_html__( 'VPN Server requires 64-bit PHP.', 'vpn-server' ) );
		}
		if ( $network_wide ) {
			deactivate_plugins( plugin_basename( PLUGIN_FILE ), true, true );
			wp_die( esc_html__( 'VPN Server cannot be network activated. Activate it on each site that should run a VPN server.', 'vpn-server' ) );
		}
		if ( false !== get_option( self::OPTION ) ) {
			return;
		}
		require_once __DIR__ . '/server-core.php';
		$storage = self::default_storage();
		$keys    = array();
		foreach ( range( self::DEFAULT_CLIENTS[0], self::DEFAULT_CLIENTS[1] ) as $octet ) {
			$address          = \PhpVpn\VPN_ADDRESS_PREFIX . $octet;
			$keys[ $address ] = self::default_access_key( $address );
		}
		add_option(
			self::OPTION,
			self::normalize(
				array(
					// A random secret like Generate New Secret, the clients get the default keys of phpvpn.php.
					'secret'          => self::random_hex( 32 ),
					'access_keys'     => $keys,
					'endpoint'        => self::DEFAULT_ENDPOINT,
					'storage'         => $storage,
					'storage_managed' => '' !== $storage,
					'status_page'     => true,
				)
			),
			'',
			true
		);
	}
}
