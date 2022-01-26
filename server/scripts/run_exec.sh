#!/usr/bin/env bash

export AWS_ACCESS_KEY_ID=
export AWS_SECRET_ACCESS_KEY=
export AWS_DEFAULT_REGION=us-west-2
export AWS_KVS_LOG_LEVEL=4
export AWS_KVS_CACERT_PATH=/usr/lib/ssl/certs/awskvs-cert.pem
export DEBUG_LOG_SDP=TRUE
export GST_DEBUG=2

/usr/local/bin/kvs-streamer
