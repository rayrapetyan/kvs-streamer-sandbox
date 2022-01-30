FROM node:16-bullseye

ENV DEBIAN_FRONTEND noninteractive

RUN set -eux; apt-get update; \
    apt-get install --no-install-recommends -y \
        git; \
    apt-get clean -y; \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

WORKDIR /src

ADD . .

WORKDIR /src

RUN npm install

EXPOSE 3001

CMD ["node", "src/index.js"]
