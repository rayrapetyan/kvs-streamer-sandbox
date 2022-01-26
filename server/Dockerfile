ARG OS_TAG=bullseye

FROM debian:$OS_TAG-slim as builder

ENV DEBIAN_FRONTEND noninteractive

RUN set -eux; apt-get update; \
    apt-get install --no-install-recommends -y \
      x11-apps \
      wget ca-certificates pulseaudio libcairo2 libxcb1 libxrandr2 libxv1 libopus0 libvpx6 \
      libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
      gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-pulseaudio gstreamer1.0-x \
      git pkg-config cmake libcap2 libcap-dev build-essential \
      libssl-dev libcurl4-openssl-dev liblog4cplus-dev libgstreamer1.0-dev \
      libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base-apps gstreamer1.0-plugins-bad gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-tools \
      libxrandr-dev libxtst-dev libboost-dev  gstreamer1.0-alsa; \
   apt-get clean -y; \
   rm -rf /var/lib/apt/lists/* /var/cache/apt/*

WORKDIR /src

RUN git clone --recursive https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c.git; \
    mkdir amazon-kinesis-video-streams-webrtc-sdk-c/build && cd amazon-kinesis-video-streams-webrtc-sdk-c/build; \
    cmake .. && make -j8 && make DESTDIR=/packages install

RUN git clone https://github.com/jtanx/libclipboard; cd libclipboard && cmake . && make -j8 && make DESTDIR=/packages install

RUN cp -r /packages/* /
RUN cp -r /src/amazon-kinesis-video-streams-webrtc-sdk-c/open-source/* /usr/local

ADD kvs-streamer ./kvs-streamer
RUN mkdir kvs-streamer/build && cd kvs-streamer/build; cmake .. && make -j8

FROM debian:$OS_TAG-slim as runner

ENV DEBIAN_FRONTEND noninteractive

ARG USERNAME=gamer
ARG PASSWORD=gamer
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN set -eux; apt-get update; \
    apt-get install --no-install-recommends -y \
        ca-certificates openssl \
        xserver-xorg-video-dummy x11-xserver-utils \
        pulseaudio \
        gstreamer1.0-pulseaudio gstreamer1.0-plugins-ugly gstreamer1.0-tools gstreamer1.0-x \
        gstreamer1.0-vaapi i965-va-driver libva2 libva-drm2 libva-x11-2 vainfo intel-media-va-driver \
        supervisor \
        openbox xfce4-terminal firefox-esr procps; \
    apt-get clean -y; \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

# create non-root user TODO: drop sudo
RUN groupadd --gid $USER_GID $USERNAME; \
    useradd --uid $USER_UID --gid $USER_GID --shell /bin/bash --password $(openssl passwd $PASSWORD) --create-home --home-dir /home/$USERNAME $USERNAME; \
    usermod -aG sudo,audio,video,pulse,render,kvm $USERNAME; \
    mkdir -p /etc/$USERNAME /var/log/$USERNAME; \
    chmod 1777 /var/log/$USERNAME; \
    chown $USERNAME /var/log/$USERNAME/

COPY --from=builder /packages /
COPY --from=builder /src/amazon-kinesis-video-streams-webrtc-sdk-c/open-source /usr/local
COPY --from=builder /src/kvs-streamer/build/kvs-streamer /usr/local/bin/kvs-streamer
COPY --from=builder /src/amazon-kinesis-video-streams-webrtc-sdk-c/certs/cert.pem /usr/lib/ssl/certs/awskvs-cert.pem

RUN ldconfig

RUN mkdir -p /home/$USERNAME/.config/pulse/; \
    echo "default-server=unix:/tmp/pulseaudio.socket" > /home/$USERNAME/.config/pulse/client.conf

# fix: Bad Mojo: Redux causing https://github.com/webanck/docker-wine-steam/issues/12:
RUN sed -i "s/; enable-shm = yes/enable-shm = no/g" /etc/pulse/daemon.conf; \
    sed -i "s/; enable-shm = yes/enable-shm = no/g" /etc/pulse/client.conf

RUN chown -R $USERNAME:$USERNAME /home/$USERNAME

COPY scripts/run_exec.sh /home/$USERNAME
COPY scripts/dbus /usr/bin/dbus
COPY scripts/default.pa /etc/pulse/default.pa

COPY conf/xorg.conf /etc/$USERNAME/xorg.conf
COPY conf/supervisord.conf /etc/$USERNAME/supervisord.conf
COPY conf/supervisord-openbox.conf /etc/$USERNAME/supervisord/openbox.conf

ENV USER=$USERNAME
ENV DISPLAY=:10

CMD ["/usr/bin/supervisord", "-c", "/etc/gamer/supervisord.conf"]
