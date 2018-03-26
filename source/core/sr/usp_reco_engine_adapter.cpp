//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// usp_reco_engine_adapter.cpp: Implementation definitions for CSpxUspRecoEngineAdapter C++ class
//

#include "stdafx.h"
#include "usp_reco_engine_adapter.h"
#include "handle_table.h"
#include "file_utils.h"
#include <inttypes.h>
#include <cstring>
#include "service_helpers.h"

#define INVALID_USP_HANDLE ((UspHandle)-1)


namespace CARBON_IMPL_NAMESPACE() {

int CSpxUspRecoEngineAdapter::m_instanceCounter = 0;

CSpxUspRecoEngineAdapter::CSpxUspRecoEngineAdapter() :
    m_handle(INVALID_USP_HANDLE),
    m_servicePreferedBufferSize(0),
    m_bytesInBuffer(0),
    m_ptrIntoBuffer(nullptr),
    m_bytesLeftInBuffer(0)
{
    m_instanceCounter++;
    InitCallbacks(&m_callbacks);
}

void CSpxUspRecoEngineAdapter::Init()
{
    SPX_IFTRUE_THROW_HR(GetSite() == nullptr, SPXERR_UNINITIALIZED);
    SPX_IFTRUE_THROW_HR(IsUspHandleValid(m_handle), SPXERR_ALREADY_INITIALIZED);

    UspInitialize(&m_handle, &m_callbacks, static_cast<void*>(this));
    DumpFileInit();
}

void CSpxUspRecoEngineAdapter::Term()
{
    SPX_DBG_TRACE_SCOPE("Terminating CSpxUspRecoEngineAdapter...", "Terminating CSpxUspRecoEngineAdapter... Done!");
    
    UspShutdown(m_handle);
    m_handle = INVALID_USP_HANDLE;

    DumpFileClose();
}

void CSpxUspRecoEngineAdapter::SetFormat(WAVEFORMATEX* pformat)
{
    SPX_IFTRUE_THROW_HR(!IsUspHandleValid(m_handle), SPXERR_UNINITIALIZED);

    if (pformat != nullptr)
    {
        UspWriteFormat(m_handle, pformat);
        m_servicePreferedBufferSize = (size_t)pformat->nSamplesPerSec * pformat->nBlockAlign * m_servicePreferedMilliseconds / 1000;
    }
    else
    {
        UspWrite_Flush(m_handle);
    }
}

void CSpxUspRecoEngineAdapter::ProcessAudio(AudioData_Type data, uint32_t size)
{
    UspWrite(m_handle, data.get(), size);
}

bool CSpxUspRecoEngineAdapter::IsUspHandleValid(UspHandle handle)
{
    return handle != INVALID_USP_HANDLE;
}

void CSpxUspRecoEngineAdapter::UspInitialize(UspHandle* handle, UspCallbacks *callbacks, void* callbackContext)
{
    SPX_DBG_TRACE_VERBOSE("%s(0x%x)", __FUNCTION__, handle);

    // Before we initialize the USP, we need to know what Endpoint type we're going to use...
    UspEndpointType type = GetUspEndpointType();

    // Initialize the USP (by URL or by type)
    SPX_IFFAILED_THROW_HR(type == USP_ENDPOINT_UNKNOWN
        ? ::UspInitByUrl(GetUspCustomEndpoint().c_str(), callbacks, callbackContext, handle)
        : ::UspInit(type, GetUspRecoMode(), callbacks, callbackContext, handle));

    // Set the auth data, if it was provided
    auto authType = USP_AUTHENTICATION_UNKNOWN;
    auto authData = GetUspAuthenticationData(&authType);
    if (authType != USP_AUTHENTICATION_UNKNOWN)
    {
        SPX_IFFAILED_THROW_HR(::UspSetAuthentication(*handle, authType, authData.c_str()));
    }

    // Set the language, if it was provided...
    auto language = GetUspLanguage();
    if (!language.empty())
    {
        SPX_IFFAILED_THROW_HR(::UspSetLanguage(*handle, language.c_str()));
    }

    // Set the model id if it was provided...
    auto id = GetUspModelId();
    if (!id.empty())
    {
        SPX_IFFAILED_THROW_HR(::UspSetModelId(*handle, id.c_str()));
    }

    // Finally ... Connect to the service
    SPX_IFFAILED_THROW_HR(::UspConnect(*handle));
}

UspEndpointType CSpxUspRecoEngineAdapter::GetUspEndpointType()
{
    // Get the named property service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the properties that indicates what endpoint type to use...
    auto endpoint = properties->GetStringValue(LR"(SPEECH-Endpoint)");
    auto subscriptionKey = properties->GetStringValue(LR"(SPEECH-SubscriptionKey)");
    auto customSpeechModelId = properties->GetStringValue(LR"(CUSTOMSPEECH-modelId)");

    UspEndpointType type;
    if (!customSpeechModelId.empty())                           // Use the Custom Recognition Intelligent Service
    {
        type = USP_ENDPOINT_CRIS;
    }
    else if (PAL::wcsicmp(endpoint.c_str(), L"CORTANA") == 0)   // Use the CORTANA SDK endpoint
    {
        type = USP_ENDPOINT_CDSDK;
    }
    else if (!endpoint.empty())                                 // Use the SPECIFIED endpoint
    {
        type = USP_ENDPOINT_UNKNOWN;
    }
    else                                                        // Otherwise ... Use the default SPEECH endpoints
    {
        type = USP_ENDPOINT_BING_SPEECH;
    }

    // We're done!
    return type;
}

std::string CSpxUspRecoEngineAdapter::GetUspCustomEndpoint()
{
    // We should only be called here if we're using a custom endpoint (aka UNKNOWN, see GetUspEndpointType())
    SPX_DBG_ASSERT(GetUspEndpointType() == USP_ENDPOINT_UNKNOWN);

    // Get the named properties service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the property that indicates what endpoint to use...
    auto value = properties->GetStringValue(LR"(SPEECH-Endpoint)");
    return PAL::ToString(value);
}

UspRecognitionMode CSpxUspRecoEngineAdapter::GetUspRecoMode()
{
    // We should only be called here if we're NOT using a custom endpoint (aka UNKNOWN, see GetUspEndpointType())
    SPX_DBG_ASSERT(GetUspEndpointType() != USP_ENDPOINT_UNKNOWN);

    // Get the named properties service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the property that indicates what reco mode to use...
    auto value = properties->GetStringValue(LR"(SPEECH-RecoMode)");

    // Convert that value to the appropriate UspRecognitionMode...
    UspRecognitionMode mode;
    if (value.empty() || PAL::wcsicmp(value.c_str(), L"INTERACTIVE") == 0)
    {
        mode = USP_RECO_MODE_INTERACTIVE;
    }
    else if (PAL::wcsicmp(value.c_str(), L"CONVERSATION") == 0)
    {
        mode = USP_RECO_MODE_CONVERSATION;
    }
    else if (PAL::wcsicmp(value.c_str(), L"DICTATION") == 0)
    {
        mode = USP_RECO_MODE_DICTATION;
    }
    else
    {
        mode = USP_RECO_MODE_UNKNOWN;
    }

    // We're done!
    return mode;
}

std::string CSpxUspRecoEngineAdapter::GetUspLanguage()
{
    // Get the named properties service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the property that indicates what language to use...
    auto value = properties->GetStringValue(LR"(SPEECH-RecoLanguage)");
    return PAL::ToString(value);
}

std::string CSpxUspRecoEngineAdapter::GetUspModelId()
{
    // Get the named properties service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the property that indicates what model to use...
    auto value = properties->GetStringValue(LR"(CUSTOMSPEECH-ModelId)");
    return PAL::ToString(value);
}

std::string CSpxUspRecoEngineAdapter::GetUspAuthenticationData(UspAuthenticationType* pauthType)
{
    // Get the named properties service...
    auto properties = SpxQueryService<ISpxNamedProperties>(GetSite());
    SPX_IFTRUE_THROW_HR(properties == nullptr, SPXERR_UNEXPECTED_USP_SITE_FAILURE);

    // Get the properties that indicates what endpoint to use...
    auto uspSubscriptionKey = properties->GetStringValue(L"SPEECH-SubscriptionKey");
    auto uspAuthToken = properties->GetStringValue(L"SPEECH-AuthToken");
    auto uspRpsToken = properties->GetStringValue(L"SPEECH-RpsToken");

    // Use those properties to determine which authentication type to use
    auto authType = USP_AUTHENTICATION_UNKNOWN;
    std::wstring* pauthData = nullptr;

    if (!uspSubscriptionKey.empty())
    {
        authType = USP_AUTHENTICATION_SUBSCRIPTION_KEY;
        pauthData = &uspSubscriptionKey;
    }
    else if (!uspAuthToken.empty())
    {
        authType = USP_AUTHENTICATION_AUTHORIZATION_TOKEN;
        pauthData = &uspAuthToken;
    }
    else if (!uspRpsToken.empty())
    {
        authType = USP_AUTHENTICATION_SEARCH_DELEGATION_RPS_TOKEN;
        pauthData = &uspRpsToken;
    }

    // And ... We're done!
    *pauthType = authType;
    return pauthData == nullptr ? std::string("") : PAL::ToString(*pauthData);
}

void CSpxUspRecoEngineAdapter::UspWriteFormat(UspHandle handle, WAVEFORMATEX* pformat)
{
    UNUSED(handle);
    static const uint16_t cbTag = 4;
    static const uint16_t cbChunkType = 4;
    static const uint16_t cbChunkSize = 4;

    uint32_t cbFormatChunk = sizeof(WAVEFORMAT) + pformat->cbSize;
    uint32_t cbRiffChunk = 0;       // NOTE: This isn't technically accurate for a RIFF/WAV file, but it's fine for Truman/Newman/Skyman
    uint32_t cbDataChunk = 0;       // NOTE: Similarly, this isn't technically correct for the 'data' chunk, but it's fine for Truman/Newman/Skyman

    size_t cbHeader =
        cbTag + cbChunkSize +       // 'RIFF' #size_of_RIFF#
        cbChunkType +               // 'WAVE'
        cbChunkType + cbChunkSize + // 'fmt ' #size_fmt#
        cbFormatChunk +             // actual format
        cbChunkType + cbChunkSize;  // 'data' #size_of_data#

    // Allocate the buffer, and create a ptr we'll use to advance thru the buffer as we're writing stuff into it
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[cbHeader]);
    auto ptr = buffer.get();

