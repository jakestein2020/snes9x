/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include "CXAudio2.h"
#include "../snes9x.h"
#include "../apu/apu.h"
#include "wsnes9x.h"
#include <process.h>
#include "commctrl.h"
#include <assert.h>
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

/* CXAudio2
    Implements audio output through XAudio2.
    Basic idea: one master voice and one source voice are created;
    the source voice consumes buffers queued by PushBuffer, plays them through
    the master voice and calls OnBufferEnd after each buffer.
    ProcessSound copies new samples into the buffer and queues them for playback.
*/

/*  Construction/Destruction
*/
CXAudio2::CXAudio2(void)
    : bufferCount(0)
    , partialOffset(0)
    , writeOffset(0)
{
    pXAudio2 = NULL;
    pSourceVoice = NULL;
    pMasterVoice = NULL;

    sum_bufferSize = singleBufferBytes
        = singleBufferSamples = blockCount = 0;
    soundBuffer = NULL;
    initDone = false;
}

CXAudio2::~CXAudio2(void)
{
    DeInitXAudio2();
}

INT_PTR CALLBACK DlgXAudio2InitError(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HICON aIcn = LoadIcon(NULL, IDI_WARNING);
        SendDlgItemMessage(hDlg, IDC_STATIC_ICON, STM_SETICON, (WPARAM)aIcn, 0);
        SetDlgItemText(hDlg, IDC_SYSLINK_DX, TEXT("Unable to initialize XAudio2.\
You will not be able to hear any sound effects or music while playing.\n\n\
This is usually caused by not having a recent DirectX9 runtime installed.\n\
You can download the most recent DirectX9 runtime here:\n\n\
<a href=\"https://www.microsoft.com/en-us/download/details.aspx?id=35\">https://www.microsoft.com/en-us/download/details.aspx?id=35</a>"));

        HWND parent = GetParent(hDlg);
        RECT rcParent, rcSelf;
        GetWindowRect(parent, &rcParent);
        GetWindowRect(hDlg, &rcSelf);

        SetWindowPos(hDlg,
            HWND_TOP,
            rcParent.left + ((rcParent.right - rcParent.left) - (rcSelf.right - rcSelf.left)) / 2,
            rcParent.top + ((rcParent.bottom - rcParent.top) - (rcSelf.bottom - rcSelf.top)) / 2,
            0, 0,
            SWP_NOSIZE);
    }
    return true;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (HIWORD(wParam) == BN_CLICKED) {
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
        }
        break;

    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_CLICK:
        case NM_RETURN:
        {
            PNMLINK pNMLink = (PNMLINK)lParam;
            LITEM   item = pNMLink->item;
            ShellExecute(NULL, L"open", item.szUrl, NULL, NULL, SW_SHOW);
            break;
        }
        }
    }

    return FALSE;
}

