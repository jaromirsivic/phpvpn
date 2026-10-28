<?php
/**
 * Settings, VPN Server: endpoint, clients and access keys, server secret, storage and checks.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Administration screen. Every change goes through admin-post.php with a nonce and a capability check.
 */
final class Admin {

	const PAGE       = 'vpn-server';
	const CAPABILITY = 'manage_options';
	const NOTICE     = 'vpn-server-notice';
	const SELF_TEST  = 'vpn_server_self_test';

	/** Default request headers of the PHP VPN client, client/src/core/config/config.h. */
	const CLIENT_HTTP_HEADERS = "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0";

	/** First path segments that belong to WordPress itself. */
	const RESERVED_ENDPOINTS = array( 'wp-admin', 'wp-content', 'wp-includes', 'wp-json', 'feed', 'xmlrpc.php' );

	/** admin-post.php actions, without the vpn_server_ prefix, and their handlers. */
	const ACTIONS = array(
		'save'           => 'handle_save',
		'secret'         => 'handle_secret',
		'add_client'     => 'handle_add_client',
		'client_key'     => 'handle_client_key',
		'remove_client'  => 'handle_remove_client',
		'client_config'  => 'handle_client_config',
		'fast_route_on'  => 'handle_fast_route_on',
		'fast_route_off' => 'handle_fast_route_off',
		'self_test'      => 'handle_self_test',
	);

	/**
	 * Hooks the screen.
	 */
	public static function register(): void {
		add_action( 'admin_menu', array( self::class, 'menu' ) );
		add_action( 'admin_enqueue_scripts', array( self::class, 'enqueue' ) );
		add_filter( 'plugin_action_links_' . plugin_basename( PLUGIN_FILE ), array( self::class, 'action_links' ) );
		foreach ( self::ACTIONS as $action => $handler ) {
			add_action( 'admin_post_vpn_server_' . $action, array( self::class, $handler ) );
		}
	}

	/**
	 * Adds Settings, VPN Server.
	 */
	public static function menu(): void {
		add_options_page(
			__( 'VPN Server', 'vpn-server' ),
			__( 'VPN Server', 'vpn-server' ),
			self::CAPABILITY,
			self::PAGE,
			array( self::class, 'render' )
		);
	}

	/**
	 * Adds a Settings link to the plugin row.
	 *
	 * @param string[] $links Existing links.
	 * @return string[]
	 */
	public static function action_links( $links ): array {
		$links   = is_array( $links ) ? $links : array();
		$links[] = '<a href="' . esc_url( self::page_url() ) . '">' . esc_html__( 'Settings', 'vpn-server' ) . '</a>';
		return $links;
	}

	/**
	 * Loads the screen assets on this screen only.
	 *
	 * @param string $hook_suffix Current admin screen.
	 */
	public static function enqueue( $hook_suffix ): void {
		if ( 'settings_page_' . self::PAGE !== $hook_suffix ) {
			return;
		}
		wp_enqueue_style( 'vpn-server-admin', plugins_url( 'assets/admin.css', PLUGIN_FILE ), array(), VERSION );
		wp_enqueue_script( 'vpn-server-admin', plugins_url( 'assets/admin.js', PLUGIN_FILE ), array(), VERSION, array( 'in_footer' => true ) );
		wp_localize_script(
			'vpn-server-admin',
			'vpnServerAdmin',
			array(
				'copied' => __( 'Copied', 'vpn-server' ),
			)
		);
	}

	/**
	 * URL of this screen.
	 */
	public static function page_url(): string {
		return admin_url( 'options-general.php?page=' . self::PAGE );
	}

	// ---------------------------------------------------------------------
	// Screen
	// ---------------------------------------------------------------------

	/**
	 * Renders the screen.
	 */
	public static function render(): void {
		if ( ! current_user_can( self::CAPABILITY ) ) {
			return;
		}
		require_once __DIR__ . '/relay-core.php';
		$settings = Settings::get();
		?>
		<div class="wrap vpn-server">
			<h1><?php echo esc_html__( 'VPN Server', 'vpn-server' ); ?></h1>
			<?php self::render_notice(); ?>
			<p><?php echo esc_html__( 'This site relays encrypted traffic between PHP VPN clients. The relay never sees the payload: clients encrypt it with keys that only they know.', 'vpn-server' ); ?></p>
			<?php
			self::render_connection( $settings );
			self::render_clients( $settings );
			self::render_secret( $settings );
			self::render_storage( $settings );
			self::render_fast_route();
			self::render_checks( $settings );
			?>
		</div>
		<?php
	}

