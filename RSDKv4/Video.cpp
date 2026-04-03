#include "RetroEngine.hpp"
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if RETRO_USING_SDL2
extern SDL_AudioStream *ogv_stream;
extern SDL_AudioSpec audioDeviceFormat;
#endif


enum VideoStatus {
    VIDEOSTATUS_NOTPLAYING,
    VIDEOSTATUS_PLAYING_OGV,
    VIDEOSTATUS_PLAYING_RSV,
};

int currentVideoFrame = 0;
int videoFrameCount   = 0;
int videoWidth        = 0;
int videoHeight       = 0;
float videoAR         = 0;

THEORAPLAY_Decoder *videoDecoder;
const THEORAPLAY_VideoFrame *videoVidData;
THEORAPLAY_Io callbacks;

byte videoSurface = 0;
int videoFilePos  = 0;
int videoPlaying  = 0;
int vidFrameMS    = 0;
int vidBaseticks  = 0;

bool videoSkipped = false;

static long videoRead(THEORAPLAY_Io *io, void *buf, long buflen)
{
    FileIO *file    = (FileIO *)io->userdata;
    const size_t br = fRead(buf, 1, buflen * sizeof(byte), file);
    if (br == 0)
        return -1;
    return (int)br;
}

static void videoClose(THEORAPLAY_Io *io)
{
    FileIO *file = (FileIO *)io->userdata;
    fClose(file);
}

void PlayVideoFile(char *filePath, int audioTrack)
{
    char pathBuffer[0x100];
    int len = StrLength(filePath);

    if (StrComp(filePath + ((size_t)len - 2), "us")) {
        filePath[len - 2] = 0;
    }

    StrCopy(pathBuffer, "Data/Videos/");
    StrAdd(pathBuffer, filePath);
    StrAdd(pathBuffer, ".ogv");

    bool addPath = true;
    char pathLower[0x100];
    memset(pathLower, 0, sizeof(char) * 0x100);
    for (int c = 0; c < strlen(pathBuffer); ++c) {
        pathLower[c] = tolower(pathBuffer[c]);
    }

#if RETRO_USE_MOD_LOADER
    for (int m = 0; m < modList.size(); ++m) {
        if (modList[m].active) {
            std::map<std::string, std::string>::const_iterator iter = modList[m].fileMap.find(pathLower);
            if (iter != modList[m].fileMap.cend()) {
                StrCopy(pathBuffer, iter->second.c_str());
                Engine.usingDataFile = false;
                addPath              = false;
                break;
            }
        }
    }
#endif

    char filepath[0x100];
    if (addPath) {
#if RETRO_PLATFORM == RETRO_UWP
        static char resourcePath[256] = { 0 };

        if (strlen(resourcePath) == 0) {
            auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            auto path   = to_string(folder.Path());

            std::copy(path.begin(), path.end(), resourcePath);
        }

        sprintf(filepath, "%s/%s", resourcePath, pathBuffer);
#elif RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_ANDROID
        sprintf(filepath, "%s/%s", gamePath, pathBuffer);
#else
        sprintf(filepath, "%s%s", BASE_PATH, pathBuffer);
#endif
    }
    else {
        sprintf(filepath, "%s", pathBuffer);
    }

    FileIO *file = fOpen(filepath, "rb");
    if (file) {
        PrintLog("Loaded File '%s'!", filepath);

        callbacks.read     = videoRead;
        callbacks.close    = videoClose;
        callbacks.userdata = (void *)file;

#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, 30, THEORAPLAY_VIDFMT_IYUV, audioTrack);
#endif

#if RETRO_USING_SDL1 && !RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, 30, THEORAPLAY_VIDFMT_RGBA, audioTrack);
#endif

#if RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, 30, THEORAPLAY_VIDFMT_RGBA, audioTrack);
#endif

        if (!videoDecoder) {
            PrintLog("Video Decoder Error!");
            return;
        }

        while (!videoVidData) {
            videoVidData = THEORAPLAY_getVideo(videoDecoder);
            if (!videoVidData)
                SDL_Delay(1);
            if (!videoVidData)
                SDL_Delay(1);
        }

        if (!videoVidData) {
            PrintLog("Video Error!");
            return;
        }

        videoWidth  = videoVidData->width;
        videoHeight = videoVidData->height;
        videoAR     = float(videoWidth) / float(videoHeight);
        videoAR     = float(videoWidth) / float(videoHeight);

        SetupVideoBuffer(videoWidth, videoHeight);
        vidBaseticks = SDL_GetTicks();
        vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((Uint32)(1000.0 / videoVidData->fps));

#if RETRO_USING_SDL2
        // Clear any leftover audio data from previous video
        if (ogv_stream) {
            SDL_AudioStreamClear(ogv_stream);
        }
#endif

        videoPlaying = 1;
        trackID      = TRACK_COUNT - 1;

        videoSkipped    = false;
        Engine.gameMode = ENGINE_VIDEOWAIT;
    }
    else {
        PrintLog("Couldn't find file '%s'!", filepath);
    }
}

