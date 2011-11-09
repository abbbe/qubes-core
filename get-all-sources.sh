#!/bin/sh

: ${COMPONENTS="yum xen kernel core gui qubes-manager installer template-builder kde-dom0 antievilmaid"}

SCRIPT_DIR=$PWD
SRC_ROOT=$PWD/qubes-src

set -e

mkdir -p $SRC_ROOT
cd $SRC_ROOT

for COMPONENT in $COMPONENTS; do
    . $SCRIPT_DIR/get-sources.sh
done