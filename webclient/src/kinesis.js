const KinesisVideo = require('aws-sdk/clients/kinesisvideo');
const SigV4RequestSigner = require('amazon-kinesis-video-streams-webrtc').SigV4RequestSigner;
const KinesisVideoSignalingChannels = require('aws-sdk/clients/kinesisvideosignalingchannels');
require('dotenv').config();

const REQUEST_TIME_OUT_VALUE = 'TIMEOUT!';
const REQUEST_TIME_OUT_TIME = 15 * 1000;
const requestTimeout = (time, value) => {
    return new Promise((resolve, reject) => {
        setTimeout(() => {
            resolve(value);
        }, time);
    });
};

class KinesisUtil {
    constructor () {
        this.credential = {
            region: process.env.KINESIS_REGION,
            accessKeyId: process.env.KINESIS_ACCESS_KEY_ID,
            secretAccessKey: process.env.KINESIS_SECRET_ACCESS_KEY,
        };

        if (!this.kinesisClient) {
            try {
                this.kinesisClient = new KinesisVideo({ ...this.credential, correctClockSkew: true });
            } catch (err) {
                console.error('Error creating Kinesis Client:', err.message);
            };
        }
    }

    async createChannel (ChannelName, role = 'VIEWER', clientId = null,
                         natTraversalDisabled = false, forceTURN = true) {
        try {
            const result = { errorCode: 400 };
            /* CHECK IF THE CHANNEL EXISTS */
            const list = await this.kinesisClient.listSignalingChannels({
                ChannelNameCondition: {
                    ComparisonOperator: 'BEGINS_WITH',
                    ComparisonValue: ChannelName
                },
                MaxResults: 1
            }).promise();

            if (list.ChannelInfoList.length) {
                /* CHANNEL ALREADY EXISTS */
                console.warn('Channel already exists:', ChannelName);
            } else {
                /* CREATE NEW CHANNEL */
                await this.kinesisClient.createSignalingChannel({ ChannelName }).promise();
            }

            const describeSignalingChannelResponse = await this.kinesisClient.describeSignalingChannel({ ChannelName }).promise();
            const channelInfo = describeSignalingChannelResponse.ChannelInfo;

            const endpointsByProtocol = await this.listEndpoints(channelInfo.ChannelARN, role);
            if (!endpointsByProtocol) {
                result.errorCode = 404;
                return result;
            }

            const iceServers = await this.listICEServers(channelInfo.ChannelARN, endpointsByProtocol.HTTPS,
                natTraversalDisabled, forceTURN);
            if (!iceServers) {
                result.errorCode = 404;
                return result;
            }

            const configuration = {
                iceServers,
                iceTransportPolicy: forceTURN ? 'relay' : 'all'
            };

            let queryParams = {
                'X-Amz-ChannelARN': channelInfo.ChannelARN
            };
            if (clientId) {
                queryParams = {
                    ...queryParams,
                    'X-Amz-ClientId': clientId
                };
            }
            const signer = new SigV4RequestSigner(process.env.KINESIS_REGION, this.credential);
            const url = await signer.getSignedURL(endpointsByProtocol.WSS, queryParams);
            console.log('Kinesis created channel ARN:', channelInfo.ChannelARN);
            const response = { configuration, url, role };
            return response;
        } catch (err) {
            console.error('Error creating channel: ', err.message);
        }
    }

    async listEndpoints (channelARN, role) {
        const getSignalingChannelEndpoint = this.kinesisClient.getSignalingChannelEndpoint({
            ChannelARN: channelARN,
            SingleMasterChannelEndpointConfiguration: {
                Protocols: ['WSS', 'HTTPS'],
                Role: role
            }
        }).promise();

        const getSignalingChannelEndpointResponse = await Promise.race([
            getSignalingChannelEndpoint,
            requestTimeout(REQUEST_TIME_OUT_TIME, REQUEST_TIME_OUT_VALUE)
        ]);

        if (getSignalingChannelEndpointResponse === REQUEST_TIME_OUT_VALUE) {
            console.error('getSignalingChannelEndpoint timeout!');
            return null;
        }

        const endpointsByProtocol = getSignalingChannelEndpointResponse.ResourceEndpointList.reduce(
            (endpoints, endpoint) => {
                endpoints[endpoint.Protocol] = endpoint.ResourceEndpoint;
                return endpoints;
            }, {}
        );

        return endpointsByProtocol;
    }

    async listICEServers (channelARN, endpoint, natTraversalDisabled, forceTURN) {
        const kinesisVideoSignalingChannelsClient = new KinesisVideoSignalingChannels({
            ...this.credential, endpoint: endpoint, correctClockSkew: true
        });

        const getIceServerConfig = kinesisVideoSignalingChannelsClient.getIceServerConfig({
            ChannelARN: channelARN
        }).promise();

        const getIceServerConfigResponse = await Promise.race([
            getIceServerConfig,
            requestTimeout(REQUEST_TIME_OUT_TIME, REQUEST_TIME_OUT_VALUE)
        ]);

        if (getIceServerConfigResponse === REQUEST_TIME_OUT_VALUE) {
            console.error('getIceServerConfigResponse timeout!');
            return null;
        }

        const iceServers = [];

        if (!natTraversalDisabled && !forceTURN) {
            iceServers.push({ urls: `stun:stun.kinesisvideo.${this.credential.region}.amazonaws.com:443` });
        }
        if (!natTraversalDisabled) {
            getIceServerConfigResponse.IceServerList.forEach(iceServer =>
                iceServers.push({
                    urls: iceServer.Uris,
                    username: iceServer.Username,
                    credential: iceServer.Password
                })
            );
        }

        return iceServers;
    }

    async deleteChannel (ChannelARN) {
        try {
            await this.kinesisClient.deleteSignalingChannel({ ChannelARN }).promise();
        } catch (err) {
            console.error('Error deleting channel: ', err.message);
        }
    }
}

module.exports = {
    kinesis: new KinesisUtil()
};