    // The 'RIFF' header (consists of 'RIFF' followed by size of payload that follows)
    ptr = FormatBufferWriteChars(ptr, "RIFF", cbTag);
    ptr = FormatBufferWriteNumber(ptr, cbRiffChunk);

    // The 'WAVE' chunk header
    ptr = FormatBufferWriteChars(ptr, "WAVE", cbChunkType);

    // The 'fmt ' chunk (consists of 'fmt ' followed by the total size of the WAVEFORMAT(EX)(TENSIBLE), followed by the WAVEFORMAT(EX)(TENSIBLE)
    ptr = FormatBufferWriteChars(ptr, "fmt ", cbChunkType);
    ptr = FormatBufferWriteNumber(ptr, cbFormatChunk);
    ptr = FormatBufferWriteBytes(ptr, (uint8_t*)pformat, cbFormatChunk);

    // The 'data' chunk is next
    ptr = FormatBufferWriteChars(ptr, "data", cbChunkType);
    ptr = FormatBufferWriteNumber(ptr, cbDataChunk);

    // Now that we've prepared the header/buffer, send it along to Truman/Newman/Skyman via UspWrite
    SPX_DBG_ASSERT(cbHeader == size_t(ptr - buffer.get()));
    UspWrite(m_handle, buffer.get(), cbHeader);
}

