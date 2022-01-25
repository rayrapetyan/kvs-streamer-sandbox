#!/usr/bin/env bash

set -e

DOCKER_TAG=test/gstreamer-aws

docker build "$@" -t $DOCKER_TAG .
#docker tag $DOCKER_TAG:latest 752538483784.dkr.ecr.us-east-1.amazonaws.com/$DOCKER_TAG:latest
#docker push 752538483784.dkr.ecr.us-east-1.amazonaws.com/$DOCKER_TAG:latest
