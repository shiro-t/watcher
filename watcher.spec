Name: watcher
Summary: Process watch and restarter
Version: 2.05
Release: 1
Copyright: GPL
Group: Networking/Daemons
Source: watcher-%{version}.tar.gz
BuildRoot: /tmp/wacher-root
Packager: SHIROYAMA Takayuki <siroyama@archsystem.com>

%description
Process watch and restarter.

%prep
%setup

%build
make

%install
rm -fr $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/local/bin
cp -pr ./watcher $RPM_BUILD_ROOT/usr/local/bin/

%clean
rm -rf $RPM_BUILD_ROOT

%pre

%post

%preun

%postun

%files
%doc COPYING.GPL
/usr/local/bin/watcher
