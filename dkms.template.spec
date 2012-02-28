%{?!module_name: %{error: You did not specify a module name (%%module_name)}}
%{?!module_srcdir: %{error: You did not specify a module name (%%module_srcdir)}}
%{?!version: %{error: You did not specify a module version (%%version)}}
%{?!kernel_version: %define kernel_version: $(uname -r)}
%{?!packager: %define packager DKMS <dkms-devel@lists.us.dell.com>}
%{?!license: %define license Unknown}
%{?!_datarootdir: %define _datarootdir %{_datadir}}
%{?!_srcdir: %define _srcdir %_prefix/src}
%{?!module_srcdir: %{error: You did not specify a module name (%%module_srcdir)}}

Summary:	%{module_name} %{version} package
Name:		%{module_name}
Version:	%{version}
License:	%license
Release:	1%{dist}
Packager:	%{packager}
BuildArch:	noarch
Group:		System/Kernel
Requires: 	dkms >= 1.95
BuildRequires: 	dkms
BuildRoot: 	%{_tmppath}/%{name}-%{version}-%{release}-root/

%description
Kernel modules for %{module_name} %{version} in a DKMS wrapper.

%prep

%install
if [ "$RPM_BUILD_ROOT" != "/" ]; then
        rm -rf $RPM_BUILD_ROOT
fi
mkdir -p $RPM_BUILD_ROOT/%{_srcdir}/%{module_name}-%{version}

pushd %{module_srcdir}
cp -af * $RPM_BUILD_ROOT/%{_srcdir}/%{module_name}-%{version}/ 
popd

%clean
if [ "$RPM_BUILD_ROOT" != "/" ]; then
        rm -rf $RPM_BUILD_ROOT
fi

%post
for POSTINST in %_prefix/lib/dkms/common.postinst %{_datarootdir}/%{module_name}/postinst; do
        if [ -f $POSTINST ]; then
                $POSTINST %{module_name} %{version} %{_datarootdir}/%{module_name}
                exit $?
        fi
        echo "WARNING: $POSTINST does not exist."
done
echo -e "ERROR: DKMS version is too old and %{module_name} was not"
echo -e "built with legacy DKMS support."
echo -e "You must either rebuild %{module_name} with legacy postinst"
echo -e "support or upgrade DKMS to a more current version."
exit 1

%preun
echo -e
echo -e "Uninstall of %{module_name} module (version %{version}) beginning:"
dkms remove -m %{module_name} -v %{version} --all --rpm_safe_upgrade
exit 0

%files
%defattr(-,root,root)
%{_srcdir}/%{module_name}-%{version}

%changelog
* %(date "+%a %b %d %Y") %packager %{version}-%{release}
- Automatic build by DKMS

