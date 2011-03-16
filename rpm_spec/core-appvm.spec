#
# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2010  Joanna Rutkowska <joanna@invisiblethingslab.com>
# Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#

%{!?version: %define version %(cat version_vm)}

Name:		qubes-core-appvm
Version:	%{version}
Release:	1
Summary:	The Qubes core files for AppVM

Group:		Qubes
Vendor:		Invisible Things Lab
License:	GPL
URL:		http://www.qubes-os.org
Requires:	/usr/bin/xenstore-read
Requires:   fedora-release
Requires:	/usr/bin/mimeopen
Requires:	qubes-core-commonvm
BuildRequires:  gcc
BuildRequires:  xen-devel
Provides:   qubes-core-vm

%define _builddir %(pwd)/appvm

%define kde_service_dir /usr/share/kde4/services/ServiceMenus 

%description
The Qubes core files for installation inside a Qubes AppVM.

%pre

if [ "$1" !=  1 ] ; then
# do this whole %pre thing only when updating for the first time...
exit 0
fi

adduser --create-home user

mkdir -p $RPM_BUILD_ROOT/var/lib/qubes

%build
make clean all
make -C ../common

%install

mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp qubes_core_appvm $RPM_BUILD_ROOT/etc/init.d/
mkdir -p $RPM_BUILD_ROOT/var/lib/qubes
mkdir -p $RPM_BUILD_ROOT/usr/bin
cp qubes_timestamp qvm-copy-to-vm qvm-open-in-dvm $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT/usr/lib/qubes
cp qubes_add_pendrive_script qubes_penctl qvm-copy-to-vm.kde $RPM_BUILD_ROOT/usr/lib/qubes
ln -s /usr/bin/qvm-open-in-dvm $RPM_BUILD_ROOT/usr/lib/qubes/qvm-dvm-transfer 
cp ../common/meminfo-writer $RPM_BUILD_ROOT/usr/lib/qubes
mkdir -p $RPM_BUILD_ROOT/%{kde_service_dir}
cp qvm-copy.desktop qvm-dvm.desktop $RPM_BUILD_ROOT/%{kde_service_dir}
mkdir -p $RPM_BUILD_ROOT/etc/udev/rules.d
cp qubes.rules $RPM_BUILD_ROOT/etc/udev/rules.d
mkdir -p $RPM_BUILD_ROOT/mnt/incoming
mkdir -p $RPM_BUILD_ROOT/mnt/outgoing
mkdir -p $RPM_BUILD_ROOT/mnt/removable

mkdir -p $RPM_BUILD_ROOT/etc/X11
cp xorg-preload-apps.conf $RPM_BUILD_ROOT/etc/X11

mkdir -p $RPM_BUILD_ROOT/home_volatile/user
chown 500:500 $RPM_BUILD_ROOT/home_volatile/user

%post

chkconfig --add qubes_core_appvm || echo "WARNING: Cannot add service qubes_core!"
chkconfig qubes_core_appvm on || echo "WARNING: Cannot enable service qubes_core!"

if [ "$1" !=  1 ] ; then
# do this whole %post thing only when updating for the first time...
exit 0
fi

usermod -L user

%preun
if [ "$1" = 0 ] ; then
    # no more packages left
    chkconfig qubes_core_appvm off
fi

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/etc/init.d/qubes_core_appvm
/usr/bin/qvm-copy-to-vm
/usr/lib/qubes/qvm-copy-to-vm.kde
%attr(4755,root,root) /usr/bin/qvm-open-in-dvm
/usr/lib/qubes/qvm-dvm-transfer
/usr/lib/qubes/meminfo-writer
%{kde_service_dir}/qvm-copy.desktop
%{kde_service_dir}/qvm-dvm.desktop
%attr(4755,root,root) /usr/lib/qubes/qubes_penctl
/usr/lib/qubes/qubes_add_pendrive_script
/etc/udev/rules.d/qubes.rules
%dir /mnt/incoming
%dir /mnt/outgoing
%dir /mnt/removable
/usr/bin/qubes_timestamp
%dir /home_volatile
%attr(700,user,user) /home_volatile/user
/etc/X11/xorg-preload-apps.conf
