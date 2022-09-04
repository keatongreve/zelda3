#include "pch.h"

#include "win_volume_control.h"

#include <initguid.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

bool SetApplicationVolume(int volume_level) {
    HRESULT hr = S_OK;
    CoInitialize(NULL);

    IMMDeviceEnumerator* pDeviceEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioSessionManager* pAudioSessionManager = NULL;
    IAudioSessionControl* pAudioSession = NULL;
    ISimpleAudioVolume* pSimpleAudioVolume = NULL;

    // Get the enumerator for the audio endpoint devices.
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDeviceEnumerator)
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the default audio endpoint that the SAR will use.
    hr = pDeviceEnumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,   // The SAR uses 'eConsole' by default.
        &pDevice
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the session manager for this device.
    hr = pDevice->Activate(
        __uuidof(IAudioSessionManager),
        CLSCTX_INPROC_SERVER,
        NULL,
        (void**)&pAudioSessionManager
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the audio session.
    hr = pAudioSessionManager->GetAudioSessionControl(
        &GUID_NULL,     // Get the default audio session.
        FALSE,          // The session is not cross-process.
        &pAudioSession
    );


    if (FAILED(hr))
    {
        goto done;
    }

    hr = pAudioSessionManager->GetSimpleAudioVolume(
        &GUID_NULL, 0, &pSimpleAudioVolume
    );

    hr = pSimpleAudioVolume->SetMasterVolume((float)(volume_level / 100.0), NULL);

done:
    if (pDeviceEnumerator) pDeviceEnumerator->Release();
    if (pDevice) pDevice->Release();
    if (pAudioSessionManager) pAudioSessionManager->Release();
    if (pAudioSession) pAudioSession->Release();
    if (pSimpleAudioVolume) pSimpleAudioVolume->Release();
    return SUCCEEDED(hr);
}

bool SetApplicationMuted(bool muted) {
    HRESULT hr = S_OK;
    CoInitialize(NULL);

    IMMDeviceEnumerator* pDeviceEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioSessionManager* pAudioSessionManager = NULL;
    IAudioSessionControl* pAudioSession = NULL;
    ISimpleAudioVolume* pSimpleAudioVolume = NULL;

    // Get the enumerator for the audio endpoint devices.
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDeviceEnumerator)
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the default audio endpoint that the SAR will use.
    hr = pDeviceEnumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,   // The SAR uses 'eConsole' by default.
        &pDevice
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the session manager for this device.
    hr = pDevice->Activate(
        __uuidof(IAudioSessionManager),
        CLSCTX_INPROC_SERVER,
        NULL,
        (void**)&pAudioSessionManager
    );

    if (FAILED(hr))
    {
        goto done;
    }

    // Get the audio session.
    hr = pAudioSessionManager->GetAudioSessionControl(
        &GUID_NULL,     // Get the default audio session.
        FALSE,          // The session is not cross-process.
        &pAudioSession
    );


    if (FAILED(hr))
    {
        goto done;
    }

    hr = pAudioSessionManager->GetSimpleAudioVolume(
        &GUID_NULL, 0, &pSimpleAudioVolume
    );

    hr = pSimpleAudioVolume->SetMute(muted, NULL);

done:
    if (pDeviceEnumerator) pDeviceEnumerator->Release();
    if (pDevice) pDevice->Release();
    if (pAudioSessionManager) pAudioSessionManager->Release();
    if (pAudioSession) pAudioSession->Release();
    if (pSimpleAudioVolume) pSimpleAudioVolume->Release();
    return SUCCEEDED(hr);
}