	/**
	 * Result of the last admin-post action.
	 */
	private static function render_notice(): void {
		// Only a code from the redirect of a nonce checked action, mapped to fixed messages.
		// phpcs:ignore WordPress.Security.NonceVerification.Recommended
		$code     = isset( $_GET[ self::NOTICE ] ) ? sanitize_key( wp_unslash( $_GET[ self::NOTICE ] ) ) : '';
		$messages = array(
			'saved'              => array( 'success', __( 'Settings saved.', 'vpn-server' ) ),
			'secret'             => array( 'success', __( 'The server secret was changed. Update serverSecret in every client.', 'vpn-server' ) ),
			'client_added'       => array( 'success', __( 'Client added. Download its configuration and import it in the client.', 'vpn-server' ) ),
			'client_key'         => array( 'success', __( 'A new access key was generated. Update accessKey in that client.', 'vpn-server' ) ),
			'client_removed'     => array( 'success', __( 'Client removed.', 'vpn-server' ) ),
			'fast_route_on'      => array( 'success', __( 'The fast route is installed.', 'vpn-server' ) ),
			'fast_route_off'     => array( 'success', __( 'The fast route is removed.', 'vpn-server' ) ),
			'self_test'          => array( 'info', __( 'The self test finished, see Checks below.', 'vpn-server' ) ),
			'bad_endpoint'       => array( 'error', __( 'The endpoint path is empty or reserved by WordPress.', 'vpn-server' ) ),
			'bad_storage'        => array( 'error', __( 'The storage folder must be an absolute path to a dedicated folder that WordPress can create and write. It cannot be the site root, wp-content, the uploads folder itself, a folder that contains the site, or a folder inside the plugins, themes, wp-admin or wp-includes folders.', 'vpn-server' ) ),
			'network_only'       => array( 'error', __( 'On a multisite network only a super admin can install the fast route or move the storage folder outside the uploads folder.', 'vpn-server' ) ),
			'bad_secret'         => array( 'error', __( 'The server secret must have 16 to 256 printable ASCII characters without spaces.', 'vpn-server' ) ),
			'bad_address'        => array( 'error', __( 'The VPN address is invalid or already used.', 'vpn-server' ) ),
			'bad_key'            => array( 'error', __( 'The access key must have exactly 64 hexadecimal characters.', 'vpn-server' ) ),
			'no_address'         => array( 'error', __( 'All VPN addresses are already used.', 'vpn-server' ) ),
			'unknown_client'     => array( 'error', __( 'The client does not exist.', 'vpn-server' ) ),
			'fast_route_failed'  => array( 'error', __( 'The fast route could not be installed. WordPress needs direct write access to wp-content/mu-plugins.', 'vpn-server' ) ),
		);
		if ( ! isset( $messages[ $code ] ) ) {
			return;
		}
		printf(
			'<div class="notice notice-%1$s is-dismissible"><p>%2$s</p></div>',
			esc_attr( $messages[ $code ][0] ),
			esc_html( $messages[ $code ][1] )
		);
	}

	/**
	 * Opens a form that posts to admin-post.php with a nonce.
	 *
	 * @param string $action  Action without the vpn_server_ prefix.
	 * @param string $classes Extra CSS classes.
	 */
	private static function form_start( string $action, string $classes = '' ): void {
		printf( '<form method="post" action="%1$s" class="%2$s">', esc_url( admin_url( 'admin-post.php' ) ), esc_attr( trim( 'vpn-server-form ' . $classes ) ) );
		printf( '<input type="hidden" name="action" value="%s">', esc_attr( 'vpn_server_' . $action ) );
		wp_nonce_field( 'vpn_server_' . $action );
	}

