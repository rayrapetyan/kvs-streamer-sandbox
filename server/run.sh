#!/usr/bin/env bash

docker stop gstreamer-aws
docker rm gstreamer-aws

docker run \
    -it \
    --privileged \
    --device=/dev/dri/card0:/dev/dri/card0 \
    --device=/dev/dri/renderD128:/dev/dri/renderD128 \
    --device=/dev/snd/seq:/dev/snd/seq \
    --group-add=render \
    --hostname="gstreamer-aws" \
    --shm-size="2g" \
    --name="gstreamer-aws" \
    test/gstreamer-aws:latest
