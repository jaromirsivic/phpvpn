<?php
/**
 * Development-only front controller for a disposable WordPress test installation.
 * php -S 127.0.0.1:5098 -t /absolute/test-wordpress /absolute/repo/WordPress/tests/router.php
 * This file is not distributed inside the plugin ZIP.
 */
if ( 'cli-server' !== PHP_SAPI ) {
	http_response_code( 403 );
	exit;
}
$root = realpath( $_SERVER['DOCUMENT_ROOT'] );
if ( false === $root || ! is_file( $root . '/index.php' ) ) {
	throw new RuntimeException( 'The document root must be a disposable WordPress installation.' );
}
$path = parse_url( $_SERVER['REQUEST_URI'], PHP_URL_PATH );
$file = is_string( $path ) ? realpath( $root . '/' . ltrim( rawurldecode( $path ), '/' ) ) : false;
if ( false !== $file && str_starts_with( $file, $root . DIRECTORY_SEPARATOR )
	&& ( is_file( $file ) || ( is_dir( $file ) && is_file( $file . '/index.php' ) ) ) ) {
	return false;
}
// PHP's built-in server can put an unknown dotted URI into SCRIPT_NAME.
// WordPress 6.6 then strips the whole request path and treats it as the home page.
// Emulate the ordinary index.php rewrite target without changing REQUEST_URI.
$_SERVER['SCRIPT_NAME'] = '/index.php';
$_SERVER['PHP_SELF'] = '/index.php';
$_SERVER['SCRIPT_FILENAME'] = $root . '/index.php';
unset( $_SERVER['PATH_INFO'] );
require $root . '/index.php';