void CSpxUspRecoEngineAdapter::UspWrite(UspHandle handle, const uint8_t* buffer, size_t byteToWrite)
{
    SPX_DBG_TRACE_VERBOSE_IF(byteToWrite == 0, "%s(..., %d)", __FUNCTION__, byteToWrite);

    auto fn = !m_fUseBufferedImplementation || m_servicePreferedBufferSize == 0
        ? &CSpxUspRecoEngineAdapter::UspWrite_Actual
        : &CSpxUspRecoEngineAdapter::UspWrite_Buffered;

    (this->*fn)(handle, buffer, byteToWrite);
}

void CSpxUspRecoEngineAdapter::UspWrite_Actual(UspHandle handle, const uint8_t* buffer, size_t byteToWrite)
{
    SPX_INIT_HR(hr);
    SPX_DBG_TRACE_VERBOSE("%s(..., %d)", __FUNCTION__, byteToWrite);

    hr = ::UspWriteAudio(handle, buffer, byteToWrite, NULL);
    hr = (byteToWrite == 0 && hr == USP_WRITE_AUDIO_ERROR) ? SPX_NOERROR : hr; // ::UspWriteAudio currently returns USP_WRITE_AUDIO_ERROR on zero bytes, but there's no other way to flush buffer...

    DumpFileWrite(buffer, byteToWrite);

    SPX_IFFAILED_THROW_HR(hr);
}

