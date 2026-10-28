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

	/** Storage folders the plugin creates itself, the only ones uninstall removes. */
	const MANAGED_STORAGE_PATTERN = '/^vpn-server-[0-9a-f]{16}$/';

	/**
	 * Current settings with defaults for missing or malformed members.
	 *
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool}
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
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool}
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
		$endpoint = isset( $values['endpoint'] ) && is_string( $values['endpoint'] ) ? self::sanitize_endpoint( $values['endpoint'] ) : '';
		return array(
			'secret'          => isset( $values['secret'] ) && is_string( $values['secret'] ) ? $values['secret'] : '',
			'access_keys'     => $keys,
			'endpoint'        => '' === $endpoint ? self::DEFAULT_ENDPOINT : $endpoint,
			'storage'         => isset( $values['storage'] ) && is_string( $values['storage'] ) ? $values['storage'] : '',
			'storage_managed' => ! empty( $values['storage_managed'] ),
			'status_page'     => ! isset( $values['status_page'] ) || (bool) $values['status_page'],
		);
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
					// The same defaults as phpvpn.php and the client wizard. The settings screen and
					// the status page show that this secret is insecure until it is replaced.
					'secret'          => self::DEFAULT_SECRET,
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
