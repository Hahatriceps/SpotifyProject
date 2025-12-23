#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BP_FileHelper.generated.h"

/**
 * Target JSON / API format
 */
UENUM(BlueprintType)
enum class EAudioJsonFormat : uint8
{
    Generic      UMETA(DisplayName = "Generic PCM JSON"),
    GoogleSpeech UMETA(DisplayName = "Google Speech-to-Text"),
    Whisper      UMETA(DisplayName = "OpenAI Whisper")
};

/**
 * Base64 transport encoding
 */
UENUM(BlueprintType)
enum class EBase64EncodingType : uint8
{
    Standard  UMETA(DisplayName = "Standard Base64"),
    Base64Url UMETA(DisplayName = "Base64URL (RFC 4648)")
};

/**
 * Blueprint helper for WAV → JSON audio payloads
 */
UCLASS()
class SPOTIFYPROJECT_API UBP_FileHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    /**
     * Convert WAV file into a single JSON payload.
     * WAV header is stripped automatically.
     * Optionally re-added for debugging / playback.
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "File IO|Audio",
        meta = (
            AdvancedDisplay = "TargetSampleRate,TargetNumChannels,TargetBitsPerSample,bIncludeWavHeader",
            TargetSampleRate = "16000",
            TargetNumChannels = "1",
            TargetBitsPerSample = "16"
            )
    )
    static bool ConvertWavToJson(
        const FString& FilePath,
        EAudioJsonFormat AudioFormat,
        EBase64EncodingType Base64Type,
        bool bIncludeWavHeader,
        int32 TargetSampleRate,
        int32 TargetNumChannels,
        int32 TargetBitsPerSample,
        FString& OutJsonPayload
    );

    UFUNCTION(BlueprintCallable, Category = "JSON")
    static bool GetJsonStringValuesByKey(
        const FString& JsonString,
        const FString& PropertyName,
        TArray<FString>& OutValues
    );

    UFUNCTION(BlueprintCallable, Category = "HTTP")
    static FString UrlEncodeText(const FString& Input);

private:

    // Parses WAV and extracts raw PCM
    static bool ParseWavData(
        const TArray<uint8>& WavBytes,
        TArray<uint8>& OutPCMBytes,
        int32& OutNumChannels,
        int32& OutSampleRate,
        int32& OutBitsPerSample
    );

    // Prepends a 44-byte WAV header (DEBUG ONLY)
    static void AddWavHeader(
        TArray<uint8>& InOutPCMData,
        int32 SampleRate,
        int32 NumChannels,
        int32 BitsPerSample
    );

    // Encodes Base64 or Base64URL
    static FString EncodeBase64(
        const TArray<uint8>& Bytes,
        EBase64EncodingType EncodingType
    );
};
