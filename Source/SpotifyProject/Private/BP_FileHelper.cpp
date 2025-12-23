#include "BP_FileHelper.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformHttp.h"

bool UBP_FileHelper::ConvertWavToJson(
    const FString& FilePath,
    EAudioJsonFormat AudioFormat,
    EBase64EncodingType Base64Type,
    bool bIncludeWavHeader,
    int32 TargetSampleRate,
    int32 TargetNumChannels,
    int32 TargetBitsPerSample,
    FString& OutJsonPayload
)
{
    OutJsonPayload.Empty();

    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("WAV file not found: %s"), *FilePath);
        return false;
    }

    TArray<uint8> WavBytes;
    if (!FFileHelper::LoadFileToArray(WavBytes, *FilePath))
    {
        return false;
    }

    TArray<uint8> PCMBytes;
    int32 SampleRate = 0;
    int32 NumChannels = 0;
    int32 BitsPerSample = 0;

    if (!ParseWavData(WavBytes, PCMBytes, NumChannels, SampleRate, BitsPerSample))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse WAV"));
        return false;
    }

    if (SampleRate != TargetSampleRate ||
        NumChannels != TargetNumChannels ||
        BitsPerSample != TargetBitsPerSample)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("WAV format mismatch (SR=%d CH=%d BPS=%d)"),
            SampleRate,
            NumChannels,
            BitsPerSample
        );
        return false;
    }

    // Optional WAV header (debug / playback only)
    if (bIncludeWavHeader)
    {
        AddWavHeader(PCMBytes, SampleRate, NumChannels, BitsPerSample);
    }

    const FString EncodedAudio = EncodeBase64(PCMBytes, Base64Type);

    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(&OutJsonPayload);

    Writer->WriteObjectStart();

    switch (AudioFormat)
    {
    case EAudioJsonFormat::Generic:
    {
        Writer->WriteValue(TEXT("sampleRate"), SampleRate);
        Writer->WriteValue(TEXT("channels"), NumChannels);
        Writer->WriteValue(TEXT("bitsPerSample"), BitsPerSample);
        Writer->WriteValue(TEXT("audio"), EncodedAudio);
        break;
    }

    case EAudioJsonFormat::GoogleSpeech:
    {
        Writer->WriteObjectStart(TEXT("config"));
        Writer->WriteValue(TEXT("encoding"), TEXT("LINEAR16"));
        Writer->WriteValue(TEXT("sampleRateHertz"), SampleRate);
        Writer->WriteValue(TEXT("languageCode"), TEXT("en-US"));
        Writer->WriteValue(TEXT("audioChannelCount"), NumChannels);
        Writer->WriteValue(TEXT("enableAutomaticPunctuation"), true);
        Writer->WriteObjectEnd();

        Writer->WriteObjectStart(TEXT("audio"));
        Writer->WriteValue(TEXT("content"), EncodedAudio);
        Writer->WriteObjectEnd();
        break;
    }

    case EAudioJsonFormat::Whisper:
    {
        Writer->WriteValue(TEXT("audio"), EncodedAudio);
        Writer->WriteValue(TEXT("format"), TEXT("pcm_s16le"));
        Writer->WriteValue(TEXT("sample_rate"), SampleRate);
        Writer->WriteValue(TEXT("channels"), NumChannels);
        break;
    }
    }

    Writer->WriteObjectEnd();
    Writer->Close();

    return true;
}

bool UBP_FileHelper::ParseWavData(
    const TArray<uint8>& WavBytes,
    TArray<uint8>& OutPCMBytes,
    int32& OutNumChannels,
    int32& OutSampleRate,
    int32& OutBitsPerSample
)
{
    if (WavBytes.Num() < 44)
    {
        return false;
    }

    OutNumChannels = *reinterpret_cast<const uint16*>(&WavBytes[22]);
    OutSampleRate = *reinterpret_cast<const uint32*>(&WavBytes[24]);
    OutBitsPerSample = *reinterpret_cast<const uint16*>(&WavBytes[34]);

    if (OutBitsPerSample != 16)
    {
        UE_LOG(LogTemp, Error, TEXT("Only 16-bit PCM WAV supported"));
        return false;
    }

    const uint32 DataOffset = 44;
    const uint32 DataSize = *reinterpret_cast<const uint32*>(&WavBytes[40]);

    if (static_cast<uint32>(WavBytes.Num()) < DataOffset + DataSize)
    {
        return false;
    }

    OutPCMBytes.SetNum(DataSize);
    FMemory::Memcpy(
        OutPCMBytes.GetData(),
        &WavBytes[DataOffset],
        DataSize
    );

    return true;
}

