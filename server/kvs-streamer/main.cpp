#include <string>
#include <vector>

#include <getopt.h>

#include <boost/algorithm/string.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include "const.h"
#include "input.h"
#include "Samples.h"

// #define VERBOSE

extern PSampleConfiguration gSampleConfiguration;

#define MOUSE_BUTTON_LEFT 1
#define MOUSE_BUTTON_MIDDLE 2
#define MOUSE_BUTTON_RIGHT 3
#define MOUSE_SCROLL_UP 5
#define MOUSE_SCROLL_DOWN 6

#define KEY_UP 0
#define KEY_DOWN 1

static GstPadProbeReturn
cb_have_data (GstPad          *pad,
              GstPadProbeInfo *info,
              gpointer         user_data)
{
    //DLOGI("[!!!ARA!!!] pad probe received\n");
    return GST_PAD_PROBE_OK;
}

template<typename ... Args>
std::string string_format(const std::string &format, Args ... args) {
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
    if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
    auto size = static_cast<size_t>( size_s );
    auto buf = std::make_unique<char[]>(size);
    std::snprintf(buf.get(), size, format.c_str(), args ...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

// can't define directly in SampleConfiguration due to using of strings and other CPP classes
// (they require 'new' to be called for proper init, not cmalloc)
struct CustomData {
    int video_bitrate;
    std::string display_name;
    int button_code; // last mouse button pressed
} custom_data;

VOID onDataChannelMessage(UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMessage,
                          UINT32 pMessageLen) {
    UNUSED_PARAM(pDataChannel);
    CustomData *my_custom_data = (CustomData *) (((SampleStreamingSession *) customData)->pSampleConfiguration->araCustomData);
    if (isBinary) {
        DLOGI("DataChannel Binary Message");
    } else {
        //DLOGI("DataChannel String Message: %.*s\n", pMessageLen, pMessage);

        std::string msg((char *) pMessage, pMessageLen);
        std::vector<std::string> msg_parts;
        boost::split(msg_parts, msg, boost::is_any_of(","));
        try {
            if (msg_parts[0] == "m") {
                int pos_x = std::stoi(msg_parts[1]);
                int pos_y = std::stoi(msg_parts[2]);
                XMove(pos_x, pos_y);
                int button_mask = std::stoi(msg_parts[3]);
                bool button_pressed = button_mask != 0;
                int button_code = 0;
                // button_mask to button_code (basically it's a "bit set" position)
                while (button_mask)
                {
                    button_mask = button_mask >> 1;
                    ++button_code;
                }
                // inverse scrolling. TODO: make it based on some flag
                if (button_code == 4) {
                    button_code = 5;
                } else if (button_code == 5) {
                    button_code = 4;
                }
                if (button_code != my_custom_data->button_code) {
                    XButton(button_pressed ? button_code : my_custom_data->button_code, button_pressed);
                    my_custom_data->button_code = button_code;
                }
            } else if (msg_parts[0] == "kd" || msg_parts[0] == "ku") {
                KeySym key_code = std::stoi(msg_parts[1]);
                if (msg_parts[0] == "kd") {
                    XKey(key_code, KEY_DOWN);
                } else if (msg_parts[0] == "ku") {
                    XKey(key_code, KEY_UP);
                }
            }
        } catch (...) {

        }
    }
}

VOID onDataChannelCustom(UINT64 customData, PRtcDataChannel pRtcDataChannel) {
    DLOGI("New DataChannel has been opened %s \n", pRtcDataChannel->name);
    dataChannelOnMessage(pRtcDataChannel, customData, onDataChannelMessage);
}

GstFlowReturn on_new_sample(GstElement *sink, gpointer data, UINT64 trackid) {
    GstBuffer *buffer;
    BOOL isDroppable, delta;
    GstFlowReturn ret = GST_FLOW_OK;
    GstSample *sample = NULL;
    GstMapInfo info;
    GstSegment *segment;
    GstClockTime buf_pts;
    Frame frame;
    STATUS status;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) data;
    PSampleStreamingSession pSampleStreamingSession = NULL;
    PRtcRtpTransceiver pRtcRtpTransceiver = NULL;
    UINT32 i;

    if (pSampleConfiguration == NULL) {
        printf("[KVS GStreamer Master] on_new_sample(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    info.data = NULL;
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

    buffer = gst_sample_get_buffer(sample);
    isDroppable = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
                  GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY) ||
                  (GST_BUFFER_FLAGS(buffer) == GST_BUFFER_FLAG_DISCONT) ||
                  (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) &&
                   GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) ||
                  // drop if buffer contains header only and has invalid timestamp
                  !GST_BUFFER_PTS_IS_VALID(buffer);

    if (!isDroppable) {
        delta = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

        frame.flags = delta ? FRAME_FLAG_NONE : FRAME_FLAG_KEY_FRAME;

        // convert from segment timestamp to running time in live mode.
        segment = gst_sample_get_segment(sample);
        buf_pts = gst_segment_to_running_time(segment, GST_FORMAT_TIME, buffer->pts);
        if (!GST_CLOCK_TIME_IS_VALID(buf_pts)) {
            printf("[KVS GStreamer Master] Frame contains invalid PTS dropping the frame. \n");
        }

        if (!(gst_buffer_map(buffer, &info, GST_MAP_READ))) {
            printf("[KVS GStreamer Master] on_new_sample(): Gst buffer mapping failed\n");
            goto CleanUp;
        }

        frame.trackId = trackid;
        frame.duration = 0;
        frame.version = FRAME_CURRENT_VERSION;
        frame.size = (UINT32) info.size;
        frame.frameData = (PBYTE) info.data;

        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            pSampleStreamingSession = pSampleConfiguration->sampleStreamingSessionList[i];
            frame.index = (UINT32) ATOMIC_INCREMENT(&pSampleStreamingSession->frameIndex);

            if (trackid == DEFAULT_AUDIO_TRACK_ID) {
                pRtcRtpTransceiver = pSampleStreamingSession->pAudioRtcRtpTransceiver;
                frame.presentationTs = pSampleStreamingSession->audioTimestamp;
                frame.decodingTs = frame.presentationTs;
                pSampleStreamingSession->audioTimestamp +=
                        SAMPLE_AUDIO_FRAME_DURATION; // assume audio frame size is 20ms, which is default in opusenc
            } else {
                pRtcRtpTransceiver = pSampleStreamingSession->pVideoRtcRtpTransceiver;
                frame.presentationTs = pSampleStreamingSession->videoTimestamp;
                frame.decodingTs = frame.presentationTs;
                pSampleStreamingSession->videoTimestamp += SAMPLE_VIDEO_FRAME_DURATION; // assume video fps is 30
            }
            status = writeFrame(pRtcRtpTransceiver, &frame);
            if (status != STATUS_SRTP_NOT_READY_YET && status != STATUS_SUCCESS) {
#ifdef VERBOSE
                printf("writeFrame() failed with 0x%08x", status);
#endif
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);
    }

    CleanUp:

    if (info.data != NULL) {
        gst_buffer_unmap(buffer, &info);
    }

    if (sample != NULL) {
        gst_sample_unref(sample);
    }

    if (ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) {
        ret = GST_FLOW_EOS;
    }

    return ret;
}

GstFlowReturn on_new_sample_video(GstElement *sink, gpointer data) {
    //DLOGI("[!!!ARA!!!] sending video frame\n");
    return on_new_sample(sink, data, DEFAULT_VIDEO_TRACK_ID);
}

GstFlowReturn on_new_sample_audio(GstElement *sink, gpointer data) {
    return on_new_sample(sink, data, DEFAULT_AUDIO_TRACK_ID);
}

PVOID sendGstreamerAudioVideo(PVOID args) {
    STATUS retStatus = STATUS_SUCCESS;
    GstElement *appsinkVideo = NULL, *appsinkAudio = NULL, *pipeline = NULL;
    GstBus *bus;
    GstMessage *msg;
    GError *error = NULL;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;

    DLOGI("[!!!ARA!!!] client connected, starting streaming\n");

    if (pSampleConfiguration == NULL) {
        printf("[KVS GStreamer Master] sendGstreamerAudioVideo(): operation returned status code: 0x%08x \n",
               STATUS_NULL_ARG);
        goto CleanUp;
    }

    /**
     * Use x264enc as its available on mac, pi, ubuntu and windows
     * mac pipeline fails if resolution is not 720p
     *
     * For alaw
     * audiotestsrc is-live=TRUE ! queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample !
     * audio/x-raw, rate=8000, channels=1, format=S16LE, layout=interleaved ! alawenc ! appsink sync=TRUE emit-signals=TRUE name=appsink-audio
     *
     * For VP8
     * videotestsrc is-live=TRUE ! video/x-raw,width=1280,height=720,framerate=30/1 !
     * vp8enc error-resilient=partitions keyframe-max-dist=10 auto-alt-ref=true cpu-used=5 deadline=1 !
     * appsink sync=TRUE emit-signals=TRUE name=appsink-video
     */

    switch (pSampleConfiguration->mediaType) {
        case SAMPLE_STREAMING_VIDEO_ONLY:
            if (pSampleConfiguration->useTestSrc) {
                pipeline = gst_parse_launch(
                        "videotestsrc is-live=TRUE ! queue ! videoconvert ! video/x-raw,width=1280,height=720,framerate=30/1 ! "
                        "x264enc bframes=0 speed-preset=veryfast bitrate=512 byte-stream=TRUE tune=zerolatency ! "
                        "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! appsink sync=TRUE emit-signals=TRUE name=appsink-video",
                        &error);
            } else {
                pipeline = gst_parse_launch(
                        "autovideosrc ! queue ! videoconvert ! video/x-raw,width=1280,height=720,framerate=[30/1,10000000/333333] ! "
                        "x264enc bframes=0 speed-preset=veryfast bitrate=512 byte-stream=TRUE tune=zerolatency ! "
                        "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! appsink sync=TRUE emit-signals=TRUE name=appsink-video",
                        &error);
            }
            break;

        case SAMPLE_STREAMING_AUDIO_VIDEO:
            if (pSampleConfiguration->useTestSrc) {
                pipeline = gst_parse_launch(
                        "videotestsrc is-live=TRUE ! queue ! videoconvert ! video/x-raw,width=1280,height=720,framerate=30/1 ! "
                        "x264enc bframes=0 speed-preset=veryfast bitrate=512 byte-stream=TRUE tune=zerolatency ! "
                        "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! appsink sync=TRUE "
                        "emit-signals=TRUE name=appsink-video audiotestsrc is-live=TRUE ! "
                        "queue leaky=2 max-size-buffers=400 ! audioconvert ! audioresample ! opusenc ! "
                        "audio/x-opus,rate=48000,channels=2 ! appsink sync=TRUE emit-signals=TRUE name=appsink-audio",
                        &error);
            } else {
                GstElement *el_ximagesrc = gst_element_factory_make("ximagesrc", "x11");
                g_object_set(
                        el_ximagesrc,
                        "show-pointer", 1,
                        "remote", 1,
                        "blocksize", 16384,
                        "use-damage", 0,
                        NULL);
                GstElement *caps_ximagesrc = gst_element_factory_make("capsfilter", NULL);
                g_object_set(
                        caps_ximagesrc,
                        "caps", gst_caps_new_simple(
                                "video/x-raw",
                                "framerate", GST_TYPE_FRACTION, 30, 1,
                                NULL
                        ),
                        NULL);
                GstElement *el_videoconvert = gst_element_factory_make("videoconvert", NULL);
                GstElement *caps_videoconvert = gst_element_factory_make("capsfilter", NULL);
                g_object_set(
                        caps_videoconvert,
                        "caps", gst_caps_new_simple(
                                "video/x-raw",
                                NULL
                        ),
                        NULL);
                GstElement *el_queue_video = gst_element_factory_make("queue", NULL);
                g_object_set(
                    el_queue_video,
                    "leaky", 1,
                    NULL
                );
                GstElement *el_x264enc = gst_element_factory_make("x264enc", NULL);
                g_object_set(
                        el_x264enc,
                        "threads", 1,
                        "bframes", 0,
                        "key-int-max", 0,
                        "byte-stream", true,
                        "tune", 4, // zerolatency
                        "speed-preset", 3, // veryfast
                        "bitrate", ((struct CustomData *) pSampleConfiguration->araCustomData)->video_bitrate,
                        NULL);
                GstElement *caps_x264enc = gst_element_factory_make("capsfilter", NULL);
                g_object_set(
                        caps_x264enc,
                        "caps", gst_caps_new_simple(
                                "video/x-h264",
                                "stream-format", G_TYPE_STRING, "byte-stream",
                                "profile", G_TYPE_STRING, "high",
                                NULL
                        ),
                        NULL);
                GstElement *el_appsink_video = gst_element_factory_make("appsink", "appsink-video");
                g_object_set(
                        el_appsink_video,
                        "emit-signals", TRUE,
                        "sync", FALSE,
                        NULL);
                pipeline = gst_pipeline_new("my_pipeline");
                gst_bin_add_many(
                        GST_BIN(pipeline),
                        el_ximagesrc,
                        caps_ximagesrc,
                        el_videoconvert,
                        caps_videoconvert,
                        el_queue_video,
                        el_x264enc,
                        caps_x264enc,
                        el_appsink_video,
                        NULL);
                if (gst_element_link_many (el_ximagesrc, caps_ximagesrc, el_videoconvert, caps_videoconvert,
                        el_queue_video, el_x264enc, caps_x264enc, el_appsink_video, NULL) != TRUE)
                    g_printerr ("elements could not be linked.\n");

                GstElement *el_pulsesrc = gst_element_factory_make("pulsesrc", "audiosrc");
                g_object_set(
                        el_pulsesrc,
                        "provide-clock", TRUE,
                        "do-timestamp", TRUE,
                        NULL);
                GstElement *el_audioconvert = gst_element_factory_make("audioconvert", NULL);
                GstElement *el_audioresample = gst_element_factory_make("audioresample", NULL);
                GstElement *el_queue_audio = gst_element_factory_make("queue", NULL);
                g_object_set(
                    el_queue_audio,
                    "leaky", 1,
                    NULL
                );
                GstElement *el_opusenc = gst_element_factory_make("opusenc", NULL);
                g_object_set(
                    el_opusenc,
                    "bitrate", 44100,
                    NULL);
                GstElement *el_appsink_audio = gst_element_factory_make("appsink", "appsink-audio");
                g_object_set(
                        el_appsink_audio,
                        "emit-signals", TRUE,
                        "sync", FALSE,
                        NULL);
                gst_bin_add_many(
                        GST_BIN(pipeline),
                        el_pulsesrc,
                        el_audioconvert,
                        el_audioresample,
                        el_queue_audio,
                        el_opusenc,
                        el_appsink_audio,
                        NULL);
                if (gst_element_link_many (el_pulsesrc, el_audioconvert, el_audioresample, el_queue_audio, el_opusenc, el_appsink_audio, NULL) != TRUE)
                    g_printerr ("elements could not be linked.\n");

                //GstPad *pad = gst_element_get_static_pad(el_pulsesrc, "src");
                //gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) cb_have_data, NULL, NULL);
                //gst_object_unref (pad);
                //DLOGI("[!!!ARA!!!] intercepted PAD PROBES\n");

                /*
                std::string pl = string_format(
                        "ximagesrc display-name=%s show-pointer=true use-damage=false ! "
                        "queue ! "
                        "video/x-raw,framerate=30/1 ! videoconvert ! "
                        "x264enc bframes=0 speed-preset=veryfast bitrate=%d byte-stream=true tune=zerolatency ! "
                        "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! "
                        "appsink sync=true emit-signals=true name=appsink-video "
                        "alsasrc ! "
                        "audioconvert ! "
                        "queue ! "
                        "opusenc ! "
                        "audio/x-opus,rate=48000,channels=2 ! "
                        "appsink sync=true emit-signals=true name=appsink-audio",
                        ((struct CustomData*)pSampleConfiguration->araCustomData)->display_name.c_str(),
                        ((struct CustomData*)pSampleConfiguration->araCustomData)->video_bitrate
                );
                printf("[KVS GStreamer Master] !!!PIPELINE!!!: %s\n", pl.c_str());
                pipeline = gst_parse_launch(pl.c_str(), &error);
                 */

            }
            break;
    }

    if (pipeline == NULL) {
        printf("[KVS GStreamer Master] sendGstreamerAudioVideo(): Failed to launch gstreamer, operation returned status code: 0x%08x \n",
               STATUS_INTERNAL_ERROR);
        goto CleanUp;
    }

    appsinkVideo = gst_bin_get_by_name(GST_BIN(pipeline), "appsink-video");
    appsinkAudio = gst_bin_get_by_name(GST_BIN(pipeline), "appsink-audio");

    if (!(appsinkVideo != NULL || appsinkAudio != NULL)) {
        printf("[KVS GStreamer Master] sendGstreamerAudioVideo(): cant find appsink, operation returned status code: 0x%08x \n",
               STATUS_INTERNAL_ERROR);
        goto CleanUp;
    }

    if (appsinkVideo != NULL) {
        g_signal_connect(appsinkVideo, "new-sample", G_CALLBACK(on_new_sample_video), (gpointer) pSampleConfiguration);
    }

    if (appsinkAudio != NULL) {
        g_signal_connect(appsinkAudio, "new-sample", G_CALLBACK(on_new_sample_audio), (gpointer) pSampleConfiguration);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    DLOGI("[!!!ARA!!!] pipeline state set to playing\n");

    /* block until error or EOS */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    /* Free resources */
    if (msg != NULL) {
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    CleanUp:

    if (error != NULL) {
        printf("%s", error->message);
        g_clear_error(&error);
    }

    return (PVOID) (ULONG_PTR) retStatus;
}

int main(INT32 argc, CHAR *argv[]) {
    std::string channel_name = DEFAULT_CHANNEL_NAME;
    bool enable_sound = DEFAULT_ENABLE_SOUND;
    std::string display_name = getenv(ENV_VAR_DISPLAY);
    bool use_testsrc = DEFAULT_USE_TESTSRC;
    UINT32 video_bitrate = DEFAULT_VIDEO_BITRATE;

    setbuf(stdout, NULL);

    option long_options[] = {
            {"channel_name",  required_argument, NULL, CMD_OPT_CHANNEL_NAME},
            {"disable_sound", no_argument,       NULL, CMD_OPT_DISABLE_SOUND},
            {"display_name",  optional_argument, NULL, CMD_OPT_DISPLAY_NAME},
            {"use_testsrc",   optional_argument, NULL, CMD_OPT_USE_TESTSRC},
            {"video_bitrate", optional_argument, NULL, CMD_OPT_VIDEO_BITRATE},
            {NULL, 0,                            NULL, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            case CMD_OPT_CHANNEL_NAME:
                channel_name = optarg;
                break;
            case CMD_OPT_DISABLE_SOUND:
                enable_sound = false;
                break;
            case CMD_OPT_DISPLAY_NAME:
                display_name = optarg;
                break;
            case CMD_OPT_USE_TESTSRC:
                use_testsrc = true;
                break;
            case CMD_OPT_VIDEO_BITRATE:
                video_bitrate = std::stoi(optarg);
                break;
        }
    }

    XDisplaySet(display_name.data());

    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = NULL;
    PCHAR pChannelName;

    SET_INSTRUMENTED_ALLOCATORS();

    signal(SIGINT, sigintHandler);

    // do trickle-ice by default
    printf("[KVS GStreamer Master] Using trickleICE by default\n");

#ifdef IOT_CORE_ENABLE_CREDENTIALS
    CHK_ERR((pChannelName = getenv(IOT_CORE_THING_NAME)) != NULL, STATUS_INVALID_OPERATION, "AWS_IOT_CORE_THING_NAME must be set");
#else
    pChannelName = channel_name.data();
#endif

    retStatus = createSampleConfiguration(pChannelName, SIGNALING_CHANNEL_ROLE_TYPE_MASTER, TRUE, TRUE,
                                          &pSampleConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] createSampleConfiguration(): operation returned status code: 0x%08x \n",
               retStatus);
        goto CleanUp;
    }

    printf("[KVS GStreamer Master] Created signaling channel %s\n", pChannelName);

    if (pSampleConfiguration->enableFileLogging) {
        retStatus =
                createFileLogger(FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES,
                                 (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] createFileLogger(): operation returned status code: 0x%08x \n", retStatus);
            pSampleConfiguration->enableFileLogging = FALSE;
        }
    }

    pSampleConfiguration->videoSource = sendGstreamerAudioVideo;
    pSampleConfiguration->mediaType = enable_sound ? SAMPLE_STREAMING_AUDIO_VIDEO : SAMPLE_STREAMING_VIDEO_ONLY;
    pSampleConfiguration->onDataChannel = onDataChannelCustom;
    pSampleConfiguration->customData = (UINT64) pSampleConfiguration;
    pSampleConfiguration->useTestSrc = use_testsrc;
    pSampleConfiguration->araCustomData = &custom_data;
    custom_data.video_bitrate = video_bitrate;
    custom_data.display_name = display_name;
    custom_data.button_code = 0;

    /* Initialize GStreamer */
    gst_init(&argc, &argv);
    printf("[KVS Gstreamer Master] Finished initializing GStreamer\n");

    // Initalize KVS WebRTC. This must be done before anything else, and must only be done once.
    retStatus = initKvsWebRtc();
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] initKvsWebRtc(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS GStreamer Master] KVS WebRTC initialization completed successfully\n");

    pSampleConfiguration->signalingClientCallbacks.messageReceivedFn = signalingMessageReceived;

    strcpy(pSampleConfiguration->clientInfo.clientId, SAMPLE_MASTER_CLIENT_ID);

    retStatus = createSignalingClientSync(&pSampleConfiguration->clientInfo, &pSampleConfiguration->channelInfo,
                                          &pSampleConfiguration->signalingClientCallbacks,
                                          pSampleConfiguration->pCredentialProvider,
                                          &pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] createSignalingClientSync(): operation returned status code: 0x%08x \n",
               retStatus);
    }
    printf("[KVS GStreamer Master] Signaling client created successfully\n");

    // Enable the processing of the messages
    retStatus = signalingClientFetchSync(pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] signalingClientFetchSync(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

    retStatus = signalingClientConnectSync(pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] signalingClientConnectSync(): operation returned status code: 0x%08x \n",
               retStatus);
        goto CleanUp;
    }
    printf("[KVS GStreamer Master] Signaling client connection to socket established\n");

    printf("[KVS Gstreamer Master] Beginning streaming...check the stream over channel %s\n", pChannelName);

    gSampleConfiguration = pSampleConfiguration;

    // Checking for termination
    retStatus = sessionCleanupWait(pSampleConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] sessionCleanupWait(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

    printf("[KVS GStreamer Master] Streaming session terminated\n");

    CleanUp:

    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS GStreamer Master] Terminated with status code 0x%08x\n", retStatus);
    }

    printf("[KVS GStreamer Master] Cleaning up....\n");

    if (pSampleConfiguration != NULL) {
        // Kick of the termination sequence
        ATOMIC_STORE_BOOL(&pSampleConfiguration->appTerminateFlag, TRUE);

        if (pSampleConfiguration->mediaSenderTid != INVALID_TID_VALUE) {
            THREAD_JOIN(pSampleConfiguration->mediaSenderTid, NULL);
        }

        if (pSampleConfiguration->enableFileLogging) {
            freeFileLogger();
        }
        retStatus = freeSignalingClient(&pSampleConfiguration->signalingClientHandle);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS GStreamer Master] freeSignalingClient(): operation returned status code: 0x%08x \n",
                   retStatus);
        }

        retStatus = freeSampleConfiguration(&pSampleConfiguration);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS GStreamer Master] freeSampleConfiguration(): operation returned status code: 0x%08x \n",
                   retStatus);
        }
    }
    printf("[KVS Gstreamer Master] Cleanup done\n");

    RESET_INSTRUMENTED_ALLOCATORS();

    // https://www.gnu.org/software/libc/manual/html_node/Exit-Status.html
    // We can only return with 0 - 127. Some platforms treat exit code >= 128
    // to be a success code, which might give an unintended behaviour.
    // Some platforms also treat 1 or 0 differently, so it's better to use
    // EXIT_FAILURE and EXIT_SUCCESS macros for portability.
    return STATUS_FAILED(retStatus) ? EXIT_FAILURE : EXIT_SUCCESS;
}
