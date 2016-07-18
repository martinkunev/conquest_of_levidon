# Copyright 1999-2015 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v3
# $Id$

# games-strategy/conquest_of_levidon

EAPI="4"

inherit eutils
if [[ ${PV} == "9999" ]] ; then
	ESVN_REPO_URI="svn://svn.savannah.gnu.org/nano/trunk/nano"
	inherit subversion autotools
else
	MY_P=${PN}-${PV/_}
	SRC_URI="http://www.nano-editor.org/dist/v${PV:0:3}/${MY_P}.tar.gz"
fi

DESCRIPTION="Turn-based medieval strategic game"
HOMEPAGE="http://www.nano-editor.org/ https://www.gentoo.org/doc/en/nano-basics-guide.xml"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64 ~x86 ~amd64-linux ~x86-linux"
IUSE="debug +magic nls"

RDEPEND="media-libs/libpng
	media-libs/mesa
	x11-libs/libX11
	x11-libs/libxcb
	x11-libs/libXext
	media-fonts/dejavu"
DEPEND="${RDEPEND}
	dev-lang/perl"

src_configure() {
	eval export ac_cv_{header_magic_h,lib_magic_magic_open}=$(usex magic)
	local myconf=()
	case ${CHOST} in
	*-gnu*|*-uclibc*) myconf+=( "--with-wordbounds" ) ;; #467848
	esac
	econf \
		--bindir="${EPREFIX}"/bin \
		--htmldir=/trash \
		$(use_enable !minimal color) \
		$(use_enable !minimal multibuffer) \
		$(use_enable !minimal nanorc) \
		--disable-wrapping-as-root \
		$(use_enable spell speller) \
		$(use_enable justify) \
		$(use_enable debug) \
		$(use_enable nls) \
		$(use_enable unicode utf8) \
		$(use_enable minimal tiny) \
		$(usex ncurses --without-slang $(use_with slang)) \
		"${myconf[@]}"
}

src_install() {
	default
	rm -rf "${D}"/trash

	dodoc doc/nanorc.sample
	dohtml doc/faq.html
	insinto /etc
	newins doc/nanorc.sample nanorc
	if ! use minimal ; then
		# Enable colorization by default.
		sed -i \
			-e '/^# include /s:# *::' \
			"${ED}"/etc/nanorc || die
	fi

	dodir /usr/bin
	dosym /bin/nano /usr/bin/nano
}
