#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPNotifyAuthoring.generated.h"

class UAnimSequenceBase;
class UAnimSequence;
class USoundBase;
class USoundAttenuation;
class UVBGASPFoleyBank;

USTRUCT(BlueprintType)
struct FVBGASPNotifyMigrationEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString Kind;
	UPROPERTY(BlueprintReadOnly) FString ClassPath;
	UPROPERTY(BlueprintReadOnly) bool bNative = false;
	/** notify 객체 참조만 비운 event 데이터 전체 - 전후 비교용 */
	UPROPERTY(BlueprintReadOnly) FString EventData;
	UPROPERTY(BlueprintReadOnly) FString NotifyData;
};

/** 명시적 에디터 작업 - package 를 저장하거나 외부 GASP 프로젝트를 바꾸지 않는다 */
UCLASS()
class VBANIMATIONEDITOR_API UVBGASPNotifyAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static TArray<FVBGASPNotifyMigrationEntry> InspectNotifies(UAnimSequenceBase* Animation);

	/** 알려진 GASP notify 여섯 종류만 바꾼다 - 계약 하나라도 못 옮기면 -1 */
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static int32 ReplaceNotifies(UAnimSequenceBase* Animation, UVBGASPFoleyBank* Bank);

	/** Armed locomotion sequence 에 위상 맞춘 발 접촉 하나를 더한다 - 여러 번 불러도 안전하다 */
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool AddFoleyStep(UAnimSequence* Animation, UVBGASPFoleyBank* Bank, float Time,
		bool bLeftFoot, bool bRun);

	/** bank map 을 바꾸기 전에 입력을 모두 검사한다 */
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool ConfigureFoleyBank(UVBGASPFoleyBank* Bank, const TArray<FName>& Tags,
		const TArray<USoundBase*>& Sounds, USoundAttenuation* Attenuation);

	// 발소리 표는 ContextEffectComponent.Libraries 에 꽂는다
};
