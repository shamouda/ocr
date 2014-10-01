#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage <sub-directory> <arch>"
    exit 1
fi

echo "Building kernel '$1' for architecture '$2'"
RUN_JENKINS=build make -f ${WORKLOAD_SRC}/Makefile.$2 all install
RETURN_CODE=$?

if [ $RETURN_CODE -eq 0 ]; then
    if [ "x$2" == "xtg" ]; then
        cp ${WORKLOAD_SRC}/Makefile.$2 ${WORKLOAD_INSTALL_ROOT}/$2/
    fi
    echo "**** Build SUCCESS ****"
else
    echo "**** Build FAILURE ****"
fi

exit $RETURN_CODE
