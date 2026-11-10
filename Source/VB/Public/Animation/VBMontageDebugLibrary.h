// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBMontageDebugLibrary.generated.h"

class UAnimMontage;

/**
 * 파이썬 리플렉션이 못 보는 UAnimMontage 내부를 읽기 전용으로 노출하는 진단 라이브러리.
 * 자산 수정 함수는 에디터 모듈의 UVBMontageAuthoring 에 있다.
 */
UCLASS()
class VB_API UVBMontageDebugLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 몽타주 첫 슬롯 트랙의 슬롯명 (SlotAnimTracks[0].SlotName). 트랙 없으면 NAME_None.
	// 런타임에서도 쓴다: VBWeaponStateComponent 가 무기 교체마다 상체/전신 판정에 부른다. 제거 금지.
	UFUNCTION(BlueprintCallable, Category="VB|Debug")
	static FName GetMontageSlotName(UAnimMontage* Montage);

	// 몽타주 Notify 들의 요약 문자열 (이름 @시간 [class]). MotionWarping notify 유무 확인용.
	UFUNCTION(BlueprintCallable, Category="VB|Debug")
	static TArray<FString> GetMontageNotifySummary(UAnimMontage* Montage);

	// 몽타주 첫 슬롯의 source AnimSequence 정보 (이름/enableRM/forceRootLock). RootMotion 0 원인 진단.
	UFUNCTION(BlueprintCallable, Category="VB|Debug")
	static FString GetMontageRootMotionInfo(UAnimMontage* Montage);

	// 섹션의 다음 섹션 이름. NAME_None 이면 그 섹션에서 몽타주가 멈춘다(콤보는 GA 가 명시적으로 점프한다).
	// FCompositeSection 은 파이썬에 노출되지 않아 여기서 읽는다.
	UFUNCTION(BlueprintCallable, Category="VB|Debug|Montage")
	static FName GetMontageSectionNext(UAnimMontage* Montage, FName SectionName);
};