void UpdateVideoFrame()
{
    if (videoPlaying == 2) {
        if (currentVideoFrame < videoFrameCount) {
            GFXSurface *surface = &gfxSurface[videoSurface];
            byte fileBuffer     = 0;
            ushort fileBuffer2  = 0;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 8;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 16;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 24;

            byte clr[3];
            for (int i = 0; i < 0x80; ++i) {
                FileRead(&clr, 3);
                activePalette32[i].r = clr[0];
                activePalette32[i].g = clr[1];
                activePalette32[i].b = clr[2];
                activePalette[i]     = ((ushort)(clr[0] >> 3) << 11) | 32 * (clr[1] >> 2) | (clr[2] >> 3);
            }

            FileRead(&fileBuffer, 1);
            while (fileBuffer != ',') FileRead(&fileBuffer, 1);
            while (fileBuffer != ',') FileRead(&fileBuffer, 1);

            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer, 1);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer2, 2);
            FileRead(&fileBuffer, 1);
            bool interlaced = (fileBuffer & 0x40) >> 6;
            if (fileBuffer >> 7 == 1) {
                int c = 0x80;
                do {
                    ++c;
                    FileRead(&fileBuffer, 1);
                    FileRead(&fileBuffer, 1);
                    FileRead(&fileBuffer, 1);
                } while (c != 0x100);
            }
            ReadGifPictureData(surface->width, surface->height, interlaced, graphicData, surface->dataPosition);

            SetFilePosition(videoFilePos);
            ++currentVideoFrame;
        }
        else {
            videoPlaying = VIDEOSTATUS_NOTPLAYING;
            CloseFile();
        }
    }
}