/*  CXAudio2::InitXAudio2
initializes the XAudio2 object
-----
returns true if successful, false otherwise
*/
bool CXAudio2::InitXAudio2(void)
{
    if (initDone)
        return true;

    DeInitXAudio2();

    HRESULT hr;
    if (FAILED(hr = XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        MessageBox(NULL, TEXT("Unable to create XAudio2 object."), TEXT("XAudio2 Error"), MB_OK);
        DialogBox(GUI.hInstance, MAKEINTRESOURCE(IDD_DIALOG_XAUDIO2_INIT_ERROR), GUI.hWnd, DlgXAudio2InitError);
        return false;
    }

    initDone = true;
    pXAudio2->RegisterForCallbacks(this);
    return true;
}

/*  CXAudio2::InitVoices
initializes the voice objects with the current audio settings
-----
returns true if successful, false otherwise
*/
bool CXAudio2::InitVoices(void)
{
    HRESULT hr;

    // XAudio2.9 no longer supports device selection; always use default device.
    if (FAILED(hr = pXAudio2->CreateMasteringVoice(
        &pMasterVoice,
        2,
        Settings.SoundPlaybackRate,
        0,
        nullptr,
        nullptr)))
    {
        MessageBox(NULL, TEXT("Unable to create mastering voice."), TEXT("XAudio2 Error"), MB_OK);
        return false;
    }

    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = Settings.SoundPlaybackRate;
    wfx.nBlockAlign = 2 * 2;
    wfx.wBitsPerSample = 16;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    if (FAILED(hr = pXAudio2->CreateSourceVoice(
        &pSourceVoice,
        &wfx,
        XAUDIO2_VOICE_NOSRC,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        this,
        NULL,
        NULL)))
    {
        MessageBox(NULL, TEXT("Unable to create source voice."), TEXT("XAudio2 Error"), MB_OK);
        return false;
    }

    return true;
}

/*  CXAudio2::DeInitXAudio2
deinitializes all objects
*/
void CXAudio2::DeInitXAudio2(void)
{
    initDone = false;
    DeInitVoices();
    if (pXAudio2) {
        pXAudio2->Release();
        pXAudio2 = NULL;
    }
}

/*  CXAudio2::DeInitVoices
deinitializes the voice objects and buffers
*/
void CXAudio2::DeInitVoices(void)
{
    if (pSourceVoice) {
        StopPlayback();
        pSourceVoice->DestroyVoice();
        pSourceVoice = NULL;
    }
    if (pMasterVoice) {
        pMasterVoice->DestroyVoice();
        pMasterVoice = NULL;
    }
    if (soundBuffer) {
        delete[] soundBuffer;
        soundBuffer = NULL;
    }
}

/*  CXAudio2::OnBufferEnd */
void CXAudio2::OnBufferEnd(void* pBufferContext)
{
    InterlockedDecrement(&bufferCount);
    SetEvent(GUI.SoundSyncEvent);
}

/*  CXAudio2::PushBuffer */
void CXAudio2::PushBuffer(UINT32 AudioBytes, BYTE* pAudioData, void* pContext)
{
    XAUDIO2_BUFFER xa2buffer = { 0 };
    xa2buffer.AudioBytes = AudioBytes;
    xa2buffer.pAudioData = pAudioData;
    xa2buffer.pContext = pContext;
    InterlockedIncrement(&bufferCount);
    pSourceVoice->SubmitSourceBuffer(&xa2buffer);
}

/*  CXAudio2::SetupSound */
bool CXAudio2::SetupSound()
{
    if (!initDone)
        return false;

    DeInitVoices();

    blockCount = 8;
    UINT32 blockTime = GUI.SoundBufferSize / blockCount;

    singleBufferSamples = (Settings.SoundPlaybackRate * blockTime) / 1000;
    singleBufferSamples *= 2;
    singleBufferBytes = singleBufferSamples * 2;
    sum_bufferSize = singleBufferBytes * blockCount;

    if (InitVoices())
    {
        soundBuffer = new uint8[sum_bufferSize];
        writeOffset = 0;
        partialOffset = 0;
    }
    else {
        DeInitVoices();
        return false;
    }

    bufferCount = 0;

    BeginPlayback();

    return true;
}

void CXAudio2::SetVolume(double volume)
{
    pSourceVoice->SetVolume(volume);
}

void CXAudio2::BeginPlayback()
{
    pSourceVoice->Start(0);
}

void CXAudio2::StopPlayback()
{
    pSourceVoice->Stop(0);
}

int CXAudio2::GetAvailableBytes()
{
    return ((blockCount - bufferCount) * singleBufferBytes) - partialOffset;
}

/*  CXAudio2::ProcessSound */
void CXAudio2::ProcessSound()
{
    int freeBytes = GetAvailableBytes();

    if (Settings.DynamicRateControl)
    {
        S9xUpdateDynamicRate(freeBytes, sum_bufferSize);
    }

    UINT32 availableSamples = S9xGetSampleCount();

    if (Settings.DynamicRateControl && !Settings.SoundSync)
    {
        if (availableSamples > (freeBytes >> 1))
        {
            S9xClearSamples();
            return;
        }
    }

    if (!initDone)
        return;

    if (Settings.SoundSync && !Settings.TurboMode && !Settings.Mute)
    {
        while ((freeBytes >> 1) < availableSamples)
        {
            ResetEvent(GUI.SoundSyncEvent);
            if (!GUI.AllowSoundSync || WaitForSingleObject(GUI.SoundSyncEvent, 1000) != WAIT_OBJECT_0)
            {
                S9xClearSamples();
                return;
            }
            freeBytes = GetAvailableBytes();
        }
    }

    if (partialOffset != 0) {
        assert(partialOffset < singleBufferBytes);
        assert(bufferCount < blockCount);
        BYTE* offsetBuffer = soundBuffer + writeOffset + partialOffset;
        UINT32 samplesleftinblock = (singleBufferBytes - partialOffset) >> 1;

        if (availableSamples < samplesleftinblock)
        {
            S9xMixSamples(offsetBuffer, availableSamples);
            partialOffset += availableSamples << 1;
            assert(partialOffset < singleBufferBytes);
            availableSamples = 0;
        }
        else
        {
            S9xMixSamples(offsetBuffer, samplesleftinblock);
            partialOffset = 0;
            availableSamples -= samplesleftinblock;
            PushBuffer(singleBufferBytes, soundBuffer + writeOffset, NULL);
            writeOffset += singleBufferBytes;
            writeOffset %= sum_bufferSize;
        }
    }

    while (availableSamples >= singleBufferSamples && bufferCount < blockCount) {
        BYTE* curBuffer = soundBuffer + writeOffset;
        S9xMixSamples(curBuffer, singleBufferSamples);
        PushBuffer(singleBufferBytes, curBuffer, NULL);
        writeOffset += singleBufferBytes;
        writeOffset %= sum_bufferSize;
        availableSamples -= singleBufferSamples;
    }

    if (availableSamples > 0 && availableSamples < singleBufferSamples && bufferCount < blockCount) {
        S9xMixSamples(soundBuffer + writeOffset, availableSamples);
        partialOffset = availableSamples << 1;
        assert(partialOffset < singleBufferBytes);
    }
}

