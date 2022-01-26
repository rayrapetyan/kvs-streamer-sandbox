FROM node:16-bullseye

ENV DEBIAN_FRONTEND noninteractive

RUN set -eux; apt-get update; \
    apt-get install --no-install-recommends -y \
        git; \
    apt-get clean -y; \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

WORKDIR /src

RUN git clone https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js.git

WORKDIR /src/amazon-kinesis-video-streams-webrtc-sdk-js

RUN npm install

ADD examples ./examples
ADD webpack.dev.config.js .

EXPOSE 3001

CMD ["npm", "run", "develop"]
