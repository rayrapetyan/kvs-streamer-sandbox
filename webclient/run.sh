#!/usr/bin/env bash

set -eux

DOCKER_BUILD_TAG=kvs-streamer/webclient
docker build -t $DOCKER_BUILD_TAG -f Dockerfile .

set +e

DOCKER_RUN_TAG=kvs-streamer-webclient
docker stop $DOCKER_RUN_TAG
docker run \
    -it \
    --rm \
    --name=$DOCKER_RUN_TAG \
    --hostname=$DOCKER_RUN_TAG \
    --publish="3001:3001/tcp" \
    $DOCKER_BUILD_TAG
