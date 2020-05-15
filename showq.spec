Name:	    slurm-showq
Version:	0.0.1
Release:	1%{?dist}
Summary:	A Slurm-compatible implementation of Maui's showq command.

License:	GPLv3+
URL:		https://github.com/Elemnir/slurm_showq
Source0:	https://github.com/Elemnir/slurm_showq

BuildRequires:	gcc-c++ slurm-devel
Requires:   libstdc++ slurm

%description
A Slurm-compatible implementation of Maui's showq command.

%prep
%setup -q


%build
make %{?_smp_mflags}


%install
mkdir -p %{buildroot}/%{_bindir}

install -m 0755 showq %{buildroot}/%{_bindir}/showq


%files
%{_bindir}/showq


%changelog
* Fri May 15 2020 Adam Howard <ahoward@utk.edu> - 0.0.1-1
- Initial release
