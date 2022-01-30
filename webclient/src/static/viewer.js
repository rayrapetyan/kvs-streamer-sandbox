/**
 * This file demonstrates the process of starting WebRTC streaming using a KVS Signaling Channel.
 */
const viewer = {};

class CustomSigner {
    constructor (_url) {
        this.url = _url;
    }
    getSignedURL () {
        return this.url;
    }
}

async function startViewer(localView, remoteView, formValues, onStatsReport, onRemoteDataMessage) {
    let useTrickleICE = true;

    viewer.localView = localView;
    viewer.remoteView = remoteView;
    viewer.input = new Input(remoteView, data => {
        if (viewer.dataChannel.readyState === 'open') sendViewerMessage(data);
    });

    let kinesisInfo = null;
    let clientId = getRandomClientId();
    let request = new XMLHttpRequest();
    request.open('GET', `/kinesis-video-url?clientId=${clientId}`, false);  // `false` makes the request synchronous
    request.send(null);
    if (request.status === 200) {
        console.log(request.responseText);
        kinesisInfo = JSON.parse(request.responseText);
    }

    // Create Signaling Client
    viewer.signalingClient = new window.KVSWebRTC.SignalingClient({
        requestSigner: new CustomSigner(kinesisInfo.url),
        channelEndpoint: 'default endpoint (any text) as endpoint is already part of signedurl',
        channelARN: 'default channel, (any text) as channelARN is already part of signedurl',
        clientId: clientId,
        role: window.KVSWebRTC.Role.VIEWER,
        region: 'default region, (any text) as region is already part of signedurl',
    });

    viewer.peerConnection = new RTCPeerConnection(kinesisInfo.configuration);

    viewer.dataChannel = viewer.peerConnection.createDataChannel('kvsDataChannel');
    viewer.peerConnection.ondatachannel = event => {
        event.channel.onmessage = onRemoteDataMessage;
    };

    // Poll for connection stats
    viewer.peerConnectionStatsInterval = setInterval(() => viewer.peerConnection.getStats().then(onStatsReport), 1000);

    viewer.signalingClient.on('open', async () => {
        console.log('[VIEWER] Connected to signaling service');

        // Create an SDP offer to send to the master
        console.log('[VIEWER] Creating SDP offer');
        await viewer.peerConnection.setLocalDescription(
            await viewer.peerConnection.createOffer({
                offerToReceiveAudio: true,
                offerToReceiveVideo: true,
            }),
        );

        // When trickle ICE is enabled, send the offer now and then send ICE candidates as they are generated. Otherwise wait on the ICE candidates.
        if (useTrickleICE) {
            console.log('[VIEWER] Sending SDP offer');
            viewer.signalingClient.sendSdpOffer(viewer.peerConnection.localDescription);
        }
        console.log('[VIEWER] Generating ICE candidates');
    });

    viewer.signalingClient.on('sdpAnswer', async answer => {
        // Add the SDP answer to the peer connection
        console.log('[VIEWER] Received SDP answer');
        await viewer.peerConnection.setRemoteDescription(answer);
    });

    viewer.signalingClient.on('iceCandidate', candidate => {
        // Add the ICE candidate received from the MASTER to the peer connection
        console.log('[VIEWER] Received ICE candidate');
        viewer.peerConnection.addIceCandidate(candidate);
    });

    viewer.signalingClient.on('close', () => {
        console.log('[VIEWER] Disconnected from signaling channel');
    });

    viewer.signalingClient.on('error', error => {
        console.error('[VIEWER] Signaling client error: ', error);
    });

    // Send any ICE candidates to the other peer
    viewer.peerConnection.addEventListener('icecandidate', ({ candidate }) => {
        if (candidate) {
            console.log('[VIEWER] Generated ICE candidate');

            // When trickle ICE is enabled, send the ICE candidates as they are generated.
            if (useTrickleICE) {
                console.log('[VIEWER] Sending ICE candidate');
                viewer.signalingClient.sendIceCandidate(candidate);
            }
        } else {
            console.log('[VIEWER] All ICE candidates have been generated');

            // When trickle ICE is disabled, send the offer now that all the ICE candidates have ben generated.
            if (!useTrickleICE) {
                console.log('[VIEWER] Sending SDP offer');
                viewer.signalingClient.sendSdpOffer(viewer.peerConnection.localDescription);
            }
        }
    });

    // As remote tracks are received, add them to the remote view
    viewer.peerConnection.addEventListener('track', event => {
        console.log('[VIEWER] Received remote track');
        if (remoteView.srcObject) {
            return;
        }
        viewer.remoteStream = event.streams[0];
        remoteView.srcObject = viewer.remoteStream;

        viewer.input.attach();
    });

    console.log('[VIEWER] Starting viewer connection');
    viewer.signalingClient.open();
}

function stopViewer() {
    console.log('[VIEWER] Stopping viewer connection');
    if (viewer.signalingClient) {
        viewer.signalingClient.close();
        viewer.signalingClient = null;
    }

    if (viewer.peerConnection) {
        viewer.peerConnection.close();
        viewer.peerConnection = null;
    }

    if (viewer.localStream) {
        viewer.localStream.getTracks().forEach(track => track.stop());
        viewer.localStream = null;
    }

    if (viewer.remoteStream) {
        viewer.remoteStream.getTracks().forEach(track => track.stop());
        viewer.remoteStream = null;
    }

    if (viewer.peerConnectionStatsInterval) {
        clearInterval(viewer.peerConnectionStatsInterval);
        viewer.peerConnectionStatsInterval = null;
    }

    if (viewer.remoteView) {
        viewer.remoteView.srcObject = null;
    }

    if (viewer.dataChannel) {
        viewer.dataChannel = null;
    }

    if (viewer.input) {
        viewer.input = null;
    }
}

function sendViewerMessage(message) {
    if (viewer.dataChannel) {
        try {
            viewer.dataChannel.send(message);
        } catch (e) {
            console.error('[VIEWER] Send DataChannel: ', e.toString());
        }
    }
}
