//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// speech_c_recognizer_factory.h: Public API declarations for RecognizerFactory related C methods
//

#pragma once
#include <spxdebug.h>
#include <speechapi_c_common.h>

SPXAPI RecognizerFactory_CreateSpeechRecognizer_With_Defaults(SPXRECOHANDLE* phreco);
SPXAPI RecognizerFactory_CreateSpeechRecognizer_With_Language(SPXRECOHANDLE* phreco, const wchar_t* pszLanguage);
SPXAPI RecognizerFactory_CreateSpeechRecognizer_With_FileInput(SPXRECOHANDLE* phreco, const wchar_t* pszFileName);
SPXAPI RecognizerFactory_CreateSpeechRecognizer(SPXRECOHANDLE* phreco, const wchar_t* pszLanguage, const wchar_t* pszFileName);

SPXAPI RecognizerFactory_CreateIntentRecognizer_With_Defaults(SPXRECOHANDLE* phreco);
SPXAPI RecognizerFactory_CreateIntentRecognizer_With_Language(SPXRECOHANDLE* phreco, const wchar_t* pszLanguage);
SPXAPI RecognizerFactory_CreateIntentRecognizer_With_FileInput(SPXRECOHANDLE* phreco, const wchar_t* pszFileName);
SPXAPI RecognizerFactory_CreateIntentRecognizer(SPXRECOHANDLE* phreco, const wchar_t* pszLanguage, const wchar_t* pszFileName);

SPXAPI RecognizerFactory_SetParameter_String(const wchar_t* name, const wchar_t* value);
SPXAPI RecognizerFactory_GetParameter_String(const wchar_t* name, wchar_t* value, uint32_t cchValue, const wchar_t* defaultValue);
SPXAPI_(bool) RecognizerFactory_HasParameter_String(const wchar_t* name);

SPXAPI RecognizerFactory_SetParameter_Int32(const wchar_t* name, int32_t value);
SPXAPI RecognizerFactory_GetParameter_Int32(const wchar_t* name, int32_t* pvalue, int32_t defaultValue);
SPXAPI_(bool) RecognizerFactory_HasParameter_Int32(const wchar_t* name);

SPXAPI RecognizerFactory_SetParameter_Bool(const wchar_t* name, bool value);
SPXAPI RecognizerFactory_GetParameter_Bool(const wchar_t* name, bool* pvalue, bool defaultValue);
SPXAPI_(bool) RecognizerFactory_HasParameter_Bool(const wchar_t* name);
