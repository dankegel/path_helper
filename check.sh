#!/bin/sh
# Verify that path_helper outputs entries in the proper order.
set -e

if ! test -w /etc/paths.d
then
    # FIXME: use fakeroot, or modify path_helper.c to take a path option
    echo "Please do chmod a+w /etc/paths.d before running"
    exit 1
fi

# Construct a bunch of path entries with identical names but
# a numerically incrementing prefix.  Ten seems to be enough to
# tickle the bug.
rm -f /etc/paths.d/*-test-bin
for a in `seq 10 20`
do
    echo /tmp/bins/$a > /etc/paths.d/$a-test-bin
done

# Verify that path_helper outputs those entries in the proper order.
# (Avoid depending whether output is sorted with or without respect to case,
# since the foo.d idiom does not depend on it, and it's hard to satisfy
# in the face of varying degrees of case sensitivity and locales.)

path_helper -s | grep PATH | tr : '\012' | grep /tmp/bins > out.log
rm -f /etc/paths.d/*-test-bin
sort < out.log > expected.log
if diff -u expected.log out.log
then
    echo "PASS: output in proper order"
    exit 0
else
    echo "FAIL: output in wrong order"
    exit 1
fi
