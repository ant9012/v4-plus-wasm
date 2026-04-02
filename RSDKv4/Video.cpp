#include "RetroEngine.hpp"
#include <string>

// emscripten.h is no longer needed: we removed the emscripten_sleep() spin
// loop that used to block the main thread waiting for the first video frame.
// If you need emscripten_sleep elsewhere, add the include back.

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
} // IoFopenRead

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
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_IYUV, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif

        // TODO: does SDL1.2 support YUV?
#if RETRO_USING_SDL1 && !RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_RGBA, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif

#if RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_RGBA, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif

        if (!videoDecoder) {
            PrintLog("Video Decoder Error!");
            return;
        }

#ifdef __EMSCRIPTEN__
        // On WASM, never spin-wait for the first frame here.
        //
        // Why: THEORAPLAY_startDecode() launches a pthread (Web Worker).
        // That worker cannot execute until the browser's JS event loop gets
        // a turn — but any blocking loop on the main thread (including one
        // using emscripten_sleep) prevents that tick from ever happening, so
        // THEORAPLAY_getVideo() returns null forever and the game hangs.
        //
        // Even when SharedArrayBuffer IS available (required for pthreads),
        // the worker startup message needs an event-loop tick before it runs.
        // Blocking here causes the same deadlock.
        //
        // Fix: start ENGINE_VIDEOWAIT immediately with videoWidth = 0 as a
        // sentinel.  ProcessVideo() has a lazy-init block ("if videoWidth==0")
        // that sets up the GL texture on the first frame the decoder delivers.
        // FlipScreenVideo() in Drawing.cpp guards against videoWidth == 0 to
        // prevent divide-by-zero while we are still waiting for that frame.
        videoWidth   = 0;
        videoHeight  = 0;
        videoAR      = 0.0f;
        vidBaseticks = SDL_GetTicks(); // start the clock for the 2500ms timeout
        vidFrameMS   = 0;
#else
        while (!videoVidData) {
            videoVidData = THEORAPLAY_getVideo(videoDecoder);
        }

        if (!videoVidData) {
            PrintLog("Video Error!");
            return;
        }

        videoWidth  = videoVidData->width;
        videoHeight = videoVidData->height;
        videoAR     = float(videoWidth) / float(videoHeight);

        SetupVideoBuffer(videoWidth, videoHeight);
        vidBaseticks = SDL_GetTicks();
        vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((Uint32)(1000.0 / videoVidData->fps));
