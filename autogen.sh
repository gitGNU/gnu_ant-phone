#!/bin/sh

#gettextize --force --copy --intl --no-changelog
autopoint --force
aclocal
autoheader
automake --copy --add-missing --force-missing --gnu
autoconf