	/**
	 * Server URL and endpoint path.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_connection( array $settings ): void {
		$url = Settings::endpoint_url( $settings );
		?>
		<h2><?php echo esc_html__( 'Connection', 'vpn-server' ); ?></h2>
		<?php self::form_start( 'save' ); ?>
		<table class="form-table" role="presentation">
			<tr>
				<th scope="row"><?php echo esc_html__( 'Server URL', 'vpn-server' ); ?></th>
				<td>
					<input type="text" class="large-text code" id="vpn-server-url" value="<?php echo esc_attr( $url ); ?>" readonly>
					<button type="button" class="button" data-vpn-server-copy="vpn-server-url"><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
					<p class="description"><?php echo esc_html__( 'Enter this address as Server URL in the PHP VPN client.', 'vpn-server' ); ?></p>
				</td>
			</tr>
			<tr>
				<th scope="row"><label for="vpn-server-endpoint"><?php echo esc_html__( 'Endpoint path', 'vpn-server' ); ?></label></th>
				<td>
					<code><?php echo esc_html( home_url( '/' ) ); ?></code><input type="text" id="vpn-server-endpoint" name="endpoint" class="regular-text code" value="<?php echo esc_attr( $settings['endpoint'] ); ?>"><code>/</code>
					<p class="description"><?php echo esc_html__( 'Letters, digits, hyphens, underscores and slashes. Changing it disconnects all clients until they use the new Server URL.', 'vpn-server' ); ?></p>
				</td>
			</tr>
			<tr>
				<th scope="row"><?php echo esc_html__( 'Status page', 'vpn-server' ); ?></th>
				<td>
					<label><input type="checkbox" name="status_page" value="1" <?php checked( $settings['status_page'] ); ?>> <?php echo esc_html__( 'Show the status page when the Server URL is opened in a browser', 'vpn-server' ); ?></label>
					<p class="description"><?php echo esc_html__( 'When it is off, a browser gets 404. The clients are not affected.', 'vpn-server' ); ?></p>
				</td>
			</tr>
			<tr>
				<th scope="row"><label for="vpn-server-storage"><?php echo esc_html__( 'Storage folder', 'vpn-server' ); ?></label></th>
				<td>
					<input type="text" id="vpn-server-storage" name="storage" class="large-text code" value="<?php echo esc_attr( $settings['storage'] ); ?>">
					<p class="description"><?php echo esc_html__( 'Absolute path of the message queues. A folder outside the web root is the safest choice. Queued messages are not moved when the folder changes, they expire within 60 seconds.', 'vpn-server' ); ?></p>
				</td>
			</tr>
		</table>
		<?php submit_button( __( 'Save Changes', 'vpn-server' ) ); ?>
		</form>
		<?php
	}

	/**
	 * Clients with their VPN address and access key.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_clients( array $settings ): void {
		$storage = '' !== $settings['storage'] ? new \PhpVpn\DiskStorage( $settings['storage'] ) : null;
		$keys    = $settings['access_keys'];
		uksort( $keys, static fn( $a, $b ): int => \PhpVpn\Validators::addressSortKey( (string) $a ) <=> \PhpVpn\Validators::addressSortKey( (string) $b ) );
		?>
		<h2><?php echo esc_html__( 'Clients', 'vpn-server' ); ?></h2>
		<p><?php echo esc_html__( 'Every client has its own VPN address and access key. Communication contracts and payload keys are set up between the clients themselves, the server never knows them.', 'vpn-server' ); ?></p>
		<table class="widefat striped vpn-server-clients">
			<thead>
				<tr>
					<th scope="col"><?php echo esc_html__( 'VPN address', 'vpn-server' ); ?></th>
					<th scope="col"><?php echo esc_html__( 'Access key', 'vpn-server' ); ?></th>
					<th scope="col"><?php echo esc_html__( 'Last seen', 'vpn-server' ); ?></th>
					<th scope="col"><?php echo esc_html__( 'Actions', 'vpn-server' ); ?></th>
				</tr>
			</thead>
			<tbody>
			<?php if ( array() === $keys ) : ?>
				<tr><td colspan="4"><?php echo esc_html__( 'No clients yet. Add the first one below.', 'vpn-server' ); ?></td></tr>
			<?php endif; ?>
			<?php foreach ( $keys as $address => $key ) : ?>
				<?php
				$address  = (string) $address;
				$field_id = 'vpn-server-key-' . str_replace( '.', '-', $address );
				$seen     = null !== $storage ? $storage->readLastSeenMs( $address ) : null;
				?>
				<tr>
					<td><code><?php echo esc_html( $address ); ?></code></td>
					<td>
						<input type="text" class="code vpn-server-key" id="<?php echo esc_attr( $field_id ); ?>" value="<?php echo esc_attr( $key ); ?>" readonly>
						<button type="button" class="button button-small" data-vpn-server-copy="<?php echo esc_attr( $field_id ); ?>"><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
					</td>
					<td>
						<?php
						if ( null === $seen ) {
							echo esc_html__( 'Never', 'vpn-server' );
						} else {
							/* translators: %s: human readable time difference, for example "5 mins". */
							echo esc_html( sprintf( __( '%s ago', 'vpn-server' ), human_time_diff( intdiv( $seen, 1000 ) ) ) );
						}
						?>
					</td>
					<td class="vpn-server-actions">
						<?php self::form_start( 'client_config', 'vpn-server-inline' ); ?>
							<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
							<button type="submit" class="button button-small"><?php echo esc_html__( 'Download config.json', 'vpn-server' ); ?></button>
						</form>
						<?php self::form_start( 'client_key', 'vpn-server-inline' ); ?>
							<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
							<button type="submit" class="button button-small" data-vpn-server-confirm="<?php echo esc_attr__( 'Generate a new access key? The client stops working until it gets the new key.', 'vpn-server' ); ?>"><?php echo esc_html__( 'New key', 'vpn-server' ); ?></button>
						</form>
						<?php self::form_start( 'remove_client', 'vpn-server-inline' ); ?>
							<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
							<button type="submit" class="button button-small button-link-delete" data-vpn-server-confirm="<?php echo esc_attr__( 'Remove this client? It can no longer connect.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Remove', 'vpn-server' ); ?></button>
						</form>
					</td>
				</tr>
			<?php endforeach; ?>
			</tbody>
		</table>

		<h3><?php echo esc_html__( 'Add client', 'vpn-server' ); ?></h3>
		<?php self::form_start( 'add_client' ); ?>
		<table class="form-table" role="presentation">
			<tr>
				<th scope="row"><label for="vpn-server-address"><?php echo esc_html__( 'VPN address', 'vpn-server' ); ?></label></th>
				<td>
					<select id="vpn-server-address" name="address">
						<option value=""><?php echo esc_html__( 'Next free address', 'vpn-server' ); ?></option>
						<?php
						foreach ( self::free_addresses( $settings ) as $address ) {
							printf( '<option value="%1$s">%1$s</option>', esc_attr( $address ) );
						}
						?>
					</select>
				</td>
			</tr>
			<tr>
				<th scope="row"><label for="vpn-server-new-key"><?php echo esc_html__( 'Access key', 'vpn-server' ); ?></label></th>
				<td>
					<input type="text" id="vpn-server-new-key" name="access_key" class="large-text code" autocomplete="off" placeholder="<?php echo esc_attr__( 'Leave empty to generate a random key', 'vpn-server' ); ?>">
					<p class="description"><?php echo esc_html__( 'Enter an existing key only when you move a client from another PHP VPN server.', 'vpn-server' ); ?></p>
				</td>
			</tr>
		</table>
		<?php submit_button( __( 'Add client', 'vpn-server' ), 'secondary' ); ?>
		</form>
		<?php
	}

	/**
	 * Server secret.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_secret( array $settings ): void {
		?>
		<h2><?php echo esc_html__( 'Server secret', 'vpn-server' ); ?></h2>
		<p><?php echo esc_html__( 'Shared by the server and all its clients. Changing it disconnects every client until it gets the new value.', 'vpn-server' ); ?></p>
		<?php self::form_start( 'secret' ); ?>
		<table class="form-table" role="presentation">
			<tr>
				<th scope="row"><label for="vpn-server-secret"><?php echo esc_html__( 'Server secret', 'vpn-server' ); ?></label></th>
				<td>
					<input type="text" id="vpn-server-secret" name="secret" class="large-text code" autocomplete="off" value="<?php echo esc_attr( $settings['secret'] ); ?>">
					<button type="button" class="button" data-vpn-server-copy="vpn-server-secret"><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
					<p class="description"><?php echo esc_html__( 'Keep the generated value, or enter the secret of the PHP VPN server you move from.', 'vpn-server' ); ?></p>
				</td>
			</tr>
		</table>
		<p class="submit">
			<button type="submit" name="mode" value="save" class="button button-primary" data-vpn-server-confirm="<?php echo esc_attr__( 'Change the server secret? All clients stop working until they get the new value.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Save secret', 'vpn-server' ); ?></button>
			<button type="submit" name="mode" value="generate" class="button" data-vpn-server-confirm="<?php echo esc_attr__( 'Generate a new server secret? All clients stop working until they get the new value.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Generate new secret', 'vpn-server' ); ?></button>
		</p>
		</form>
		<?php
	}

	/**
	 * Storage protection and the nginx rule.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_storage( array $settings ): void {
		$location = self::storage_url_path( $settings );
		?>
		<h2><?php echo esc_html__( 'Storage protection', 'vpn-server' ); ?></h2>
		<?php if ( null === $location ) : ?>
			<p><?php echo esc_html__( 'The storage folder is outside the uploads folder. Make sure the web server does not serve it.', 'vpn-server' ); ?></p>
		<?php else : ?>
			<p><?php echo esc_html__( 'The storage folder is inside the uploads folder. Apache and IIS are protected by the .htaccess and web.config files that the plugin writes there. On nginx add this rule to the server block and reload nginx:', 'vpn-server' ); ?></p>
			<pre class="vpn-server-snippet"><code><?php echo esc_html( 'location ^~ ' . $location . ' { deny all; return 404; }' ); ?></code></pre>
		<?php endif; ?>
		<?php
	}

	/**
	 * Optional mu-plugin.
	 */
	private static function render_fast_route(): void {
		$installed = Fast_Route::is_installed();
		?>
		<h2><?php echo esc_html__( 'Fast route', 'vpn-server' ); ?></h2>
		<p><?php echo esc_html__( 'Clients poll the server several times per second. By default a relay request is answered right after all plugins are loaded, before the theme. The fast route adds a small must-use plugin that answers it before the other plugins load, which helps on sites with many plugins. Other requests are not affected. It is removed when this plugin is deactivated.', 'vpn-server' ); ?></p>
		<p>
			<?php
			if ( ! $installed ) {
				echo esc_html__( 'Status: not installed.', 'vpn-server' );
			} elseif ( Fast_Route::is_current() ) {
				echo esc_html__( 'Status: installed.', 'vpn-server' );
			} else {
				echo esc_html__( 'Status: installed for another version or location of this plugin, install it again.', 'vpn-server' );
			}
			?>
		</p>
		<?php
		if ( ! self::can_manage_network_files() ) {
			echo '<p>' . esc_html__( 'The fast route serves the whole network, only a super admin can install or remove it.', 'vpn-server' ) . '</p>';
			return;
		}
		self::form_start( $installed && Fast_Route::is_current() ? 'fast_route_off' : 'fast_route_on' );
		?>
		<?php
		if ( $installed && Fast_Route::is_current() ) {
			submit_button( __( 'Remove fast route', 'vpn-server' ), 'secondary', 'submit', false );
		} else {
			submit_button( __( 'Install fast route', 'vpn-server' ), 'secondary', 'submit', false );
		}
		?>
		</form>
		<?php
	}

	/**
	 * Environment checks and the loopback self test.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_checks( array $settings ): void {
		$checks = self::checks( $settings );
		$test   = get_transient( self::SELF_TEST );
		?>
		<h2><?php echo esc_html__( 'Checks', 'vpn-server' ); ?></h2>
		<table class="widefat striped vpn-server-checks">
			<tbody>
			<?php foreach ( array_merge( $checks, is_array( $test ) ? $test : array() ) as $check ) : ?>
				<tr>
					<td class="vpn-server-check-<?php echo esc_attr( $check[0] ); ?>"><?php echo esc_html( 'ok' === $check[0] ? __( 'OK', 'vpn-server' ) : ( 'warning' === $check[0] ? __( 'Warning', 'vpn-server' ) : __( 'Error', 'vpn-server' ) ) ); ?></td>
					<td><?php echo esc_html( $check[1] ); ?></td>
				</tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		<?php self::form_start( 'self_test' ); ?>
		<p><?php echo esc_html__( 'The self test sends requests from this server to the Server URL and to the storage folder, the same way a client and a visitor would.', 'vpn-server' ); ?></p>
		<?php submit_button( __( 'Run self test', 'vpn-server' ), 'secondary', 'submit', false ); ?>
		</form>
		<?php
	}

	/**
	 * Checks that need no request.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return array<int, array{0: string, 1: string}> Level and message.
	 */
	private static function checks( array $settings ): array {
		$checks   = array();
		$checks[] = PHP_INT_SIZE >= 8
			? array( 'ok', __( 'PHP is 64-bit.', 'vpn-server' ) )
			: array( 'error', __( 'PHP is 32-bit. The relay requires 64-bit PHP.', 'vpn-server' ) );
		$checks[] = '' !== (string) get_option( 'permalink_structure' )
			? array( 'ok', __( 'Pretty permalinks are enabled.', 'vpn-server' ) )
			: array( 'error', __( 'Plain permalinks are used. The Server URL works only with pretty permalinks: choose another structure in Settings, Permalinks.', 'vpn-server' ) );
		$checks[] = 'https' === wp_parse_url( home_url(), PHP_URL_SCHEME )
			? array( 'ok', __( 'The site uses HTTPS.', 'vpn-server' ) )
			: array( 'warning', __( 'The site does not use HTTPS. The traffic stays encrypted, but HTTPS also hides addresses and timing from the network.', 'vpn-server' ) );
		if ( ini_get( 'zlib.output_compression' ) ) {
			$checks[] = array( 'warning', __( 'zlib.output_compression is enabled in PHP. Disable it for this site if the clients report damaged responses.', 'vpn-server' ) );
		}
		if ( url_to_postid( Settings::endpoint_url( $settings ) ) > 0 ) {
			$checks[] = array( 'warning', __( 'A page or post uses the same address as the endpoint. Visitors get the relay instead of that content, choose another endpoint path.', 'vpn-server' ) );
		}
		if ( array() === $settings['access_keys'] ) {
			$checks[] = array( 'warning', __( 'No client is configured yet.', 'vpn-server' ) );
		}
		$storage = (string) $settings['storage'];
		if ( '' === $storage ) {
			$checks[] = array( 'error', __( 'No storage folder is set.', 'vpn-server' ) );
		} elseif ( is_dir( $storage ) ) {
			$checks[] = wp_is_writable( $storage )
				? array( 'ok', __( 'The storage folder is writable.', 'vpn-server' ) )
				: array( 'error', __( 'The storage folder is not writable by PHP.', 'vpn-server' ) );
		} else {
			$checks[] = array( 'ok', __( 'The storage folder is created with the first client request.', 'vpn-server' ) );
		}
		return $checks;
	}

	// ---------------------------------------------------------------------
	// Actions
	// ---------------------------------------------------------------------

	/**
	 * Capability and nonce check of an admin-post action.
	 *
	 * @param string $action Action without the vpn_server_ prefix.
	 */
	private static function verify( string $action ): void {
		if ( ! current_user_can( self::CAPABILITY ) ) {
			wp_die( esc_html__( 'Sorry, you are not allowed to manage VPN Server.', 'vpn-server' ), '', array( 'response' => 403 ) );
		}
		check_admin_referer( 'vpn_server_' . $action );
	}

	/**
	 * Returns to the screen with a result code.
	 *
	 * @param string $notice Key of render_notice().
	 * @return never
	 */
	private static function done( string $notice ) {
		wp_safe_redirect( add_query_arg( self::NOTICE, $notice, self::page_url() ) );
		exit;
	}

	/**
	 * Text of a posted field, unslashed and sanitized.
	 *
	 * @param string $name Field name.
	 */
	private static function posted( string $name ): string {
		// Nonces are checked in verify() before any field is read.
		// phpcs:ignore WordPress.Security.NonceVerification.Missing
		return isset( $_POST[ $name ] ) && is_string( $_POST[ $name ] ) ? sanitize_text_field( wp_unslash( $_POST[ $name ] ) ) : '';
	}

	/**
	 * Saves endpoint, status page and storage folder.
	 */
	public static function handle_save(): void {
		self::verify( 'save' );
		$settings = Settings::get();

		$endpoint = Settings::sanitize_endpoint( self::posted( 'endpoint' ) );
		$first    = explode( '/', $endpoint )[0];
		if ( '' === $endpoint || in_array( $first, self::RESERVED_ENDPOINTS, true ) ) {
			self::done( 'bad_endpoint' );
		}

		$storage = untrailingslashit( wp_normalize_path( self::posted( 'storage' ) ) );
		if ( $storage !== $settings['storage'] ) {
			if ( ! self::is_allowed_storage( $storage, $settings ) ) {
				self::done( 'bad_storage' );
			}
			// On multisite only a super admin may place the queues outside the uploads folder.
			if ( is_multisite() && ! is_super_admin() && null === self::storage_url( array( 'storage' => $storage ) ) ) {
				self::done( 'network_only' );
			}
			require_once __DIR__ . '/relay-core.php';
			$disk = new \PhpVpn\DiskStorage( $storage );
			if ( ! $disk->isUsable() || ! wp_is_writable( $storage ) ) {
				self::done( 'bad_storage' );
			}
			// isUsable() writes the guards only into a folder it creates itself.
			$disk->writeGuards();
			if ( Settings::is_managed_storage( $settings ) ) {
				self::delete_folder( $settings['storage'] );
			}
			$settings['storage']         = $storage;
			$settings['storage_managed'] = false;
		}

		$settings['endpoint']    = $endpoint;
		$settings['status_page'] = '1' === self::posted( 'status_page' );
		Settings::save( $settings );
		delete_transient( self::SELF_TEST );
		self::done( 'saved' );
	}

	/**
	 * Saves an entered secret or generates a new one.
	 */
	public static function handle_secret(): void {
		self::verify( 'secret' );
		$settings = Settings::get();
		if ( 'generate' === self::posted( 'mode' ) ) {
			$settings['secret'] = Settings::random_hex( 32 );
		} else {
			// The exact bytes of the secret enter the key derivation, chapter 3.2. Only printable ASCII
			// without spaces is accepted, so a validated value never needs sanitizing.
			// phpcs:ignore WordPress.Security.NonceVerification.Missing, WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- validated by the pattern below.
			$secret = isset( $_POST['secret'] ) && is_string( $_POST['secret'] ) ? trim( wp_unslash( $_POST['secret'] ) ) : '';
			if ( 1 !== preg_match( '/^[\x21-\x7e]{16,256}$/', $secret ) ) {
				self::done( 'bad_secret' );
			}
			$settings['secret'] = $secret;
		}
		Settings::save( $settings );
		self::done( 'secret' );
	}

	/**
	 * Adds a client with the chosen or the next free address.
	 */
	public static function handle_add_client(): void {
		self::verify( 'add_client' );
		require_once __DIR__ . '/relay-core.php';
		$settings = Settings::get();
		$free     = self::free_addresses( $settings );
		$address  = self::posted( 'address' );
		if ( '' === $address ) {
			if ( array() === $free ) {
				self::done( 'no_address' );
			}
			$address = $free[0];
		} elseif ( ! in_array( $address, $free, true ) ) {
			self::done( 'bad_address' );
		}
		$key = strtolower( self::posted( 'access_key' ) );
		if ( '' === $key ) {
			$key = Settings::random_hex( 32 );
		} elseif ( 1 !== preg_match( '/^[0-9a-f]{64}$/', $key ) ) {
			self::done( 'bad_key' );
		}
		$settings['access_keys'][ $address ] = $key;
		Settings::save( $settings );
		self::done( 'client_added' );
	}

	/**
	 * Generates a new access key for a client.
	 */
	public static function handle_client_key(): void {
		self::verify( 'client_key' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$settings['access_keys'][ $address ] = Settings::random_hex( 32 );
		Settings::save( $settings );
		self::done( 'client_key' );
	}

	/**
	 * Removes a client.
	 */
	public static function handle_remove_client(): void {
		self::verify( 'remove_client' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		unset( $settings['access_keys'][ $address ] );
		Settings::save( $settings );
		self::done( 'client_removed' );
	}

	/**
	 * Sends config.json for one client: the masterSetup part of the PHP VPN client configuration.
	 */
	public static function handle_client_config(): void {
		self::verify( 'client_config' );
		require_once __DIR__ . '/relay-core.php';
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$config = array(
			'masterSetup'            => array(
				'serverSecret'                   => $settings['secret'],
				'serverUrl'                      => Settings::endpoint_url( $settings ),
				'httpHeaders'                    => self::CLIENT_HTTP_HEADERS,
				'trustAnyTlsCertificate'         => false,
				'vpnCidr'                        => \PhpVpn\VPN_ADDRESS_PREFIX . '0/24',
				'virtualIp'                      => $address,
				'accessKey'                      => $settings['access_keys'][ $address ],
				'useDefaultServerSecretInWizard' => false,
				'autoDetectIpAddressInWizard'    => false,
				'useDefaultAccessKeyInWizard'    => false,
			),
			'communicationContracts' => new \stdClass(),
		);
		nocache_headers();
		header( 'Content-Disposition: attachment; filename="phpvpn-' . str_replace( '.', '-', $address ) . '.json"' );
		wp_send_json( $config, 200, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES );
	}

	/**
	 * Installs the fast route.
	 */
	public static function handle_fast_route_on(): void {
		self::verify( 'fast_route_on' );
		if ( ! self::can_manage_network_files() ) {
			self::done( 'network_only' );
		}
		self::done( true === Fast_Route::install() ? 'fast_route_on' : 'fast_route_failed' );
	}

	/**
	 * Removes the fast route.
	 */
	public static function handle_fast_route_off(): void {
		self::verify( 'fast_route_off' );
		if ( ! self::can_manage_network_files() ) {
			self::done( 'network_only' );
		}
		Fast_Route::uninstall();
		self::done( 'fast_route_off' );
	}

	/**
	 * Loopback requests to the endpoint and to the storage folder. The result is kept for an hour.
	 */
	public static function handle_self_test(): void {
		self::verify( 'self_test' );
		require_once __DIR__ . '/relay-core.php';
		$settings = Settings::get();
		$results  = array();
		$url      = Settings::endpoint_url( $settings );

		// An empty JSON body must reach the relay, which answers 400 bad_request with JSON.
		$response = wp_remote_post(
			add_query_arg( 'action', 'health', $url ),
			array(
				'timeout'   => 10,
				'headers'   => array( 'Content-Type' => 'application/json' ),
				'body'      => '{}',
				// Same rule as the loopback test of Site Health, staging sites often use own certificates.
				'sslverify' => apply_filters( 'https_local_ssl_verify', false ), // phpcs:ignore WordPress.NamingConventions.PrefixAllGlobals.NonPrefixedHooknameFound -- core filter.
			)
		);
		$body = is_wp_error( $response ) ? '' : (string) wp_remote_retrieve_body( $response );
		if ( is_wp_error( $response ) ) {
			/* translators: %s: error message. */
			$results[] = array( 'error', sprintf( __( 'The Server URL cannot be reached from this server: %s. A server with a single PHP worker cannot answer its own request, test the Server URL from a client then.', 'vpn-server' ), $response->get_error_message() ) );
		} elseif ( str_starts_with( $body, '{"type":"error"' ) ) {
			$results[] = array( 'ok', __( 'The Server URL answers protocol requests.', 'vpn-server' ) );
		} else {
			/* translators: %d: HTTP status code. */
			$results[] = array( 'error', sprintf( __( 'The Server URL returned HTTP %d without a protocol answer. Check the permalinks and the web server rewrite rules.', 'vpn-server' ), (int) wp_remote_retrieve_response_code( $response ) ) );
		}

		$results[] = self::probe_storage( $settings );
		set_transient( self::SELF_TEST, $results, HOUR_IN_SECONDS );
		self::done( 'self_test' );
	}

	/**
	 * Writes a random file into the storage folder and requests it over HTTP.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return array{0: string, 1: string} Level and message.
	 */
	private static function probe_storage( array $settings ): array {
		$location = self::storage_url( $settings );
		if ( null === $location ) {
			return array( 'ok', __( 'The storage folder is outside the uploads folder, no web request was made.', 'vpn-server' ) );
		}
		$storage = new \PhpVpn\DiskStorage( $settings['storage'] );
		if ( ! $storage->isUsable() ) {
			return array( 'error', __( 'The storage folder cannot be created.', 'vpn-server' ) );
		}
		require_once ABSPATH . 'wp-admin/includes/file.php';
		if ( 'direct' !== get_filesystem_method() || ! WP_Filesystem() ) {
			return array( 'warning', __( 'The storage protection was not tested, WordPress cannot write files directly.', 'vpn-server' ) );
		}
		global $wp_filesystem;
		$name    = 'vpn-server-probe-' . Settings::random_hex( 8 ) . '.txt';
		$content = Settings::random_hex( 16 );
		$path    = trailingslashit( $settings['storage'] ) . $name;
		if ( ! $wp_filesystem->put_contents( $path, $content, FS_CHMOD_FILE ) ) {
			return array( 'error', __( 'The storage folder is not writable.', 'vpn-server' ) );
		}
		$response = wp_remote_get(
			$location . $name,
			array(
				'timeout'   => 10,
				'sslverify' => apply_filters( 'https_local_ssl_verify', false ), // phpcs:ignore WordPress.NamingConventions.PrefixAllGlobals.NonPrefixedHooknameFound -- core filter.
			)
		);
		$wp_filesystem->delete( $path );
		if ( ! is_wp_error( $response ) && trim( (string) wp_remote_retrieve_body( $response ) ) === $content ) {
			return array( 'error', __( 'The storage folder can be downloaded from the web. Add the web server rule shown under Storage protection, or move the folder outside the web root.', 'vpn-server' ) );
		}
		return array( 'ok', __( 'The storage folder cannot be downloaded from the web.', 'vpn-server' ) );
	}

	// ---------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------

	/**
	 * True for a dedicated storage folder. The folder receives deny-all .htaccess and web.config
	 * files, so it must not be the site root, a WordPress folder, an ancestor of the site, or
	 * inside the code folders. The current managed folder cannot contain the new one, it is deleted.
	 *
	 * @param string               $storage  Normalized absolute path without a trailing slash.
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function is_allowed_storage( string $storage, array $settings ): bool {
		// path_is_absolute() rejects normalized Windows paths such as D:/data, the path here is
		// already normalized: a drive letter, a root slash or a UNC share (//server).
		if ( 1 !== preg_match( '#^([a-zA-Z]:/|/)#', $storage ) || str_contains( $storage, '..' ) ) {
			return false;
		}
		$fold    = static fn( string $path ): string => '\\' === DIRECTORY_SEPARATOR ? strtolower( $path ) : $path;
		$path    = $fold( trailingslashit( $storage ) );
		$uploads = wp_upload_dir( null, false );
		$exact   = array( ABSPATH, WP_CONTENT_DIR, (string) $uploads['basedir'] );
		$inside  = array( WP_PLUGIN_DIR, WPMU_PLUGIN_DIR, get_theme_root(), ABSPATH . 'wp-admin', ABSPATH . WPINC );
		if ( Settings::is_managed_storage( $settings ) ) {
			$inside[] = (string) $settings['storage'];
		}
		foreach ( array_merge( $exact, $inside ) as $folder ) {
			$folder = $fold( trailingslashit( wp_normalize_path( $folder ) ) );
			// The folder itself, or an ancestor of it.
			if ( str_starts_with( $folder, $path ) ) {
				return false;
			}
		}
		foreach ( $inside as $folder ) {
			if ( str_starts_with( $path, $fold( trailingslashit( wp_normalize_path( $folder ) ) ) ) ) {
				return false;
			}
		}
		return true;
	}

	/**
	 * Deletes a storage folder the plugin created, used when the administrator moves the queues.
	 *
	 * @param string $folder Absolute path of the managed folder.
	 */
	private static function delete_folder( string $folder ): void {
		require_once ABSPATH . 'wp-admin/includes/file.php';
		if ( 'direct' === get_filesystem_method() && WP_Filesystem() ) {
			global $wp_filesystem;
			$wp_filesystem->delete( $folder, true );
		}
	}

	/**
	 * True when the current user may change things shared by the whole network on multisite.
	 */
	private static function can_manage_network_files(): bool {
		return ! is_multisite() || is_super_admin();
	}

	/**
	 * Client addresses that are still free, in numeric order.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return string[]
	 */
	private static function free_addresses( array $settings ): array {
		require_once __DIR__ . '/relay-core.php';
		$free = array();
		for ( $host = \PhpVpn\VPN_HOST_MIN; $host <= \PhpVpn\VPN_HOST_MAX; $host++ ) {
			$address = \PhpVpn\VPN_ADDRESS_PREFIX . $host;
			if ( ! isset( $settings['access_keys'][ $address ] ) ) {
				$free[] = $address;
			}
		}
		return $free;
	}

	/**
	 * URL of the storage folder with a trailing slash, null when it is outside the uploads folder.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function storage_url( array $settings ): ?string {
		$uploads = wp_upload_dir( null, false );
		$storage = wp_normalize_path( (string) $settings['storage'] );
		$base    = trailingslashit( wp_normalize_path( (string) $uploads['basedir'] ) );
		if ( '' === $storage || ! empty( $uploads['error'] ) || ! str_starts_with( trailingslashit( $storage ), $base ) ) {
			return null;
		}
		return trailingslashit( (string) $uploads['baseurl'] ) . substr( trailingslashit( $storage ), strlen( $base ) );
	}

	/**
	 * Path part of the storage URL for a web server rule, null when it is outside the uploads folder.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function storage_url_path( array $settings ): ?string {
		$url  = self::storage_url( $settings );
		$path = null === $url ? null : wp_parse_url( $url, PHP_URL_PATH );
		return is_string( $path ) ? $path : null;
	}
}
