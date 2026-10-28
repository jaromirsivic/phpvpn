/**
 * Settings, VPN Server: copy buttons and confirmations of actions that disconnect clients.
 */
( function () {
	'use strict';

	document.addEventListener( 'click', function ( event ) {
		var copy = event.target.closest( '[data-vpn-server-copy]' );
		if ( copy ) {
			var field = document.getElementById( copy.getAttribute( 'data-vpn-server-copy' ) );
			if ( ! field ) {
				return;
			}
			field.select();
			var done = function () {
				var label = copy.textContent;
				copy.textContent = window.vpnServerAdmin.copied;
				window.setTimeout( function () {
					copy.textContent = label;
				}, 1500 );
			};
			if ( navigator.clipboard ) {
				navigator.clipboard.writeText( field.value ).then( done, function () {
					document.execCommand( 'copy' );
					done();
				} );
			} else {
				document.execCommand( 'copy' );
				done();
			}
			return;
		}

		var confirmed = event.target.closest( '[data-vpn-server-confirm]' );
		if ( confirmed && ! window.confirm( confirmed.getAttribute( 'data-vpn-server-confirm' ) ) ) {
			event.preventDefault();
		}
	} );
} )();
