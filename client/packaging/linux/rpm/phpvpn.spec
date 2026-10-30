# phpvpn RPM packaging (plan-v001 section 4.13). Built by
# client/tools/linux/package-rpm.sh from a tarball of the client/ tree, whose
# top-level directory is phpvpn-%%{version}. Version must equal the CMake
# project version of client/CMakeLists.txt; package-rpm.sh checks that.
#
# The release can be raised for rebuilds without a source change:
#   rpmbuild --define 'phpvpn_release 2' ...
#
# The graphical tray is the separate subpackage phpvpn-tray (WP11). The base
# package stays free of graphical dependencies; rpmbuild --without tray builds
# only the headless package.
%bcond tray 1

%global phpvpn_rel %{?phpvpn_release}%{!?phpvpn_release:1}

Name:           phpvpn
Version:        1.0.2
Release:        %{phpvpn_rel}
Summary:        VPN client service and command line client
License:        MIT
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.28
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(openssl) >= 3
BuildRequires:  systemd-rpm-macros
%if %{with tray}
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(ayatana-appindicator3-0.1)
BuildRequires:  desktop-file-utils
%endif

# Runtime libraries (libcrypto, libcurl, libsystemd, libstdc++) come from the
# automatic soname dependencies. The package has no graphical dependency.

%description
phpvpn connects this computer to a phpvpn relay and exchanges IPv4 packets with
the other clients of the same virtual network through a TUN interface.

The package contains the system service phpvpn-service, started by systemd as
phpvpn.service, and the command line client phpvpn, which controls the service
over a local socket and needs neither root nor a graphical session. A
NetworkManager snippet keeps NetworkManager away from the phpvpn interface.

%if %{with tray}
# The exact version dependency keeps the tray talking to the service of the
# same release. GTK 3 and libayatana-appindicator3 come from the automatic
# soname dependencies. hicolor-icon-theme owns the icon directories; its file
# triggers refresh the icon cache, so no scriptlet is needed.
%package tray
Summary:        Tray application of the VPN client
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       hicolor-icon-theme
# Vanilla GNOME shows tray icons only with this extension. It is recommended,
# not enabled: desktop settings belong to the user.
Recommends:     gnome-shell-extension-appindicator

%description tray
The phpvpn tray shows the connection state of the phpvpn service in the
desktop notification area and offers Connect, Disconnect, the configuration
Wizard, the configuration editor, communication contracts, server information
and statistics. It starts at every graphical login and never owns the VPN
connection: closing it or logging out leaves the connection as it is. Without
a notification area host (GNOME without the AppIndicator extension) it shows a
control window instead.
%endif

%prep
%autosetup

%build
# Tests are built for %%check; the privileged network namespace tests and the
# end-to-end helpers need root and are not part of the package build. Warnings
# stay warnings with the distribution compiler flags.
%cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_LIBEXECDIR=libexec \
    -DPHPVPN_SYSTEMD_UNIT_DIR=%{_unitdir} \
    -DPHPVPN_NM_CONF_DIR=%{_prefix}/lib/NetworkManager/conf.d \
    -DPHPVPN_BUILD_TRAY=%{?with_tray:ON}%{!?with_tray:OFF} \
    -DPHPVPN_WARNINGS_AS_ERRORS=OFF \
    -DBUILD_TESTING=ON \
    -DPHPVPN_BUILD_PRIVILEGED_TESTS=OFF \
    -DPHPVPN_BUILD_LINUX_E2E=OFF
%cmake_build

%install
%cmake_install

%check
# Unprivileged unit tests only (portable core, logger, Linux platform layer).
%ctest
%if %{with tray}
desktop-file-validate %{buildroot}%{_datadir}/applications/phpvpn-tray.desktop
desktop-file-validate %{buildroot}%{_sysconfdir}/xdg/autostart/phpvpn-tray.desktop
%endif

%post
%systemd_post phpvpn.service

%preun
%systemd_preun phpvpn.service

%postun
%systemd_postun_with_restart phpvpn.service

%files
%license packaging/linux/LICENSE
%doc packaging/linux/dist/README.md
%{_bindir}/phpvpn
%dir %{_libexecdir}/phpvpn
%{_libexecdir}/phpvpn/phpvpn-service
%{_unitdir}/phpvpn.service
# The directories are shared with NetworkManager, which is not required: a
# headless system may manage its network without it.
%dir %{_prefix}/lib/NetworkManager
%dir %{_prefix}/lib/NetworkManager/conf.d
%{_prefix}/lib/NetworkManager/conf.d/90-phpvpn.conf
%{_mandir}/man1/phpvpn.1*
%{_mandir}/man8/phpvpn-service.8*
# /etc/phpvpn (0700), /var/lib/phpvpn (0700) and /run/phpvpn are created by
# systemd from ConfigurationDirectory=, StateDirectory= and RuntimeDirectory=
# of phpvpn.service. The service writes config.json itself; the package never
# ships or owns it, so an upgrade or removal keeps the configuration.
# uninstall.sh --remove-data deletes both directories explicitly.

%if %{with tray}
%files tray
# MIT like the main package (its licence file comes with phpvpn, which the tray
# requires at the exact release); the icons are the project's own PHPVPN icons.
%{_bindir}/phpvpn-tray
%{_datadir}/applications/phpvpn-tray.desktop
# XDG autostart for every user (the Windows HKLM Run value). A user disables it
# with Hidden=true in ~/.config/autostart; an administrator's edit is kept.
%config(noreplace) %{_sysconfdir}/xdg/autostart/phpvpn-tray.desktop
%{_datadir}/icons/hicolor/*/apps/phpvpn.png
%{_datadir}/icons/hicolor/*/apps/phpvpn-connected.png
%{_datadir}/icons/hicolor/*/apps/phpvpn-connecting.png
%{_datadir}/icons/hicolor/*/apps/phpvpn-disconnected.png
%{_datadir}/icons/hicolor/scalable/apps/phpvpn.svg
%{_datadir}/icons/hicolor/scalable/apps/phpvpn-connected.svg
%{_datadir}/icons/hicolor/scalable/apps/phpvpn-connecting.svg
%{_datadir}/icons/hicolor/scalable/apps/phpvpn-disconnected.svg
%{_mandir}/man1/phpvpn-tray.1*
%endif

%changelog
* Fri Oct 30 2026 phpvpn maintainers <maintainers@phpvpn.invalid> - 1.0.2-1
- Release 1.0.2.

* Tue Oct 27 2026 phpvpn maintainers <maintainers@phpvpn.invalid> - 1.0.1-1
- Release 1.0.1.

* Sat Sep 26 2026 phpvpn maintainers <maintainers@phpvpn.invalid> - 1.0.0-1
- First Linux package: headless service, command line client, systemd unit,
  NetworkManager snippet and manual pages; the tray application in the
  phpvpn-tray subpackage.
