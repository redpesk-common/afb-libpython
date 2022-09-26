Name:           afb-libpython
Version:        1.0.0
Release:        0%{?dist}
License:        LGPLv3
Summary:        Abstraction of afb-libafb for integration with non C/C++
Group:          Development/Libraries/C and C++
Url:            https://git.ovh.iot/redpesk/redpesk-common/afb-libpython
Source:         %{name}-%{version}.tar.gz

BuildRequires:  make
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  afb-cmake-modules
BuildRequires:  pkgconfig(libafb) >= 4.0
BuildRequires:  pkgconfig(librp-utils)

BuildRequires:       pkgconfig(libafb-glue)
BuildRequires:       pkgconfig(python3)

%global debug_package %{nil}

%description
Exposes afb-libafb to the Python scripting language.

%prep
%setup -q -n %{name}-%{version}

%build
%if 0%{?almalinux_ver} >= 8
mkdir -p build
cd build
%cmake ../ -DPython3_SITEARCH=%{python3_sitearch}
%__make %{?_smp_mflags}
%else
%cmake .
%if 0%{?fedora} >= 33
%cmake_build
%else
%__make %{?_smp_mflags}
%endif
%endif

%install
%if 0%{?fedora} >= 33
[ -d redhat-linux-build ] && cd redhat-linux-build
%make_install
%else
[ -d build ] && cd build
%make_install
%endif

#This should be remove
rm %{?buildroot}%{_prefix}/afb-libpython/lib/*.so

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
%{python3_sitearch}/*.so
