
#define MINI_AL_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
//#define MAL_DEBUG_OUTPUT

#include "dr_wav.h"
#include "mini_al.h"

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#undef min
#undef max

struct SampleData
{
    float* data;
    mal_uint64 numFrames;
};

SampleData gMusic;
SampleData gAlphabet['z' - 'a' + 1];

struct PlayState
{
    mal_uint64 frameOffset;
    size_t sampleDataIndex;
};

mal_uint64 gMusicFrameOffset = 0;
std::vector<PlayState> gPlayStates;
std::mutex gPlayStateMutex;

mal_uint32 on_send_frames_to_device(mal_device* pDevice, mal_uint32 frameCount, void* pSamples)
{
    float* sampleBuffer = reinterpret_cast<float*>(pSamples);
    memset(sampleBuffer, 0, sizeof(float) * frameCount * pDevice->channels);

    gPlayStateMutex.lock();
    for (PlayState& state : gPlayStates)
    {
        const SampleData& sampleData = gAlphabet[state.sampleDataIndex];
        if (!sampleData.data)
        {
            continue;
        }

        mal_uint64 frameOffset = state.frameOffset;

        const mal_uint64 numFramesToMix = std::min<mal_uint64>(sampleData.numFrames - frameOffset, frameCount);
        if (numFramesToMix > 0)
        {
            state.frameOffset += numFramesToMix;
            const mal_uint64 numSamplesToMix = numFramesToMix * pDevice->channels;
            // no clip handling, overflow ub
            for (size_t i = 0; i < numSamplesToMix; ++i)
            {
                sampleBuffer[i] += static_cast<float>(0.1f * sampleData.data[frameOffset * pDevice->channels + i]);
            }
        }
    }
    gPlayStateMutex.unlock();

    if (gMusic.data)
    {
        mal_uint64 frameOffset = gMusicFrameOffset;
        const mal_uint64 numFramesToMix = std::min<mal_uint64>(gMusic.numFrames - gMusicFrameOffset, frameCount);
        if (numFramesToMix > 0)
        {
            gMusicFrameOffset = (gMusicFrameOffset + numFramesToMix) % gMusic.numFrames;
            const mal_uint64 numSamplesToMix = numFramesToMix * pDevice->channels;
            // no clip handling, overflow ub
            for (size_t i = 0; i < numSamplesToMix; ++i)
            {
                sampleBuffer[i] += static_cast<float>(0.1f * gMusic.data[frameOffset * pDevice->channels + i]);
            }
        }
    }
    return frameCount;
}

std::atomic<int> gQuitTextProcessThread = 0;
std::mutex gTextProcessLock;
std::vector<std::string> gTextProcessQueue;

void TextProcessThread()
{
    std::vector<std::string> lines;
    std::chrono::milliseconds speakPeriod = std::chrono::milliseconds(80);

    while (!gQuitTextProcessThread)
    {
        gTextProcessLock.lock();
        if (!gTextProcessQueue.empty())
        {
            std::move(gTextProcessQueue.begin(), gTextProcessQueue.end(), std::back_inserter(lines));
            gTextProcessQueue.clear();
        }
        gTextProcessLock.unlock();

        if (!lines.empty())
        {
            std::string& line = lines.front();
            if (!line.empty())
            {
                char c = tolower(line.front());
                if (c >= 'a' && c <= 'z')
                {
                    PlayState state;
                    state.frameOffset = 0;
                    state.sampleDataIndex = c - 'a';
                    gPlayStateMutex.lock();
                    gPlayStates.emplace_back(state);
                    gPlayStateMutex.unlock();
                }
                line.erase(line.begin());
            }

            if (line.empty())
            {
                lines.erase(lines.begin());
            }
        }

        gPlayStateMutex.lock();
        auto it = std::remove_if(gPlayStates.begin(), gPlayStates.end(), [](const PlayState& state)
        {
            return state.frameOffset >= gAlphabet[state.sampleDataIndex].numFrames;
        });
        gPlayStates.erase(it, gPlayStates.end());
        gPlayStateMutex.unlock();

        std::this_thread::sleep_until(std::chrono::steady_clock::now() + speakPeriod);
    }
}

int main(int argc, char* argv[])
{
    {
        // fix up working directory
        char temp[128] = {};
        const char *dir = getcwd(temp, sizeof(temp));
        const char *bin_pos = strstr(dir, "bin");
        const char *build_pos_win = strstr(dir, "vs20");
        const char *build_pos_lin = strstr(dir, "gmake");
        const char *build_pos = build_pos_win ? build_pos_win : build_pos_lin;
        if (bin_pos || build_pos)
        {
            chdir("..");
            if (build_pos > bin_pos)
            {
                chdir("..");
            }
        }
    }

    if ((argc != 2) || !argv[1])
    {
        std::cout << "Usage: " << argv[0] << " <voicedir>\n";
        return 0;
    }

    char temp[128] = {};
    const char *dir = getcwd(temp, sizeof(temp));
    std::cout << temp << "\n";

    {
        mal_decoder_config config;
        char voicePath[_MAX_PATH];
        for (char c = 'a'; c != ('z' + 1); ++c)
        {
            snprintf(voicePath, sizeof(voicePath), "%s/%c.wav", argv[1], c);

            config = mal_decoder_config_init(mal_format_f32, 2, 22050);
            SampleData& data = gAlphabet[c - 'a'];
            const mal_result res = mal_decode_file(voicePath, &config, &data.numFrames, reinterpret_cast<void**>(&data.data));
            if (res != MAL_SUCCESS)
            {
                std::cout << "Error loading .wav \'" << voicePath << "\'\n";
            }
        }

        config = mal_decoder_config_init(mal_format_f32, 2, 48000);
        const mal_result res = mal_decode_file("music/music2.wav", &config, &gMusic.numFrames, reinterpret_cast<void**>(&gMusic.data));
    }

    mal_device_config config = mal_device_config_init_playback(mal_format_f32, 2, 48000, &on_send_frames_to_device);

    mal_device device;
    if (mal_device_init(NULL, mal_device_type_playback, NULL, &config, NULL, &device) != MAL_SUCCESS)
    {
        std::cout << "Failed to open playback device.\n";
        return -1;
    }

    if (mal_device_start(&device) != MAL_SUCCESS)
    {
        std::cout << "Failed to start playback device.\n";
        mal_device_uninit(&device);
        return -1;
    }

    std::thread textProcessThread(&TextProcessThread);

    std::string line;
    while ((std::cin >> line) && (line != "quit"))
    {
        gTextProcessLock.lock();
        gTextProcessQueue.emplace_back(line);
        gTextProcessLock.unlock();
        //std::cout << "> " << line << '\n';
    }

    gQuitTextProcessThread = true;
    textProcessThread.join();

    mal_device_uninit(&device);
    return 0;
}