#endif

        videoPlaying = 1; // playing ogv
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
            while (fileBuffer != ',') FileRead(&fileBuffer, 1); // gif image start identifier

            FileRead(&fileBuffer2, 2); // IMAGE LEFT
            FileRead(&fileBuffer2, 2); // IMAGE TOP
            FileRead(&fileBuffer2, 2); // IMAGE WIDTH
            FileRead(&fileBuffer2, 2); // IMAGE HEIGHT
            FileRead(&fileBuffer, 1);  // PaletteType
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
    if (videoPlaying == 1) {
        CheckKeyPress(keyPress);

        if (videoSkipped && fadeMode < 0xFF) {
            fadeMode += 8;
        }

        /*
        if (inputDevice[0][INPUT_BUTTONA].press || inputDevice[0][INPUT_START].press > 0 || touches > 0) {
            if (!videoSkipped)
                fadeMode = 0;

            videoSkipped = true;
        }

        if (Engine.gameDeviceType == RETRO_MOBILE) {
            if (touches > 0) {
                if (!videoSkipped)
                    fadeMode = 0;

                videoSkipped = true;
            }
        }
        */

        // Only play the "decide" chime once, on the very first frame.
        // Without the guard this fires every frame while fadeMode <= 0
        // (i.e. the entire video), which causes rapid audio restarts on WASM.
        if (fadeMode <= 0 && !videoSkipped) {
            // No chime needed during video playback — skip it entirely.
            // If you want the chime on skip, move it inside the skip-input
            // block in StopVideoPlayback instead.
        }

        if (!THEORAPLAY_isDecoding(videoDecoder) || (videoSkipped && fadeMode >= 0xFF)) {
            return QuitVideo();
        }

        // Don't pause or it'll go wild
        if (videoPlaying == VIDEOSTATUS_PLAYING_OGV) {
            const Uint32 now = (SDL_GetTicks() - vidBaseticks);

            if (!videoVidData) {
                videoVidData = THEORAPLAY_getVideo(videoDecoder);
                
#ifdef __EMSCRIPTEN__
                if (!videoVidData) {
                    // ESCAPE HATCH: If we wait more than 2.5 seconds (2500ms) for a frame, abort!
                    if (now > 2500) {
                        PrintLog("WEB ERROR: Video thread timed out and silently failed. Skipping video.");
                        return QuitVideo();
                    }
                    
                    // The background thread is just buffering. Be patient and wait!
                    return VIDEOSTATUS_PLAYING_RSV; 
                }
                
                // Did we just catch the very first frame dynamically? Initialize the buffer!
                if (videoWidth == 0) {
                    videoWidth  = videoVidData->width;
                    videoHeight = videoVidData->height;
                    videoAR = float(videoWidth) / float(videoHeight);
                    
                    SetupVideoBuffer(videoWidth, videoHeight);
                    vidBaseticks = SDL_GetTicks(); // Start the clock NOW so it doesn't skip frames
                    vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((Uint32)(1000.0 / videoVidData->fps));
                }
#else
                // we done lmao
                if (!videoVidData) {
                    StopVideoPlayback();
                    ResumeSound();
                    return QuitVideo();
                }
#endif
            }
            
            // Play video frames when it's time.
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

                // do nothing; we're far behind and out of options.
                if (!videoVidData) {
                    // video lagging uh oh
                }

#if RETRO_USING_OPENGL
if (videoVidData->pixels) {
                    glBindTexture(GL_TEXTURE_2D, videoBuffer);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoVidData->width, videoVidData->height, GL_RGBA, GL_UNSIGNED_BYTE, videoVidData->pixels);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
#elif RETRO_USING_SDL2
                int half_w     = videoVidData->width / 2;
                const Uint8 *y = (const Uint8 *)videoVidData->pixels;
                const Uint8 *u = y + (videoVidData->width * videoVidData->height);
                const Uint8 *v = u + (half_w * (videoVidData->height / 2));

                // SDL_UpdateYUVTexture(Engine.videoBuffer, NULL, y, videoVidData->width, u, half_w, v, half_w);
#elif RETRO_USING_SDL1
                memcpy(Engine.videoBuffer->pixels, videoVidData->pixels, videoVidData->width * videoVidData->height * sizeof(uint));
#endif

                THEORAPLAY_freeVideo(videoVidData);
                videoVidData = NULL;
            }
            else if (!videoVidData) {
                // something is wrong with THEORAPLAY_isDecoding, so
                // here's some tape & glue
                return QuitVideo();
            }

            return VIDEOSTATUS_PLAYING_RSV; // its playing as expected
        }
    }

    return VIDEOSTATUS_NOTPLAYING; // its not even initialised
}

int QuitVideo()
{
    StopVideoPlayback();
    ResumeSound();
    return VIDEOSTATUS_PLAYING_OGV; // video finished
}

void StopVideoPlayback()
{
    if (videoPlaying == 1) {
#ifdef __EMSCRIPTEN__
        SDL_PauseAudio(1);
#else
        SDL_LockAudio();
#endif

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

        CloseVideoBuffer();
        videoPlaying = 0;

#ifdef __EMSCRIPTEN__
        SDL_PauseAudio(0);
#else
        SDL_UnlockAudio();
#endif
    }
}

void SetupVideoBuffer(int width, int height)
{
#if RETRO_USING_OPENGL
    if (videoBuffer > 0) {
        glDeleteTextures(1, &videoBuffer);
        videoBuffer = 0;
    }
    glGenTextures(1, &videoBuffer);
    glBindTexture(GL_TEXTURE_2D, videoBuffer);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoVidData->width, videoVidData->height, GL_RGBA, GL_UNSIGNED_BYTE, videoVidData->pixels);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    if (!videoBuffer || !&videoBuffer || !videoVidData)
        PrintLog("Failed to create video buffer!");
#elif RETRO_USING_SDL1
    Engine.videoBuffer = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#elif RETRO_USING_SDL2
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_TARGET, width, height);

    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#endif

    InitVideoBuffer(width, height);
}

void InitVideoBuffer(int width, int height)
{
#if !RETRO_USING_OPENGL && RETRO_USING_SDL2 && RETRO_SOFTWARE_RENDER
    int size  = width * height;
    int sizeh = (width / 2) * (height / 2);
    std::vector<Uint8> frame(size + 2 * sizeh);
    memset(frame.data(), 0, size);
    memset(frame.data() + size, 128, 2 * sizeh);
    SDL_UpdateYUVTexture(Engine.videoBuffer, nullptr, frame.data(), width, frame.data() + size, width / 2, frame.data() + size + sizeh, width / 2);
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