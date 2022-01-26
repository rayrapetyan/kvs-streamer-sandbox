#!/usr/bin/env bash

set -eux

DOCKER_BUILD_TAG=kvs/server
docker build -t $DOCKER_BUILD_TAG -f Dockerfile .

set +e

DOCKER_RUN_TAG=kvs-server
docker stop $DOCKER_RUN_TAG

docker run \
    -it \
    --rm \
    --name=$DOCKER_RUN_TAG \
    --hostname=$DOCKER_RUN_TAG \
    --device=/dev/dri/card0:/dev/dri/card0 \
    --device=/dev/dri/renderD128:/dev/dri/renderD128 \
    --device=/dev/snd/seq:/dev/snd/seq \
    --shm-size="2g" \
    $DOCKER_BUILD_TAG