void CSpxUspRecoEngineAdapter::UspWrite_Buffered(UspHandle handle, const uint8_t* buffer, size_t bytesToWrite)
{
    bool flushBuffer = bytesToWrite == 0;

    if (m_buffer.get() == nullptr)
    {
        m_buffer = SpxAllocSharedUint8Buffer(m_servicePreferedBufferSize);
        m_bytesInBuffer = m_servicePreferedBufferSize;

        m_ptrIntoBuffer = m_buffer.get();
        m_bytesLeftInBuffer = m_bytesInBuffer;
    }

    for (;;)
    {
        if (flushBuffer || (m_bytesInBuffer > 0 && m_bytesLeftInBuffer == 0))
        {
            auto bytesToFlush = m_bytesInBuffer - m_bytesLeftInBuffer;
            UspWrite_Actual(handle, m_buffer.get(), bytesToFlush);

            m_bytesLeftInBuffer = m_bytesInBuffer;
            m_ptrIntoBuffer = m_buffer.get();
        }

        if (flushBuffer)
        {
            m_buffer = nullptr;
            m_bytesInBuffer = 0;
            m_ptrIntoBuffer = nullptr;
            m_bytesLeftInBuffer = 0;
        }

        if (bytesToWrite == 0)
        {
            break;
        }

        size_t bytesThisLoop = std::min(bytesToWrite, m_bytesLeftInBuffer);
        std::memcpy(m_ptrIntoBuffer, buffer, bytesThisLoop);

        m_ptrIntoBuffer += bytesThisLoop;
        m_bytesLeftInBuffer -= bytesThisLoop;
        bytesToWrite -= bytesThisLoop;
    }
}

void CSpxUspRecoEngineAdapter::UspWrite_Flush(UspHandle handle)
{
    UspWrite_Buffered(handle, nullptr, 0);
}

void CSpxUspRecoEngineAdapter::UspShutdown(UspHandle handle)
{
    SPX_DBG_TRACE_VERBOSE("%s(0x%x)", __FUNCTION__, handle);

    SPX_IFFAILED_THROW_HR(::UspClose(handle));
}

void CSpxUspRecoEngineAdapter::InitCallbacks(UspCallbacks* pcallbacks)
{
    pcallbacks->size = sizeof(UspCallbacks);
    pcallbacks->version = (uint16_t)USP_CALLBACK_VERSION;

    pcallbacks->onSpeechStartDetected = [](UspHandle handle, void* context, UspMsgSpeechStartDetected *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Speech.StartDetected message. Speech starts at offset %" PRIu64 " (100ns).\n", message->offset);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnSpeechStartDetected(message);
    };

    pcallbacks->onSpeechEndDetected = [](UspHandle handle, void* context, UspMsgSpeechEndDetected *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Speech.EndDetected message. Speech ends at offset %" PRIu64 " (100ns)\n", message->offset);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnSpeechEndDetected(message);
    };

    pcallbacks->onSpeechHypothesis = [](UspHandle handle, void* context, UspMsgSpeechHypothesis *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Speech.Hypothesis message. Starts at offset %" PRIu64 ", with duration %" PRIu64 " (100ns). Text: %ls\n", message->offset, message->duration, message->text);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnSpeechHypothesis(message);
    };

    pcallbacks->onSpeechFragment = [](UspHandle handle, void* context, UspMsgSpeechFragment *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Speech.Fragment message. Starts at offset %" PRIu64 ", with duration %" PRIu64 " (100ns). Text: %ls\n", message->offset, message->duration, message->text);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnSpeechFragment(message);
    };

    pcallbacks->onSpeechPhrase = [](UspHandle handle, void* context, UspMsgSpeechPhrase *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Speech.Phrase message. Status: %d, Text: %ls, starts at %" PRIu64 ", with duration %" PRIu64 " (100ns).\n", message->recognitionStatus, message->displayText, message->offset, message->duration);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnSpeechPhrase(message);
    };

    pcallbacks->onTurnStart = [](UspHandle handle, void* context, UspMsgTurnStart *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Turn.Start message. Context.ServiceTag: %ls\n", message->contextServiceTag);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnTurnStart(message);
    };

    pcallbacks->onTurnEnd = [](UspHandle handle, void* context, UspMsgTurnEnd *message) {
        SPX_DBG_TRACE_VERBOSE("Response: Turn.End message.\n");
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnTurnEnd(message);
    };

    pcallbacks->OnError = [](UspHandle handle, void* context, const UspError* error) {
        SPX_DBG_TRACE_VERBOSE("Response: On Error: 0x%x (%s).\n", error->errorCode, error->description);
        CSpxUspRecoEngineAdapter::From(handle, context)->UspOnError(error);
    };
}