int ProcessVideo()
{
    static bool frameRendered = false; // Track if we've rendered a frame this session
    
    if (videoPlaying == 1) {
        CheckKeyPress(keyPress);

        if (videoSkipped && fadeMode < 0xFF) {
            fadeMode += 8;
        }

        if (fadeMode <= 0) {
            PlaySfxByName("Menu Decide", false);
        }

        if (!THEORAPLAY_isDecoding(videoDecoder) || (videoSkipped && fadeMode >= 0xFF)) {
            frameRendered = false;
            return QuitVideo();
        }

        {
            const Uint32 now = (SDL_GetTicks() - vidBaseticks);

            if (!videoVidData)
                videoVidData = THEORAPLAY_getVideo(videoDecoder);

            // Only render when we have a frame that's due
            if (videoVidData && (videoVidData->playms <= now)) {
                if (vidFrameMS && ((now - videoVidData->playms) >= vidFrameMS)) {
                    const THEORAPLAY_VideoFrame *last = videoVidData;
                    while ((videoVidData = THEORAPLAY_getVideo(videoDecoder)) != NULL) {
                        THEORAPLAY_freeVideo(last);
                        last = videoVidData;
                        if ((now - videoVidData->playms) < vidFrameMS)
                            break;
                    }

                    if (!videoVidData)
                        videoVidData = last;
                }

                if (videoVidData && videoVidData->pixels) {
                    // Clear framebuffer to black (letterbox/pillarbox areas)
                    if (!frameRendered) {
                        memset(Engine.frameBuffer, 0, GFX_LINESIZE * SCREEN_YSIZE * sizeof(ushort));
                        frameRendered = true;
                    }

                    // Render video frame into the software framebuffer.
                    const uint32_t *src = (const uint32_t *)videoVidData->pixels;

                    // Calculate scaling to fit video into screen
                    float scaleX = (float)videoVidData->width / (float)SCREEN_XSIZE;
                    float scaleY = (float)videoVidData->height / (float)SCREEN_YSIZE;
                    float scale  = (scaleX > scaleY) ? scaleX : scaleY;

                    int destW = (int)(videoVidData->width / scale);
                    int destH = (int)(videoVidData->height / scale);
                    int destX = (SCREEN_XSIZE - destW) / 2;
                    int destY = (SCREEN_YSIZE - destH) / 2;

                    // Optimized blit - process as 32-bit RGBA directly
                    for (int y = 0; y < destH; y++) {
                        int srcY = (int)(y * scale);
                        if (srcY >= videoVidData->height)
                            srcY = videoVidData->height - 1;

                        int dy = destY + y;
                        if (dy < 0 || dy >= SCREEN_YSIZE)
                            continue;

                        ushort *dstRow = &Engine.frameBuffer[dy * GFX_LINESIZE + destX];
                        const uint32_t *srcRow = &src[srcY * videoVidData->width];

                        for (int x = 0; x < destW; x++) {
                            int srcX = (int)(x * scale);
                            if (srcX >= videoVidData->width)
                                srcX = videoVidData->width - 1;

                            uint32_t rgba = srcRow[srcX];
                            byte r = (rgba >> 0) & 0xFF;
                            byte g = (rgba >> 8) & 0xFF;
                            byte b = (rgba >> 16) & 0xFF;

                            dstRow[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                        }
                    }

                    THEORAPLAY_freeVideo(videoVidData);
                    videoVidData = NULL;
                }
            }
            // else: No new frame yet — Engine.frameBuffer retains the last frame

            return VIDEOSTATUS_PLAYING_RSV;
        }
    }
    else {
        frameRendered = false;
    }

    return VIDEOSTATUS_NOTPLAYING;
}

int QuitVideo()
{
    StopVideoPlayback();
    ResumeSound();
    return VIDEOSTATUS_PLAYING_OGV; // video finished (signals engine to leave ENGINE_VIDEOWAIT)
}

void StopVideoPlayback()
{
    if (videoPlaying == 1) {
        SDL_LockAudio();

        if (videoSkipped && fadeMode >= 0xFF)
            fadeMode = 0;

        if (videoVidData) {
            THEORAPLAY_freeVideo(videoVidData);
            videoVidData = NULL;
        }
        if (videoDecoder) {
            THEORAPLAY_stopDecode(videoDecoder);
            videoDecoder = NULL;
        }

#if RETRO_USING_SDL2
        // Clear the stream but don't free it — it's reused for future videos
        if (ogv_stream) {
            SDL_AudioStreamClear(ogv_stream);
        }
#endif

        CloseVideoBuffer();
        videoPlaying = 0;

        SDL_UnlockAudio();
    }
}

void SetupVideoBuffer(int width, int height)
{
#if RETRO_USING_OPENGL
PrintLog("SetupVideoBuffer: creating texture %dx%d", width, height);
    if (videoBuffer > 0) {
        glDeleteTextures(1, &videoBuffer);
        videoBuffer = 0;
    }
    glGenTextures(1, &videoBuffer);
    glBindTexture(GL_TEXTURE_2D, videoBuffer);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (videoVidData && videoVidData->pixels) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        videoVidData->width, videoVidData->height,
                        GL_RGBA, GL_UNSIGNED_BYTE, videoVidData->pixels);
    }
    PrintLog("SetupVideoBuffer: created texture ID %d", videoBuffer);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!videoBuffer)

    if (!videoBuffer)
        PrintLog("Failed to create video buffer!");
#elif RETRO_USING_SDL1
    Engine.videoBuffer = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#elif RETRO_USING_SDL2
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);

    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#endif

    InitVideoBuffer(width, height);
}

void InitVideoBuffer(int width, int height)
{
#if !RETRO_USING_OPENGL && RETRO_USING_SDL2 && RETRO_SOFTWARE_RENDER
    // Clear the YUV texture to black (Y=0, U=V=128) so the first
    // frame doesn't flash garbage.
    int size  = width * height;
    int sizeh = (width / 2) * (height / 2);
    std::vector<Uint8> frame(size + 2 * sizeh);
    memset(frame.data(), 0, size);
    memset(frame.data() + size, 128, 2 * sizeh);
    SDL_UpdateYUVTexture(Engine.videoBuffer, nullptr,
                         frame.data(), width,
                         frame.data() + size, width / 2,
                         frame.data() + size + sizeh, width / 2);
    SDL_UpdateYUVTexture(Engine.videoBuffer, nullptr,
                         frame.data(), width,
                         frame.data() + size, width / 2,
                         frame.data() + size + sizeh, width / 2);
#endif
}

void CloseVideoBuffer()
{
    if (videoPlaying == 1) {
#if RETRO_USING_OPENGL
        if (videoBuffer > 0) {
            glDeleteTextures(1, &videoBuffer);
            videoBuffer = 0;
        }
#elif RETRO_USING_SDL1
        SDL_FreeSurface(Engine.videoBuffer);
        Engine.videoBuffer = nullptr;
#elif RETRO_USING_SDL2
        SDL_DestroyTexture(Engine.videoBuffer);
        Engine.videoBuffer = nullptr;
#endif
    }
}