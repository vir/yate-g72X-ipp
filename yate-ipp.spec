# to add a distro release tag run rpmbuild --define 'dist value'
# to suppress auto dependencies run rpmbuild --define 'nodeps 1'

%{!?dist:%define dist %{nil}}
%{?nodeps:%define no_auto_deps 1}

Summary:	G.72x codecs for Yet Another Telephony Engine based on Intel IPP
Name:     	yate-ipp
Version: 	0.0.1
Release:	1%{dist}
License:	GPL
Packager:	Paul Chitescu <paulc@voip.null.ro>
Source:		http://yate.null.ro/%{name}-%{version}-1.tar.gz
Group:		Applications/Communications
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root
URL:		http://yate.null.ro/
BuildRequires:	gcc-c++
BuildRequires:	yate-devel = 3.0.0
Requires:	yate = 3.0.0

%define prefix  /usr


%description
Yate is a telephony engine designed to implement PBX and IVR solutions
for small to large scale projects.
This module provides G.723 and G.729 codecs for Yate using Intel(R) IPP.

%files
%defattr(-, root, root)
%dir /usr/share/doc/%{name}-%{version}
%doc /usr/share/doc/%{name}-%{version}/README
/usr/local/lib/yate/g723codec.yate
/usr/local/lib/yate/g729codec.yate

%prep
%setup -q -n %{name}

%if "%{no_auto_deps}" == "1"
%define local_find_requires %{_builddir}/%{name}/local-find-requires
%define local_find_provides %{_builddir}/%{name}/local-find-provides
#
%{__cat} <<EOF >%{local_find_requires}
#! /bin/sh
grep -v '\.yate$' | %{__find_requires} | grep -v '^perl'
exit 0
EOF
#
%{__cat} <<EOF >%{local_find_provides}
#! /bin/sh
%{__find_provides} | grep -v '\.yate$'
exit 0
EOF
#
chmod +x %{local_find_requires} %{local_find_provides}
%define _use_internal_dependency_generator 0
%define __find_requires %{local_find_requires}
%define __find_provides %{local_find_provides}
%define __perl_requires /bin/true
%endif

%build
./configure --prefix=%{prefix} --sysconfdir=/etc --mandir=%{prefix}/share/man
make strip

%install
make install DESTDIR=%{buildroot}

%clean
# make clean
rm -rf %{buildroot}

%changelog
* Wed Oct 13 2010 Paul Chitescu <paulc@voip.null.ro>
- Created specfile