void CSpxUspRecoEngineAdapter::UspOnSpeechStartDetected(UspMsgSpeechStartDetected *message)
{
    SPX_DBG_ASSERT(GetSite());
    GetSite()->SpeechStartDetected(this, message->offset);
}

void CSpxUspRecoEngineAdapter::UspOnSpeechEndDetected(UspMsgSpeechEndDetected *message)
{
    SPX_DBG_ASSERT(GetSite());
    GetSite()->SpeechEndDetected(this, message->offset);
}

void CSpxUspRecoEngineAdapter::UspOnSpeechHypothesis(UspMsgSpeechHypothesis *message)
{
    SPX_DBG_ASSERT(GetSite());

    // TODO: RobCh: Do something with the other fields in UspMsgSpeechHypothesis
    auto factory = SpxQueryService<ISpxRecoResultFactory>(GetSite());
    auto result = factory->CreateIntermediateResult(nullptr, message->text);

    GetSite()->IntermediateRecoResult(this, message->offset, result);
}

void CSpxUspRecoEngineAdapter::UspOnSpeechFragment(UspMsgSpeechFragment *message)
{
    SPX_DBG_ASSERT(GetSite());

    // TODO: RobCh: Do something with the other fields in UspMsgSpeechHypothesis
    // TODO: Rob: do we want to treate speech.fragment message different than speech.hypothesis message at this level?

    auto factory = SpxQueryService<ISpxRecoResultFactory>(GetSite());
    auto result = factory->CreateIntermediateResult(nullptr, message->text);
    GetSite()->IntermediateRecoResult(this, message->offset, result);
}

void CSpxUspRecoEngineAdapter::UspOnSpeechPhrase(UspMsgSpeechPhrase *message)
{
    SPX_DBG_ASSERT(GetSite());

    // TODO: RobCh: Do something with the other fields in UspMsgSpeechPhrase
    auto factory = SpxQueryService<ISpxRecoResultFactory>(GetSite());
    auto result = factory->CreateFinalResult(nullptr, message->displayText);

    GetSite()->FinalRecoResult(this, message->offset, result);
}

void CSpxUspRecoEngineAdapter::UspOnTurnStart(UspMsgTurnStart *message)
{
    SPX_DBG_ASSERT(GetSite());
    GetSite()->AdditionalMessage(this, 0, AdditionalMessagePayloadFrom(message));
}

void CSpxUspRecoEngineAdapter::UspOnTurnEnd(UspMsgTurnEnd *message)
{
    UNUSED(message);
    SPX_DBG_ASSERT(GetSite());
    GetSite()->DoneProcessingAudio(this);
}

void CSpxUspRecoEngineAdapter::UspOnError(const UspError* error)
{
    SPX_DBG_ASSERT(GetSite());
    GetSite()->Error(this, ErrorPayloadFrom(error));
}

void CSpxUspRecoEngineAdapter::DumpFileInit()
{
    // TODO: is this audio dumping some debugging cruft, is it still needed?
    // on Windows, this fopen returns null if the same file was already opened,
    // as a result writing segfaults. Adding a counter the filename, so that
    // each recognizer will get its own dumpfile.
    std::string filename = "uspaudiodump_" + std::to_string(m_instanceCounter) + ".wav";
    PAL::fopen_s(&m_hfile, filename.c_str(), "wb");
}

void CSpxUspRecoEngineAdapter::DumpFileWrite(const uint8_t* buffer, size_t bytesToWrite)
{
    fwrite(buffer, 1, bytesToWrite, m_hfile);
}

void CSpxUspRecoEngineAdapter::DumpFileClose()
{
    fclose(m_hfile);
    m_hfile = nullptr;
}

uint8_t* CSpxUspRecoEngineAdapter::FormatBufferWriteBytes(uint8_t* buffer, const uint8_t* source, size_t bytes)
{
    std::memcpy(buffer, source, bytes);
    return buffer + bytes;
}

uint8_t* CSpxUspRecoEngineAdapter::FormatBufferWriteNumber(uint8_t* buffer, uint32_t number)
{
    std::memcpy(buffer, &number, sizeof(number));
    return buffer + sizeof(number);
}

uint8_t* CSpxUspRecoEngineAdapter::FormatBufferWriteChars(uint8_t* buffer, const char* psz, size_t cch)
{
    std::memcpy(buffer, psz, cch);
    return buffer + cch;
}


} // CARBON_IMPL_NAMESPACE
