Name:	    slurm-showq
Version:	0.0.2
Release:	1%{?dist}
Summary:	A Slurm-compatible implementation of Maui's showq command.

License:	GPLv3+
URL:		https://github.com/Elemnir/slurm_showq

%global slurm_showq_source_dir slurm_showq-%{version}
Source:     %{slurm_showq_source_dir}.tar.gz

BuildRequires:	gcc-c++ slurm-devel
Requires:   libstdc++ slurm

%description
A Slurm-compatible implementation of Maui's showq command.

%prep
%setup -n %{slurm_showq_source_dir}

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
