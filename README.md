## Server

    cd server
    update AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY and AWS_DEFAULT_REGION in run_exec.sh
    ./build.sh
    ./run.sh

This will run a docker container with kvs-streamer server

## Client

    cd webclient
    ./run.sh

This will run a docker container with kvs-streamer webclient

## Host

Navigate to: http://localhost:3001

Note: use localhost! 0.0.0.0 will cause JS digest issues