void UBP_FileHelper::AddWavHeader(
    TArray<uint8>& InOutPCMData,
    int32 SampleRate,
    int32 NumChannels,
    int32 BitsPerSample
)
{
    const int32 DataSize = InOutPCMData.Num();
    const int32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
    const int32 BlockAlign = NumChannels * BitsPerSample / 8;
    const int32 FileSize = 36 + DataSize;

    TArray<uint8> WavData;
    WavData.SetNumZeroed(44 + DataSize);

    uint8* Ptr = WavData.GetData();

    FMemory::Memcpy(Ptr, "RIFF", 4);
    *(uint32*)(Ptr + 4) = FileSize;
    FMemory::Memcpy(Ptr + 8, "WAVE", 4);

    FMemory::Memcpy(Ptr + 12, "fmt ", 4);
    *(uint32*)(Ptr + 16) = 16;
    *(uint16*)(Ptr + 20) = 1;
    *(uint16*)(Ptr + 22) = NumChannels;
    *(uint32*)(Ptr + 24) = SampleRate;
    *(uint32*)(Ptr + 28) = ByteRate;
    *(uint16*)(Ptr + 32) = BlockAlign;
    *(uint16*)(Ptr + 34) = BitsPerSample;

    FMemory::Memcpy(Ptr + 36, "data", 4);
    *(uint32*)(Ptr + 40) = DataSize;

    FMemory::Memcpy(Ptr + 44, InOutPCMData.GetData(), DataSize);

    InOutPCMData = MoveTemp(WavData);
}

FString UBP_FileHelper::EncodeBase64(
    const TArray<uint8>& Bytes,
    EBase64EncodingType EncodingType
)
{
    FString Base64 = FBase64::Encode(Bytes);

    if (EncodingType == EBase64EncodingType::Base64Url)
    {
        Base64.ReplaceInline(TEXT("+"), TEXT("-"));
        Base64.ReplaceInline(TEXT("/"), TEXT("_"));
        while (Base64.EndsWith(TEXT("=")))
        {
            Base64.LeftChopInline(1);
        }
    }

    return Base64;
}
static void FindJsonValuesRecursive(
    const TSharedPtr<FJsonValue>& JsonValue,
    const FString& TargetKey,
    TArray<FString>& OutValues
)
{
    if (!JsonValue.IsValid())
    {
        return;
    }

    switch (JsonValue->Type)
    {
        case EJson::Object:
        {
            const TSharedPtr<FJsonObject>& Obj = JsonValue->AsObject();
            for (const auto& Pair : Obj->Values)
            {
                // Match key
                if (Pair.Key.Equals(TargetKey))
                {
                    if (Pair.Value->Type == EJson::String)
                    {
                        OutValues.Add(Pair.Value->AsString());
                    }
                }

                // Recurse
                FindJsonValuesRecursive(Pair.Value, TargetKey, OutValues);
            }
            break;
        }

        case EJson::Array:
        {
            for (const TSharedPtr<FJsonValue>& Element : JsonValue->AsArray())
            {
                FindJsonValuesRecursive(Element, TargetKey, OutValues);
            }
            break;
        }

        default:
            break;
    }
}

bool UBP_FileHelper::GetJsonStringValuesByKey(
    const FString& JsonString,
    const FString& PropertyName,
    TArray<FString>& OutValues
)
{
    OutValues.Empty();

    TSharedPtr<FJsonValue> RootValue;
    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid JSON input"));
        return false;
    }

    FindJsonValuesRecursive(RootValue, PropertyName, OutValues);

    return OutValues.Num() > 0;
}
FString UBP_FileHelper::UrlEncodeText(const FString& Input)
{
    return FGenericPlatformHttp::UrlEncode(Input);
}
