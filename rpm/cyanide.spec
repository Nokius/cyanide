# 
# Do NOT Edit the Auto-generated Part!
# Generated by: spectacle version 0.27
# 

Name:       cyanide

# >> macros
# << macros

%{!?qtc_qmake:%define qtc_qmake %qmake}
%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}
Summary:    Tox client for SailfishOS
Version:    0.2.4
Release:    1
Group:      Qt/Qt
License:    GPLv3
URL:        https://github.com/krobelus/cyanide
Source0:    %{name}-%{version}.tar.bz2
Source100:  cyanide.yaml
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-qtdeclarative-import-folderlistmodel
Requires:   toxcore = 15.0610
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(openal)
BuildRequires:  desktop-file-utils

%description
Tox client for SailfishOS


%prep
%setup -q -n %{name}-%{version}

# >> setup
# << setup

%build
# >> build pre
# << build pre

%qtc_qmake5 

%qtc_make %{?_smp_mflags}

# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
# << install pre
%qmake5_install

# >> install post
# TODO use variables for sanity
install -Dm644 /home/mersdk/share/git/cyanide/filesystem/usr/share/lipstick/notificationcategories/harbour.cyanide.call.conf \
%{buildroot}%{_datadir}/lipstick/notificationcategories/harbour.cyanide.call.conf
install -Dm644 /home/mersdk/share/git/cyanide/filesystem/usr/share/lipstick/notificationcategories/harbour.cyanide.message.conf \
%{buildroot}%{_datadir}/lipstick/notificationcategories/harbour.cyanide.message.conf

install -Dm644 /home/mersdk/share/git/cyanide/filesystem/usr/share/dbus-1/services/harbour.cyanide.service \
%{buildroot}%{_datadir}/dbus-1/services/harbour.cyanide.service
# << install post

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications             \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
# >> files
%{_datadir}/dbus-1/services/harbour.cyanide.service
%{_datadir}/lipstick/notificationcategories/harbour.cyanide.call.conf
%{_datadir}/lipstick/notificationcategories/harbour.cyanide.message.conf
# << files
