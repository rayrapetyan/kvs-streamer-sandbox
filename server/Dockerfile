ARG OS_TAG=bullseye

FROM debian:$OS_TAG-slim as builder

ENV DEBIAN_FRONTEND noninteractive

ARG USERNAME=gamer
ARG PASSWORD=gamer
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN set -eux; apt-get update; \
    apt-get install --no-install-recommends -y xserver-xorg-video-dummy x11-apps \
      wget ca-certificates supervisor pulseaudio libcairo2 libxcb1 libxrandr2 libxv1 libopus0 libvpx6 \
      openbox xfce4-terminal firefox-esr; \
    # gst
    apt-get install -y --no-install-recommends libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
      gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-pulseaudio gstreamer1.0-x; \
    # create non-root user TODO: drop sudo
    groupadd --gid $USER_GID $USERNAME; \
    useradd --uid $USER_UID --gid $USER_GID --shell /bin/bash --password $(openssl passwd $PASSWORD) --create-home --home-dir /home/$USERNAME $USERNAME; \
    usermod -aG sudo,audio,video,pulse,render,kvm $USERNAME; \
    #echo "gamer ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers; \
    # setup pulseaudio
    #mkdir -p /home/$USERNAME/.config/pulse/; \
    #echo "default-server=unix:/tmp/pulseaudio.socket" > /home/$USERNAME/.config/pulse/client.conf; \
    # workaround for an X11 problem: http://blog.tigerteufel.de/?p=476
    mkdir /tmp/.X11-unix; \
    chmod 1777 /tmp/.X11-unix; \
    chown $USERNAME /tmp/.X11-unix/; \
    # make directories for user
    mkdir -p /etc/$USERNAME /var/log/$USERNAME; \
    chmod 1777 /var/log/$USERNAME; \
    chown $USERNAME /var/log/$USERNAME/; \
    chown -R $USERNAME:$USERNAME /home/$USERNAME; \
    # clean up
    apt-get clean -y; \
    rm -rf /var/lib/apt/lists/* /var/cache/apt/*

RUN sed -i -E 's/^; autospawn =.*/autospawn = yes/' /etc/pulse/client.conf \
     && touch /etc/pulse/client.conf.d/00-disable-autospawn.conf \
     && sed -i -E 's/^(autospawn=.*)/# \1/' /etc/pulse/client.conf.d/00-disable-autospawn.conf

# fix: Bad Mojo: Redux causing https://github.com/webanck/docker-wine-steam/issues/12:
RUN sed -i "s/; enable-shm = yes/enable-shm = no/g" /etc/pulse/daemon.conf \
    && sed -i "s/; enable-shm = yes/enable-shm = no/g" /etc/pulse/client.conf

WORKDIR /src
RUN apt-get update && apt install -y git pkg-config cmake libcap2 libcap-dev build-essential
RUN apt-get install -y libssl-dev libcurl4-openssl-dev liblog4cplus-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base-apps gstreamer1.0-plugins-bad gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-tools
RUN git clone --recursive https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c.git
RUN mkdir -p amazon-kinesis-video-streams-webrtc-sdk-c/build && cd amazon-kinesis-video-streams-webrtc-sdk-c/build && cmake .. && make install
RUN apt-get install -y x11-xserver-utils

RUN cp -r /src/amazon-kinesis-video-streams-webrtc-sdk-c/open-source/lib/* /usr/lib
RUN cp -r /src/amazon-kinesis-video-streams-webrtc-sdk-c/build/*.so /usr/lib

RUN git clone https://github.com/jtanx/libclipboard
RUN cd libclipboard && cmake . && make -j4 && make install

RUN apt-get install -y libxrandr-dev libxtst-dev libboost-dev i965-va-driver gstreamer1.0-vaapi libva2 libva-drm2 libva-x11-2 vainfo intel-media-va-driver gstreamer1.0-alsa
COPY kvs-streamer /src/kvs-streamer
RUN mkdir kvs-streamer/build && cd kvs-streamer/build && cmake .. && make
RUN cp kvs-streamer/build/kvs-streamer /usr/bin/kvs-streamer

COPY run_exec.sh /home/gamer

COPY dbus /usr/bin/dbus
#COPY default.pa /etc/pulse/default.pa
COPY supervisord.conf /etc/$USERNAME/supervisord.conf
COPY xorg.conf /etc/$USERNAME/xorg.conf

COPY supervisord-openbox.conf /etc/$USERNAME/supervisord/openbox.conf

ENV USER=$USERNAME
ENV DISPLAY=:10

CMD ["/usr/bin/supervisord", "-c", "/etc/gamer/supervisord.conf"]
