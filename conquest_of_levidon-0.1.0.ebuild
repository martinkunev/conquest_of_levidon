# Copyright 1999-2016 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v3
# $Id$

# games-strategy/conquest_of_levidon

EAPI="4"

#inherit eutils

DESCRIPTION="Turn-based strategic medieval game"
HOMEPAGE="https://github.com/martinkunev/conquest_of_levidon"
SRC_URI="https://github.com/martinkunev/${PN}/${P}.tar.gz"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="amd64 x86"
IUSE="debug"

RDEPEND="media-libs/libpng
	media-libs/mesa
	x11-libs/libX11
	x11-libs/libxcb
	x11-libs/libXext
	media-fonts/dejavu
	media-libs/freetype"
DEPEND="${RDEPEND}
	virtual/pkgconfig
	dev-lang/perl"

src_configure() {
	econf
		--prefix=/usr \
		$(usex debug --debug)
}

src_compile() {
	emake conquest_of_levidon
}

src_test() {
	emake check
}

src_install() {
	emake install
}
