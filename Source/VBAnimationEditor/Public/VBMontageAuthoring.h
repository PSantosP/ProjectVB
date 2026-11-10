// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBMontageAuthoring.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class USkeleton;
class UAnimNotifyState;

/** 몽타주 구조와 알림을 수정하는 에디터 도구. 저장은 호출자가 담당한다. */
UCLASS()
class VBANIMATIONEDITOR_API UVBMontageAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 지정한 상태 알림이 한 번만 등록돼 있을 때 해당 이벤트만 제거한다.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool RemoveMontageNotifyState(UAnimMontage* Montage, UAnimNotifyState* ExpectedState);

	// 지정한 상태 알림의 설정을 보존하고 시작 시각과 길이만 바꾼다.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageNotifyStateRange(UAnimMontage* Montage, UAnimNotifyState* ExpectedState, float StartTime, float Duration);

	// 몽타주 첫 슬롯 트랙의 슬롯명을 재저작 (DefaultSlot -> UpperBody 등). 반환: 트랙이 있어 바꿨으면 true.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageSlotName(UAnimMontage* Montage, FName NewSlotName);

	// 빈/끊긴 몽타주의 첫 슬롯 세그먼트에 애니를 다시 잇는다(RootMotion 복구). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageSegmentAnim(UAnimMontage* Montage, UAnimSequenceBase* Anim);

	// PoseSearchBranchIn 노티의 Database 를 지정 PSD 로 설정(reflection). 성공 시 true.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageBranchInDatabase(UAnimMontage* Montage, UObject* Database);

	// MotionWarping 노티의 RootMotionModifier.bWarpTranslation 을 reflection 으로 정하고 적용한 노티 수를 준다(0=노티 없음)
	// SkewWarp 의 위치 워프는 이 플래그가 꺼지면 무효인데, 노티가 protected 라 python 에서 못 바꾼다
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static int32 SetMontageWarpTranslation(UAnimMontage* Montage, bool bEnable);

	// 섹션의 다음 섹션 이름을 바꾼다. FCompositeSection 은 python 에 안 나와 있다.
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageSectionNext(UAnimMontage* Montage, FName SectionName, FName NextSectionName);

	// 첫 슬롯 첫 세그먼트의 원료 구간, 배속을 바꾸고 몽타주 길이를 다시 잰다 (길이는 C++ 의 CalculateSequenceLength 로만 잴 수 있다)
	// 노티는 몽타주 시각 그대로 남으니 다시 놓는 것은 부르는 쪽 몫이다
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageSegmentRange(UAnimMontage* Montage, float AnimStartTime, float AnimEndTime, float PlayRate);

	// 같은 일을 첫 슬롯의 SegmentIndex 번째 세그먼트에 한다 (한 클립을 배속이 다른 두 구간으로 나눌 때)
	// python 은 세그먼트 배열을 복사본으로만 고쳐서 두 번째 세그먼트를 바꿀 길이 없다
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool SetMontageSegmentRangeAt(UAnimMontage* Montage, int32 SegmentIndex, float AnimStartTime, float AnimEndTime, float PlayRate);

	// 골격에 몽타주 슬롯을 등록하고 그룹을 정한다 - 같은 그룹의 몽타주끼리 서로 끊는다
	// 그룹이 없으면 거절하고 새 그룹을 만들지 않는다
	UFUNCTION(BlueprintCallable, Category="VB|MontageAuthoring")
	static bool RegisterSkeletonSlot(USkeleton* Skeleton, FName SlotName, FName GroupName);
};
