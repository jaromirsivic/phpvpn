# phpvpn RPM packaging (plan-v001 section 4.13). Built by
# client/tools/linux/package-rpm.sh from a tarball of the client/ tree, whose
# top-level directory is phpvpn-%%{version}. Version must equal the CMake
# project version of client/CMakeLists.txt; package-rpm.sh checks that.
#
# The release can be raised for rebuilds without a source change:
#   rpmbuild --define 'phpvpn_release 2' ...
#
# The graphical tray is the separate subpackage phpvpn-tray (WP11). It is
# off until that work package enables it; see the marked sections below.
%bcond tray 0

%global phpvpn_rel %{?phpvpn_release}%{!?phpvpn_release:1}

Name:           phpvpn
Version:        1.0.0
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
# WP11: the tray build dependencies go here, for example
# BuildRequires:  pkgconfig(gtk+-3.0)
# BuildRequires:  pkgconfig(ayatana-appindicator3-0.1)
# BuildRequires:  desktop-file-utils
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
# WP11: phpvpn-tray subpackage. Keep the exact version dependency so the tray
# always talks to the service of the same release.
%package tray
Summary:        Tray application of the VPN client
Requires:       %{name}%{?_isa} = %{version}-%{release}
Recommends:     gnome-shell-extension-appindicator

%description tray
The phpvpn tray shows the connection state in the desktop notification area
and controls the phpvpn service.
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
# WP11: the tray binary, phpvpn-tray.desktop, the autostart entry, the hicolor
# icons, phpvpn-tray(1) and the Material Symbols licence and notice.
%endif

%changelog
* Sat Sep 26 2026 phpvpn maintainers <maintainers@phpvpn.invalid> - 1.0.0-1
- First Linux package: headless service, command line client, systemd unit,
  NetworkManager snippet and manual pages.
